//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/urpc
//

#include <chrono>
#include <random>
#include <thread>
#include <iostream>
#include <urpc.hpp>
#include <arith.pb.h>

namespace net = unp;
namespace gp = google::protobuf;

using steady_t = net::monotonic_clock;
using time_point_t = typename steady_t::time_point;

struct task
{
    urpc::controller controller;

    pb::request request;
    pb::response response;

    urpc::Closure* done;

    time_point_t time_begin;
    time_point_t time_end;
};

class client
{
public:
    client(net::io_uring_context& ioc, const std::string& host, const std::string& port) : ioc(ioc), host(host), port(port), dist(0, 100)
    {
        channel = new urpc::channel(ioc);
        service = new pb::service::Stub(channel, pb::service::STUB_OWNS_CHANNEL);
    }

    void invoke()
    {
        auto t = std::make_shared<task>();
        auto& controller = t->controller;

        controller.host(host);
        controller.port(port);

        controller.timeout(80);

        auto& request = t->request;
        auto op = pb::opcode(dist(generator) % 5);

        request.set_op(op);

        request.set_lhs(dist(generator));
        request.set_rhs(dist(generator));

        std::cout << "requqest: " << request.DebugString();

        t->time_begin = steady_t::now();
        t->done = gp::NewCallback(this, &client::done, t);

        service->compute(&t->controller, &t->request, &t->response, t->done);
    }

    void done(std::shared_ptr<task> t)
    {
        t->time_end = steady_t::now();
        auto& controller = t->controller;

        auto period = t->time_end - t->time_begin;
        auto number = std::chrono::duration_cast<std::chrono::microseconds>(period).count();

        if (controller.Failed())
            std::cerr << "ErrorCode: " << controller.ErrorCode() << " ErrorText: " << controller.ErrorText() << std::endl;

        std::cout << "response " << t->response.DebugString();
        std::cout << "it takes " << number << " us" << std::endl;
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

    std::default_random_engine generator;
    std::uniform_int_distribution<int> dist;
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

    constexpr size_t size = 20;
    char buff[size];

    while (fgets(buff, size, stdin))
           c.invoke();

    t.join();

    return 0;
}
