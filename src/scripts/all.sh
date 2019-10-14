#!/usr/bin/env bash
set -ex

ROOTDIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd )"


function build() {
    #NICE="nice -n20"
    NICE="schedtool -5 -e"

    ORIGINDIR=$(pwd)

    COMPILER=$1
    CXX_STANDARD=$2
    SANITIZER=$3
    CXXFLAGS=$4

    DIRNAME=${COMPILER}_cxx${CXX_STANDARD}_sanitizer${SANITIZER}_${CXXFLAGS}
    
    rm -Rf ${DIRNAME}
    mkdir -p ${DIRNAME}
    cd ${DIRNAME}

    CXX=$(which ${COMPILER}) cmake -G Ninja -DCMAKE_CXX_FLAGS=${CXXFLAGS} -DCMAKE_BUILD_TYPE=Debug -DNB_cxx_standard=${CXX_STANDARD} -DNB_sanitizer=${SANITIZER} ${ROOTDIR}
    ${NICE} cmake --build .
    ${NICE} ./nb

    cd ${ORIGINDIR}
}

build "clang++" "11" "OFF" "-m32"

#build "g++-4.9" "11" "OFF" "-m32"
#build "g++-4.9" "14" "OFF" "-m32"

#build "g++-5" "11" "OFF" "-m32"
#build "g++-5" "14" "OFF" "-m32"
#build "g++-5" "17" "OFF" "-m32"

#build "g++-6" "11" "OFF" "-m32"
#build "g++-6" "14" "OFF" "-m32"
#build "g++-6" "17" "OFF" "-m32"

#build "g++-7" "11" "OFF" "-m32"
#build "g++-7" "14" "OFF" "-m32"
#build "g++-7" "17" "OFF" "-m32"

build "g++" "11" "ON" "-m32"
build "g++" "14" "ON" "-m32"
build "g++" "17" "ON" "-m32"
build "g++" "20" "ON" "-m32"

build "clang++-6" "11" "OFF" "-m32"
build "clang++-6" "14" "OFF" "-m32"
build "clang++-6" "17" "OFF" "-m32"
build "clang++-6" "20" "OFF" "-m32"

build "clang++" "11" "OFF" "-m32"
#build "clang++" "11" "ON" "-m32"
#build "clang++" "17" "ON" "-m32"
build "clang++" "17" "OFF" "-m32"
#build "clang++" "20" "ON" "-m32"
build "clang++" "20" "OFF" "-m32"


# all the rest
#build "g++-4.9" "11" "OFF"
#build "g++-4.9" "14" "OFF"

build "g++-5" "11" "OFF"
build "g++-5" "14" "OFF"
build "g++-5" "17" "OFF"

build "g++-6" "11" "OFF"
build "g++-6" "14" "OFF"
build "g++-6" "17" "OFF"

build "g++-7" "11" "OFF"
build "g++-7" "14" "OFF"
build "g++-7" "17" "OFF"

build "g++" "11" "ON"
build "g++" "14" "ON"
build "g++" "17" "ON"
build "g++" "20" "ON"

build "clang++-6" "11" "OFF"
build "clang++-6" "14" "OFF"
build "clang++-6" "17" "OFF"
build "clang++-6" "20" "OFF"

build "clang++" "11" "OFF"
build "clang++" "11" "ON"
build "clang++" "14" "ON"
build "clang++" "17" "ON"
build "clang++" "17" "OFF"
build "clang++" "20" "ON"
