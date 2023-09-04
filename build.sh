#!/bin/bash

dst=/tmp
flags="-std=c++23 -Wall -O3 -Os -s -I . -I ../include -I ../../unp/include -lprotobuf -lpthread"

cd example

for p in *.proto; do
    base=${p/.proto/}
    protoc --cpp_out=. ${p}

    g++ ${flags} ${base}_client.cpp ${base}.pb.cc -o ${dst}/${base}_client
    g++ ${flags} ${base}_server.cpp ${base}.pb.cc -o ${dst}/${base}_server
done

rm -f *.pb.*
echo Please check the executables at ${dst}
