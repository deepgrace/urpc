//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/urpc
//

#include <array>
#include <deque>
#include <thread>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <urpc.hpp>
#include <transfer.pb.h>

namespace net = unp;

namespace fs = std::filesystem;
namespace gp = google::protobuf;

using steady_t = net::monotonic_clock;

using time_point_t = typename steady_t::time_point;
using iterator_t = fs::recursive_directory_iterator;

template <typename T, typename U>
struct task_type
{
    uint64_t id;
    urpc::controller controller;

    T request;
    U response;

    urpc::Closure* done;

    time_point_t time_begin;
    time_point_t time_end;
};

using data_type_t = task_type<pb::data_req, pb::data_res>;
using done_type_t = task_type<pb::done_req, pb::done_res>;

using data_task_t = std::shared_ptr<data_type_t>;
using done_task_t = std::shared_ptr<done_type_t>;

class client
{
public:
    client(net::io_uring_context& ioc, net::inplace_stop_source& source, const std::string& host, const std::string& port) : ioc(ioc), source(source), host(host), port(port)
    {
        channel = new urpc::channel(ioc);
        service = new pb::service::Stub(channel, pb::service::STUB_OWNS_CHANNEL);
    }

    bool eof(const std::string& file)
    {
        return fin.gcount() != size || fs::file_size(file) == size;
    }

    std::string relative(const fs::path& path)
    {
        if (parent_.empty())
            return path;

        return fs::relative(path, parent_);
    }

    template <typename T>
    void set_last(T& t, const fs::path& path)
    {
        set_perms(t, path);
        last = relative(path);

        std::cout << "transferring " << last << std::endl;
    }

    void init(const fs::path& path)
    {
        path_ = path;
        parent_ = path_.parent_path();

        auto value = !fs::is_directory(path_);
        range = value ? iterator_t() : iterator_t(path_);

        begin = value ? iterator_t() : fs::begin(range);
        end = value ? iterator_t() : fs::end(range);
    }

    template <typename T>
    void set_perms(T& t, const fs::path& path)
    {
        fs::file_status s = fs::status(path); 
        t.set_perms(std::to_underlying(s.permissions()));
    }

    template <typename S, typename R, typename C, typename... Args, typename T>
    void invoke(S& s, R (C::*m)(Args...), T& t)
    {
        (s->*m)(&t->controller, &t->request, &t->response, t->done);
    }

    template <typename T, typename R, typename C, typename... Args>
    void fill(T& task, uint64_t id, R (C::*m)(Args...))
    {
        task->id = id;
        auto& controller = task->controller;

        controller.host(host);
        controller.port(port);

        controller.timeout(80);
        task->request.set_id(id);

        task->time_begin = steady_t::now();
        task->done = gp::NewCallback(this, m, task);
    }

    void transfer(pb::data_req& req)
    {
        auto path = begin->path();

        while (fs::is_directory(path) && !fs::is_empty(path))
        {
            ++begin;
            path = begin->path();
        }

        if (fs::is_directory(path))
        {
            set_last(req, path);
            req.set_name(last + "/");
        }
        else if (fs::is_regular_file(path))
            transfer(req, path);
    }

    void transfer(pb::data_req& req, const fs::path& path)
    {
        if (!fs::file_size(path))
            set_last(req, path);
        else
        {
            if (!fin.is_open())
            {
                set_last(req, path);
                fin.open(path, std::ios_base::in | std::ios_base::binary);
            }

            fin.read(buffer.data(), buffer.size());
            req.set_data(buffer.data(), fin.gcount());
        }

        req.set_name(last);
    }

    void transfer(const fs::path& path)
    {
        bool write_in_progress = !paths.empty();
        paths.push_back(path);

        if (!write_in_progress)
        {
            init(paths.front());
            do_data_transfer();
        }
    }

    void do_data_transfer()
    {
        auto task = std::make_shared<data_type_t>();
        fill(task, ++data_id, &client::on_data_transfer);

        auto& req = task->request;

        if (begin != end)
            transfer(req);
        else if (fs::is_regular_file(path_))
            transfer(req, path_);
        else if (fs::is_directory(path_))
        {
            set_last(req, path_);
            req.set_name(last + "/");
        }
        else
        {
            std::cerr << "cannot transfer '" << path_ << "': No such file or directory" << std::endl;

            return;
        }

        invoke(service, &pb::service::data_transfer, task);
    }

    void on_data_transfer(data_task_t task)
    {
        task->time_end = steady_t::now();
        auto& controller = task->controller;

        auto& req = task->request;
        auto& res = task->response;

        if (controller.Failed())
        {
            std::cerr << "ErrorCode: " << controller.ErrorCode() << " ErrorText: " << controller.ErrorText() << std::endl;

            return;
        }

        if (begin != end)
        {
            auto path = begin->path();

            if (fs::is_directory(path))
                ++begin;
            else if (fs::is_regular_file(path) && eof(path))
            {
                ++begin;
                fin.close();
            }
        }

        if (res.success())
        {
            if (begin == end)
            {
                if (fs::is_regular_file(path_))
                {
                    if (eof(path_))
                        fin.close();
                    else
                        return do_data_transfer();
                }

                do_done_transfer();
            }
            else
                do_data_transfer();
        }
        else
            std::cerr << "transfer " << req.id() << " failed" << std::endl;
    }

    void do_done_transfer()
    {
        auto task = std::make_shared<done_type_t>();
        fill(task, ++done_id, &client::on_done_transfer);

        task->request.set_name(last);
        invoke(service, &pb::service::done_transfer, task);
    }

    void on_done_transfer(done_task_t task)
    {
        task->time_end = steady_t::now();
        auto& controller = task->controller;

        auto& req = task->request;
        auto& res = task->response;

        if (controller.Failed())
        {
            std::cerr << "ErrorCode: " << controller.ErrorCode() << " ErrorText: " << controller.ErrorText() << std::endl;

            return;
        }

        if (res.success())
        {
            data_id = 0;
            done_id = 0;

            if (paths.pop_front(); paths.empty())
                stop();
            else
            {
                init(paths.front());
                do_data_transfer();
            }
        }
        else
            std::cerr << "transfer " << req.id() << " failed" << std::endl;
    }

    void stop()
    {
        source.request_stop();
    }

    ~client()
    {
        delete service;
    }

private:
    net::io_uring_context& ioc;
    net::inplace_stop_source& source;

    urpc::channel* channel;
    pb::service* service;

    std::string host;
    std::string port;

    fs::path path_;
    fs::path parent_;

    std::string last;
    iterator_t range;

    iterator_t begin;
    iterator_t end;

    std::ifstream fin;
    std::deque<fs::path> paths;

    uint64_t data_id = 0;
    uint64_t done_id = 0;

    static constexpr int size = 65536;
    std::array<char, size> buffer;
};

int main(int argc, char* argv[])
{
    if (argc < 4)
    {
        std::cout << "Usage: " << argv[0] << " <host> <port> <path> [<path> ...]" << std::endl;

        return 1;
    }

    bool called = false;

    std::string host(argv[1]);
    std::string port(argv[2]);

    net::io_uring_context ioc;
    net::inplace_stop_source source;

    client c(ioc, source, host, port);
    std::thread t([&]{ ioc.run(source.get_token()); });

    for (int i = 3; i != argc; ++i)
    {
         if (auto path = fs::path(argv[i]); fs::exists(path))
         {
             called = true;
             c.transfer(path);
         }
    }

    if (!called)
        c.stop();

    t.join();

    return 0;
}
