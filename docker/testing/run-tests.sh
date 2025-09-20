#!/bin/sh -e

# This is a simple shell script which can be used to run all the
# tests in the Docker testing container once the image has been built.

cd /usr/src/taurion
. venv/bin/activate
make check -j${N}
