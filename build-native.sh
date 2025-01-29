#!/bin/bash
# TODO: replace this by a Makefile

source build.cfg

INCLUDE_DIRS="$INCLUDE_DIRS -Isubprojects/asio/include -Isubprojects/websocketpp"
DEFINES="$DEFINES -DASIO_STANDALONE -DWSWRAP_SEND_EXCEPTIONS" # not using boost
LIBS="$LIBS -pthread -lssl -lcrypto -lz -Wno-deprecated-declarations"
BUILD_DIR="build/native"
CPP="g++"

# clean up
rm -Rf --one-file-system "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

if [[ "$1" == "debug" ]]; then
  # debug build
  $CPP $SRC $INCLUDE_DIRS $DEFINES $LIBS -o "$BUILD_DIR/$NAME" -g -fexceptions || exit 1
else
  # release build
  $CPP $SRC $INCLUDE_DIRS $DEFINES $LIBS -o "$BUILD_DIR/$NAME" -fexceptions -Os -flto || exit 1
  cp LICENSE "$BUILD_DIR/"
  [ -f "cacert.pem" ] && cp "cacert.pem" "$BUILD_DIR/"
fi

