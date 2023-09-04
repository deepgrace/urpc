#
# Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/deepgrace/urpc
#

SET(CMAKE_VERBOSE_MAKEFILE ON)
SET(CMAKE_CXX_FLAGS "-std=c++23 -Wall -O3 -Os -s")

include(GNUInstallDirs)

include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_BINARY_DIR}/example)
include_directories(${PROJECT_SOURCE_DIR}/../unp/include)

find_package(Protobuf REQUIRED)

function(compile BIN PROTO)
    PROTOBUF_GENERATE_CPP(PROTO_SRC PROTO_HEADER ${PROTO})
    add_executable(${BIN} ${PROTO_SRC} "${BIN}.cpp")
    target_link_libraries(${BIN} ${PROTOBUF_LIBRARY} pthread)
    install(TARGETS ${BIN} DESTINATION ${PROJECT_SOURCE_DIR}/bin)
endfunction()

function(add_file NAME)
    compile("${NAME}_client" "${NAME}.proto")
    compile("${NAME}_server" "${NAME}.proto")
endfunction()

file(GLOB PROTOS "*.proto")

foreach(file-path ${PROTOS})
    string(REPLACE ".proto" "" file-path-without-ext ${file-path})
    get_filename_component(file-name ${file-path-without-ext} NAME)
    add_file(${file-name})
endforeach()
