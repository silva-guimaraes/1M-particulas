#!/usr/bin/sh

set -euxo pipefail

g++ main.cpp -o particle-simulator -lSDL2

