//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/urpc
//

#ifndef HEADER_HPP
#define HEADER_HPP

#include <unordered_map>
#include <controller.hpp>

namespace urpc
{
    struct request
    {
        uint64_t id; 
        std::string name;
    };

    struct response
    {
        uint64_t id;

        urpc::status status;
        std::string message;
    };

    struct header
    {
        uint32_t rpc_len;
        uint32_t arg_len;

        char data[];
    };

    inline constexpr bool allocate(header*& buff, uint32_t& size, uint32_t count)
    {
        if (!buff || size < count)
        {
            if (size == 0)
                size = 1;

            while (size < count)
                size *= 2;

            if (buff = static_cast<header*>(realloc(buff, size)); !buff)
                return false;
        }

        return true;
    }

    template <bool B, typename U, typename L, typename S, typename T>
    constexpr decltype(auto) copy(L&& l, S&& s, T&& t, size_t size = sizeof(U))
    {
        auto dst = (void*)(s.data() + l);
        auto src = std::addressof(std::forward<T>(t));

        if constexpr(B)
            std::memcpy(dst, src, size);
        else
            std::memcpy(src, dst, size);

        return size;
    }

    template <bool B, typename T>
    constexpr decltype(auto) copy(header* buff, T& t, std::string& m)
    {
        size_t l = 0;
        size_t size = m.size();

        std::string_view s(buff->data, buff->rpc_len);

        if constexpr(! requires { t.id; })
            l += copy<B, uint64_t>(l, s, t);
        else
        {
            l += copy<B, uint64_t>(l, s, t.id);
            l += copy<B, status>(l, s, t.status);
        }

        l += copy<B, size_t>(l, s, size);

        m.resize(size);
        l += copy<B, size_t>(l, s, m[0], size);
    }
}

#endif
