//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/urpc
//

#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <header.hpp>

namespace urpc
{
    struct call
    {
        call(net::io_uring_context& ioc, uint64_t id) : timer(ioc), id(id)
        {
        }

        const MethodDescriptor* method;
        urpc::controller* controller;

        const Message* request;
        Message* response;

        Closure* done;
        uint32_t timeout;

        header* buff = nullptr;
        net::steady_timer timer;

        uint64_t id;
        uint32_t size = 0;

        bool called= false;

        ~call()
        {
            if (buff && size)
            {
                size = 0;
                free(buff);
            }
        }
    };

    template <typename T>
    class client : public std::enable_shared_from_this<client<T>>
    {
    public:
        using task_t = std::shared_ptr<call>;
        using tasks_t = std::unordered_map<uint64_t, task_t>;

        client(T& channel, net::io_uring_context& ioc, const std::string& endpoint) : channel(channel), ioc(ioc), socket(ioc), endpoint(endpoint)
        {
        }

        constexpr decltype(auto) shared_this()
        {
            return this->shared_from_this();
        }

        void set_done(task_t task)
        {
            task->timer.cancel();
            execute(task);
        }

        void execute(task_t& task, const std::string& reason = {}, status status = FAILED)
        {
            if (task->called)
                return;

            auto& c = task->controller; 

            if (!reason.empty())
                c->SetFailed(reason, status);

            try
            {
                task->done->Run();
            }
            catch (std::exception& e)
            {
                c->SetFailed(std::string("Run: ") + e.what());
            }

            task->called = true;
        }

        void close(error_code_t ec)
        {
            auto reason = ec.message();

            socket.shutdown(socket_t::shutdown_both);
            socket.close();

            channel.remove(endpoint);

            for (auto& [_, task] : tasks)
            {
                 task->timer.cancel();
                 execute(task, reason);
            }

            tasks.clear();
        }

        void CallMethod(const MethodDescriptor* method, controller* controller, const Message* request, Message* response, Closure* done)
        {
            auto task = std::make_shared<call>(ioc, ++id);

            task->method = method;
            task->controller = controller;

            task->request = request;
            task->response = response;

            task->done = done;

            if (socket.is_open())
                do_write(task);
            else
            {
                do_connect(task);
                connecting = true;

                reset_timer(task);
            }
        }

        void reset_timer(task_t task)
        {
            if (auto n = task->controller->timeout(); n)
            {
                task->timer.expires_from_now(std::chrono::milliseconds(n));

                task->timer.async_wait([task, self = shared_this()](error_code_t ec)
                {
                    self->on_timeout(task, ec);
                });
            }
        }

        void on_timeout(task_t task, error_code_t ec)
        {
            if (!ec)
            {
                tasks.erase(task->id);
                execute(task, std::string("Connection timed out"), TIMEDOUT);

                if (connecting)
                    close(std::make_error_code(std::errc::timed_out));
            }
            else if (ec == std::errc::operation_canceled)
            {
            }
        }

        void do_connect(task_t task)
        {
            auto& c = task->controller;

            net::async_connect(socket, tcp::endpoint(net::ip::make_address(c->host()), std::stoi(c->port())),
            [task, self = shared_this()](error_code_t ec, int fd)
            {
                self->on_connect(task, ec);
            });
        }

        void on_connect(task_t task, error_code_t ec)
        {
            if (!ec)
            {
                connecting = false;

                do_write(task);
                do_read_header();
            }
            else
            {
                execute(task, std::string("async_connect: ") + ec.message());
                close(ec);
            }
        }

        void do_read_header()
        {
            if (!allocate(buff, size, sizeof(header)))
                return close(std::make_error_code(std::errc::not_enough_memory));

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
                    return close(std::make_error_code(std::errc::not_enough_memory));

                do_read_message();
            }
            else
                close(ec);
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
                response rep;
                copy<0>(buff, rep, rep.message);

                auto it = tasks.find(rep.id);

                if (it == tasks.end())
                    return do_read_header();

                task_t task = it->second;

                if (rep.status != SUCCEED)
                    task->controller->SetFailed(rep.message, rep.status);

                if (!task->response->ParseFromArray(buff->data + buff->rpc_len, buff->arg_len))
                    task->controller->SetFailed("Cannot ParseFromArray", ERROR);

                do_read_header();

                tasks.erase(it);
                set_done(task);
            }
            else
                close(ec);
        }

        void do_write(task_t task)
        {
            std::string name = task->method->service()->name() + "." + task->method->name();

            uint32_t rpc_len = sizeof(task->id) + sizeof(size_t) + name.size();
            uint32_t arg_len = task->request->ByteSizeLong();

            count = sizeof(urpc::header) + rpc_len + arg_len;

            if (!allocate(task->buff, task->size, count))
            {
                task->controller->SetFailed("Cannot allocate memory", OOM);

                return set_done(task);
            }

            task->buff->rpc_len = rpc_len;
            task->buff->arg_len = arg_len;

            copy<1>(task->buff, task->id, name);

            if (!task->request->SerializeToArray(task->buff->data + rpc_len, arg_len))
            {
                task->controller->SetFailed("Cannot SerializeToArray", ERROR);

                return set_done(task);
            }

            tasks.try_emplace(task->id, task);
            reset_timer(task);

            net::async_write(socket, net::buffer(task->buff, count),
            [task, self = shared_this()](error_code_t ec, std::size_t bytes_transferred)
            {
                self->on_write(task, ec, bytes_transferred);
            });
        }

        void on_write(task_t task, error_code_t ec, std::size_t bytes_transferred)
        {
            if (ec)
            {
                execute(task, std::string("async_write: ") + ec.message());
                close(ec);
            }
        }

        ~client()
        {
            if (buff && size)
            {
                size = 0;
                free(buff);
            }
        }

    private:
        T& channel;
        net::io_uring_context& ioc;

        uint64_t id = 0;
        tasks_t tasks;

        socket_t socket;
        std::string endpoint;

        uint32_t size = 0;
        uint32_t count = 0;

        header* buff = nullptr;
        bool connecting = false;
    };

    class channel : public RpcChannel
    {
    public:
        using client_t = client<channel>;

        using connection_t = std::shared_ptr<client_t>;
        using connections_t = std::unordered_map<std::string, connection_t>;

        channel(net::io_uring_context& ioc) : ioc(ioc)
        {
        }

        void CallMethod(const MethodDescriptor* method, RpcController* Controller, const Message* request, Message* response, Closure* done)
        {
            auto c = static_cast<controller*>(Controller);

            auto endpoint = c->host() + ":" + c->port();
            auto it = connections.find(endpoint);

            connection_t conn;

            if (it == connections.end())
            {
                conn = std::make_shared<client_t>(*this, ioc, endpoint);
                connections.try_emplace(endpoint, conn);
            }
            else
                conn = it->second;

            conn->CallMethod(method, c, request, response, done);
        }

        void remove(const std::string& endpoint) 
        {
            connections.erase(endpoint);
        }

        ~channel()
        {
        }

    private:
        net::io_uring_context& ioc;
        connections_t connections;
    };
}

#endif
