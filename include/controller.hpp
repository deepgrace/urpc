//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/urpc
//

#ifndef CONTROLLER_HPP
#define CONTROLLER_HPP

#include <unp.hpp>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>

namespace urpc
{
    namespace net = unp;
    namespace gp = google::protobuf;

    using tcp = net::ip::tcp;
    using socket_t = tcp::socket;

    using endpoint_t = tcp::endpoint;
    using error_code_t = std::error_code; 

    using Closure = gp::Closure;
    using Message = gp::Message;

    using Service = gp::Service;
    using RpcChannel = gp::RpcChannel;

    using RpcController = gp::RpcController;
    using MethodDescriptor = gp::MethodDescriptor;

    enum status
    {
        OOM,
        ERROR,
        FAILED,
        SUCCEED,
        UNFOUND,
        TIMEDOUT
    };

    class controller : public RpcController
    {
    public:
        controller(const std::string& host = {}, const std::string& port = {}, uint32_t timeout = 0) : host_(host), port_(port), timeout_(timeout)
        {
        }

        virtual void Reset()
        {
            failed = false;
            cancelled = false;

            error_text.clear();
            error_code = SUCCEED;
        }

        virtual bool Failed() const
        {
            return failed;
        }

        virtual std::string ErrorText() const
        {
            return error_text;
        }

        virtual void StartCancel()
        {
            cancelled = true;
        }

        virtual void SetFailed(const std::string& reason)
        {
            failed = true;

            error_text = reason;
            error_code = FAILED;
        }

        void SetFailed(const std::string& reason, status status)
        {
            SetFailed(reason);
            ErrorCode(status);
        }

        virtual bool IsCanceled() const
        {
            return cancelled;
        }

        virtual void NotifyOnCancel(Closure* callback)
        {
            callback_ = callback;
        }

        void host(const std::string& host)
        {
            host_ = host;
        }

        void port(const std::string& port)
        {
            port_ = port;
        }

        void timeout(uint32_t timeout)
        {
            timeout_ = timeout;
        }

        void ErrorCode(status status)
        {
            error_code = status;
        }

        Closure* callback() const
        {
            return callback_;
        }

        std::string& host()
        {
            return host_;
        }

        std::string& port()
        {
            return port_;
        }

        uint32_t& timeout()
        {
            return timeout_;
        }

        status ErrorCode() const
        {
            return error_code;
        }

        virtual ~controller()
        {
        }

    private:
        bool failed = false;
        bool cancelled = false;

        std::string host_;
        std::string port_;

        uint32_t timeout_;
        Closure* callback_;

        std::string error_text;
        status error_code = SUCCEED;
    };
}

#endif
