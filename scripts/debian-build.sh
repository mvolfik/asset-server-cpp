#!/bin/sh

set -e

code=0
apt-get install -y --no-install-recommends build-essential cmake make libvips-dev libmagic-dev libboost-dev g++ git ca-certificates || code=$?

if [ "$code" -eq 100 ]; then
    echo "Failed to install dependencies, trying with sudo"
    code=0
    sudo apt-get install -y --no-install-recommends build-essential cmake make libvips-dev libmagic-dev libboost-dev g++ git ca-certificates || code=$?
fi
if [ "$code" -ne 0 ]; then
    echo "Failed to install dependencies"
    exit 1
fi

owd="$(pwd)"
tempdir="$(mktemp -d /tmp/asset-server-build-XXXXXX)"
srcdir="$(realpath "$(dirname "$0")/..")"

echo "Building the project in $tempdir"

cd "$tempdir"

cmake -DCMAKE_BUILD_TYPE=Release "$srcdir"
make -j 4 asset-server
cp asset-server "$owd" || { echo "Failed to copy asset-server binary"; exit 1; }

echo "Build complete. 'asset-server' binary was placed in your current directory."
