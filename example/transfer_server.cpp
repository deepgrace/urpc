//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/urpc
//

#include <fstream>
#include <iostream>
#include <filesystem>
#include <urpc.hpp>
#include <transfer.pb.h>

namespace net = unp;

namespace fs = std::filesystem;
namespace gp = google::protobuf;

std::string on_transfer(const fs::path& path, bool pre_transfer)
{
    std::string file = path;

    if (pre_transfer)
    {
        if (file.back() == '/')
        {
            file.pop_back();
            std::cout << "begin receiving directory " << file << std::endl;
        }
        else
            std::cout << "begin receiving file " << file << std::endl;
    }
    else
    {
        if (fs::is_directory(file))
            std::cout << "after receiving directory " << file << std::endl;
        else if (fs::is_regular_file(file))
            std::cout << "after receiving file " << file << std::endl;
    }

    return file;
}

void done()
{
}

class service : public pb::service
{
public:
    service(const fs::path& path)
    {
        fs::create_directories(path);
        chdir(path.c_str());
    }

    template <typename T, typename U>
    void set_res(const T* req, U* res, gp::Closure* done, bool result)
    {
        res->set_success(result);
        res->set_id(req->id());

        done->Run();
    }

    constexpr decltype(auto) upon_transfer(const std::string& name, bool b)
    {
        auto p = name.find_first_of('/');

        if (p == std::string::npos)
            p = name.size() - b;

        return on_transfer(name.substr(0, p + b), b);
    }

    void data_transfer(gp::RpcController* controller, const pb::data_req* req, pb::data_res* res, gp::Closure* done)
    {
        std::string name = req->name();

        size_t size = name.size();
        set_res(req, res, done, size);

        if (!size)
            return;

        if (req->id() == 1)
            upon_transfer(name, true);

        bool set_perms = false;
        bool is_dir = name.back() == '/';

        if (is_dir)
            name.pop_back();

        if (name != last)
        {
            last = name;
            set_perms = true;

            std::cout << "receiving " << name << std::endl;
        }

        if (is_dir)
            fs::create_directories(name);
        else
        {
            if (set_perms)
            {
                if (auto parent = fs::path(name).parent_path(); !parent.empty())
                    fs::create_directories(parent);

                if (fout.is_open())
                    fout.close();

                fout.open(name, std::ios_base::app | std::ios_base::binary);
            }

            auto& data = req->data();
            fout.write(data.c_str(), data.size());
        }

        if (set_perms)
            fs::permissions(name, fs::perms(req->perms()));
    }

    void done_transfer(gp::RpcController* controller, const pb::done_req* req, pb::done_res* res, gp::Closure* done)
    {
        std::string name = req->name();

        size_t size = name.size();
        set_res(req, res, done, size);

        if (!size)
            return;

        if (fout.is_open())
            fout.close();

        upon_transfer(name, false);
    }

    ~service()
    {
    }

    std::string last;
    std::ofstream fout;
};

int main(int argc, char* argv[])
{
    if (argc != 4)
    {
        std::cout << "Usage: " << argv[0] << " <host> <port> <path>" << std::endl;

        return 1;
    }

    std::string host(argv[1]);
    std::string port(argv[2]);

    std::string path(argv[3]);

    net::io_uring_context ioc;
    net::inplace_stop_source source;

    service s(path);
    urpc::server server(ioc, host, port);

    server.register_service(&s, gp::NewPermanentCallback(&done));
    server.run();

    ioc.run(source.get_token());

    return 0;
}
