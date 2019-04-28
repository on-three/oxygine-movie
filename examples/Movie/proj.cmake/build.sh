#!/bin/bash

mkdir build
cd build

#generate cmake project in the "build" folder
cmake ..

#build it
make VERBOSE=1

