#!/usr/bin/env bash

set -euo pipefail

action="${1:-all}"
build_dir="build/server"

install_dependencies() {
    python py/configuration.py dependency.nuspec.in install/
}

build_project() {
    cmake -S . -B "${build_dir}" -G Ninja
    cmake --build "${build_dir}" -j "${BUILD_JOBS:-4}"
}

test_project() {
    ctest --test-dir "${build_dir}" --output-on-failure
}

case "${action}" in
    deps)
        install_dependencies
        ;;
    build)
        build_project
        ;;
    test)
        test_project
        ;;
    all)
        install_dependencies
        build_project
        test_project
        ;;
    *)
        echo "Usage: bash compile.sh {deps|build|test|all}" >&2
        exit 2
        ;;
esac