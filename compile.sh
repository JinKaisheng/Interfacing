#!/usr/bin/env bash

set -euo pipefail

action="${1:-debug}"

install_dependencies() {
    python py/configuration.py dependency.nuspec.in install/
}

build_named_configuration() {
    local name="$1"
    local build_type="$2"
    local named_build_dir="build/${name}"

    cmake -S . -B "${named_build_dir}" -G Ninja \
        -DCMAKE_BUILD_TYPE="${build_type}" \
        -DINTERFACING_BUILD_TESTS=ON
    cmake --build "${named_build_dir}" -j "${BUILD_JOBS:-4}"
}

test_named_configuration() {
    local name="$1"
    ctest --test-dir "build/${name}" --output-on-failure
}

case "${action}" in
    deps)
        install_dependencies
        ;;
    debug)
        build_named_configuration debug Debug
        ;;
    debug-test)
        test_named_configuration debug
        ;;
    advanced)
        build_named_configuration advanced Release
        ;;
    advanced-test)
        test_named_configuration advanced
        ;;
    *)
        echo "Usage: bash compile.sh {deps|debug|debug-test|advanced|advanced-test}" >&2
        exit 2
        ;;
esac
