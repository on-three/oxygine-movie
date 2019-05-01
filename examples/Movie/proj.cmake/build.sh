#!/bin/bash

export BUILD=Debug

mkdir build
cd build

#generate cmake project in the "build" folder
cmake -DCMAKE_BUILD_TYPE=${BUILD} ..

#build it
make VERBOSE=1

