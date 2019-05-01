#!/bin/bash
set -e

BUILD=Debug
BUILD_DIR=web-build
MOVIE_DIR=../data/movies
TARGET=Movie.html

mkdir -p ${BUILD_DIR}

# We access videos via http so we need them in our bin folder
cp -r ${MOVIE_DIR} ${BUILD_DIR}

cd ${BUILD_DIR}

MAKE="emmake make"
CMAKE="emcmake cmake"

#generate cmake project in the "build" folder
${CMAKE} -DCMAKE_BUILD_TYPE=${BUILD} ..

#build it
${MAKE} VERBOSE=1
