//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/urpc
//

#include <thread>
#include <iostream>
#include <urpc.hpp>
#include <ping.pb.h>

namespace net = unp;
namespace gp = google::protobuf;

struct task
{
    urpc::controller controller;

    pb::request request;
    pb::response response;

    urpc::Closure* done;
};

class client
{
public:
    client(net::io_uring_context& ioc, const std::string& host, const std::string& port) : ioc(ioc), host(host), port(port)
    {
        channel = new urpc::channel(ioc);
        service = new pb::service::Stub(channel, pb::service::STUB_OWNS_CHANNEL);
    }

    void ping()
    {
        auto t = std::make_shared<task>();
        auto& controller = t->controller;

        controller.host(host);
        controller.port(port);

        controller.timeout(80);

        t->request.set_command("ping");
        t->done = gp::NewCallback(this, &client::done, t);

        service->execute(&t->controller, &t->request, &t->response, t->done);
    }

    void done(std::shared_ptr<task> t)
    {
        auto& controller = t->controller;

        if (controller.Failed())
            std::cerr << "ErrorCode: " << controller.ErrorCode() << " ErrorText: " << controller.ErrorText() << std::endl;

        std::cout << t->response.DebugString();
    }

    ~client()
    {
        delete service;
    }

private:
    net::io_uring_context& ioc;

    urpc::channel* channel;
    pb::service* service;

    std::string host;
    std::string port;
};

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        std::cout << "Usage: " << argv[0] << " <host> <port>" << std::endl;

        return 1;
    }

    std::string host(argv[1]);
    std::string port(argv[2]);

    net::io_uring_context ioc;
    net::inplace_stop_source source;

    client c(ioc, host, port);
    std::thread t([&]{ ioc.run(source.get_token()); });

    constexpr size_t size = 100;
    char buff[size];

    while (fgets(buff, size, stdin))
           c.ping();

    t.join();

    return 0;
}
