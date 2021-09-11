#!/usr/bin/bash

set -euox pipefail

c++ sqlite_blocking.cc sqlite_cpp.cc sqlite_cpp_test.cc -std=c++17 -lsqlite3
./a.out
