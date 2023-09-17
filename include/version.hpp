//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/urpc
//

#ifndef URPC_VERSION_HPP
#define URPC_VERSION_HPP

#define URPC_STRINGIZE(T) #T

/*
 *   URPC_VERSION_NUMBER
 *
 *   Identifies the API version of urpc.
 *   This is a simple integer that is incremented by one every
 *   time a set of code changes is merged to the master branch.
 */

#define URPC_VERSION_NUMBER 2
#define URPC_VERSION_STRING "urpc/" URPC_STRINGIZE(URPC_VERSION_NUMBER)

#endif
