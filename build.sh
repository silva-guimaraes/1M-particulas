#!/usr/bin/sh

set -euxo pipefail

g++ main.cpp -o c -lSDL2
./c

