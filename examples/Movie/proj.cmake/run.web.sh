#!/bin/bash
set -e

BUILD_SCRIPT=build.web.sh
BUILD_DIR=web-build
TARGET=Movie.html

./${BUILD_SCRIPT}

cd ${BUILD_DIR}
emrun ${TARGET}
