#!/bin/bash

BUILD_DIR=web-build
TARGET=Movie.html

mkdir ${BUILD_DIR}
cd ${BUILD_DIR}

MAKE="emmake make"
CMAKE="emcmake cmake"

#generate cmake project in the "build" folder
${CMAKE} ..

#build it
${MAKE} VERBOSE=1

