//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/urpc
//

#ifndef SERVER_HPP
#define SERVER_HPP

#include <header.hpp>

namespace urpc
{
    template <typename T>
    class session : public std::enable_shared_from_this<session<T>>
    {
    public:
        using message_ptr = std::unique_ptr<Message>;

        session(T& server, socket_t socket) : server(server), socket(std::move(socket))
        {
        }

        constexpr decltype(auto) shared_this()
        {
            return this->shared_from_this();
        }

        void set_res(uint64_t id, status status, const std::string& message)
        {
            res.id = id;

            res.status = status;
            res.message = message;
        }

        void close()
        {
            server.remove(uint64_t(this));

            if (!socket.is_open())
                return;

            socket.shutdown(socket_t::shutdown_both);
            socket.close();
        }

        void run()
        {
            do_read_header();
        }

        void do_read_header()
        {
            if (!allocate(buff, size, sizeof(header)))
                return close();

            net::async_read(socket, net::buffer(buff, sizeof(header)),
            [self = shared_this()](error_code_t ec, std::size_t bytes_transferred)
            {
                self->on_read_header(ec, bytes_transferred);
            });
        }

        void on_read_header(error_code_t ec, std::size_t bytes_transferred)
        {
            if (!ec)
            {
                count = buff->rpc_len + buff->arg_len;

                if (!allocate(buff, size, count + sizeof(header)))
                    return close();

                do_read_message();
            }
            else
                close();
        }

        void do_read_message()
        {
            net::async_read(socket, net::buffer(buff->data, count),
            [self = shared_this()](error_code_t ec, std::size_t bytes_transferred)
            {
                self->on_read_message(ec, bytes_transferred);
            });
        }

        void on_read_message(error_code_t ec, std::size_t bytes_transferred)
        {
            if (!ec)
            {
                copy<0>(buff, req.id, req.name);

                auto& name = req.name;
                size_t pos = name.find_first_of('.');

                if (pos == std::string::npos)
                {
                    set_res(req.id, UNFOUND, "invalid method identity");
                    do_write(nullptr);

                    return;
                }

                auto& srv = server.services();
                auto it = srv.find(name.substr(0, pos));

                if (it == srv.end())
                {
                    set_res(req.id, UNFOUND, "service not found");
                    do_write(nullptr);

                    return;
                }

                auto& p = it->second;
                Service* s = p.first;

                const MethodDescriptor* method = s->GetDescriptor()->FindMethodByName(name.substr(pos + 1));

                if (!method)
                {
                    set_res(req.id, UNFOUND, "method not found");
                    do_write(nullptr);

                    return;
                }

                message_ptr Request(s->GetRequestPrototype(method).New());

                if (!Request->ParseFromArray(buff->data + buff->rpc_len, buff->arg_len))
                    return close();

                controller controller;
                message_ptr Response(s->GetResponsePrototype(method).New());

                try
                {
                    s->CallMethod(method, &controller, Request.get(), Response.get(), p.second);
                }
                catch(std::exception& e)
                {
                    controller.SetFailed(std::string("Server Internal Error ") + e.what());
                }

                res.id = req.id;
                res.status = SUCCEED;

                if (controller.Failed())
                {
                    res.status = FAILED;
                    res.message = controller.ErrorText();
                }

                do_write(Response.get());
            }
            else
                close();
        }

        void do_write(Message* msg)
        {
            size_t rpc_len = sizeof(res.id) + sizeof(status) + sizeof(size_t) + res.message.size();
            size_t arg_len = msg ? msg->ByteSizeLong() : 0;

            count = sizeof(header) + rpc_len + arg_len;

            if (!allocate(buff, size, count))
                return close();

            buff->rpc_len = rpc_len;
            buff->arg_len = arg_len;

            copy<1>(buff, res, res.message);

            if (msg && !msg->SerializeToArray(buff->data + rpc_len, arg_len))
                return close();

            net::async_write(socket, net::buffer(buff, count),
            [self = shared_this()](error_code_t ec, std::size_t bytes_transferred)
            {
                self->on_write(ec, bytes_transferred); 
            });
        }

        void on_write(error_code_t ec, std::size_t bytes_transferred)
        {
            if (!ec)
                do_read_header();
            else
                close();
        }

        ~session()
        {
            if (buff && size)
            {
                size = 0;
                free(buff);
            }
        }

    private:
        T& server;

        socket_t socket;
        header* buff = nullptr;

        request req;
        response res;

        uint32_t size = 0;
        uint32_t count = 0;
    };

    class server
    {
    public:
        using service_t = std::pair<Service*, Closure*>;
        using services_t = std::unordered_map<std::string, service_t>;

        using connection_t = std::shared_ptr<session<server>>;
        using connections_t = std::unordered_map<uint64_t, connection_t>;

        server(net::io_uring_context& ioc, const std::string& host, const std::string& port) :
        ioc(ioc), acceptor(ioc, tcp::endpoint(net::ip::make_address(host), std::stoi(port)))
        {
        }

        server(net::io_uring_context& ioc, const std::string& port) : ioc(ioc), acceptor(ioc, tcp::endpoint(tcp::v4(), std::stoi(port)))
        {
        }

        void run()
        {
            do_accept();
        }

        services_t& services()
        {
            return services_;
        }

        void remove(uint64_t c)
        {
            connections.erase(c);
        }

        bool register_service(Service* service, Closure* closure)
        {
            std::string key = service->GetDescriptor()->name();

            if (services_.contains(key))
                return false;

            services_.try_emplace(key, std::make_pair(service, closure));

            return true;
        }

        void do_accept()
        {
            acceptor.async_accept(
            [this](error_code_t ec, socket_t socket)
            {
                on_accept(ec, std::move(socket));
            });
        }

        void on_accept(error_code_t ec, socket_t socket)
        {
            if (!ec)
            {
                auto c = std::make_shared<session<server>>(*this, std::move(socket));
                connections.try_emplace(uint64_t(c.get()), c);

                c->run();
            }

            do_accept();
        }

        ~server()
        {
        }

    private:
        net::io_uring_context& ioc;
        tcp::acceptor acceptor;

        services_t services_;
        connections_t connections;
    };
}

#endif
