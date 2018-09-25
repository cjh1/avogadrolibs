#!/bin/bash
set -e

# Pull dockcross manylinux images
docker pull dockcross/manylinux-x64

# Generate dockcross scripts
docker run dockcross/manylinux-x64 > /tmp/dockcross-manylinux-x64
chmod u+x /tmp/dockcross-manylinux-x64

pushd .
curl -L https://cmake.org/files/v3.11/cmake-3.11.0-Linux-x86_64.sh -O
chmod u+x cmake-3.11.0-Linux-x86_64.sh
./cmake-3.11.0-Linux-x86_64.sh --prefix $HOME/cmake --skip-license
export PATH=$HOME/cmake/bin:$PATH
mkdir deps
cd deps
curl -L https://github.com/pybind/pybind11/archive/v2.2.4.tar.gz -O
tar zxvf v2.2.4.tar.gz
cd pybind11-2.2.4/
cmake -DPYBIND11_TEST:BOOL=FALSE -DCMAKE_INSTALL_PREFIX:PATH=$(pwd)/../pybind11 .
cmake --build . --target install
cd ../
curl -L http://bitbucket.org/eigen/eigen/get/3.3.5.tar.bz2 -O
bzip2 -d 3.3.5.tar.bz2
tar xvf 3.3.5.tar
mkdir eigen-build
cd eigen-build/
cmake -DCMAKE_INSTALL_PREFIX:PATH=$(pwd)/../eigen ../eigen-eigen-b3f3d4950030/
cmake --build . --target install
popd

# Build wheels
mkdir -p dist
cd avogadrolibs
DOCKER_ARGS="-v $HOME/dist:/work/dist/ -v $HOME/deps:/deps"
/tmp/dockcross-manylinux-x64 \
  -a "$DOCKER_ARGS" \
  "/work/scripts/circleci/manylinux-build-module-wheels.sh" "$@"
