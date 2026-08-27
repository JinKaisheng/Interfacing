#!/usr/bin/env bash

set -euo pipefail

action="${1:-debug}"

readonly dependency_install_dir="install"
readonly -a required_dependency_files=(
    "${dependency_install_dir}/include/higgsIS/ClassLoader.h"
    "${dependency_install_dir}/include/higgsops/ConfigFactory.h"
    "${dependency_install_dir}/lib/libHiggsIS.so"
    "${dependency_install_dir}/lib/libHiggsOps.so"
)

check_dependencies() {
    local -a missing_files=()
    local required_file

    for required_file in "${required_dependency_files[@]}"; do
        if [[ ! -f "${required_file}" ]]; then
            missing_files+=("${required_file}")
        fi
    done

    if (( ${#missing_files[@]} > 0 )); then
        printf 'Interfacing dependencies are incomplete:\n' >&2
        printf '  missing: %s\n' "${missing_files[@]}" >&2
        printf '\nRun: bash compile.sh deps\n' >&2
        return 1
    fi

    printf 'Dependency preflight passed.\n'
}

remove_empty_package_cache() {
    local package_root="${dependency_install_dir}/packages"
    [[ -d "${package_root}" ]] || return 0
    [[ ! -L "${package_root}" ]] || {
        printf 'Refusing to repair symlinked package cache: %s\n' "${package_root}" >&2
        return 1
    }

    local package_dir
    for package_dir in "${package_root}"/*; do
        [[ -d "${package_dir}" ]] || continue

        # A downloaded NuGet package always contains at least a nuspec or
        # another regular file. Directories containing only empty subfolders
        # are interrupted downloads and make higgs_nuget.py skip re-download.
        if ! find "${package_dir}" \( -type f -o -type l \) -print -quit | grep -q .; then
            case "${package_dir}" in
                "${package_root}"/*)
                    printf 'Removing empty dependency cache: %s\n' "${package_dir}"
                    rm -rf -- "${package_dir}"
                    ;;
                *)
                    printf 'Refusing unexpected package path: %s\n' "${package_dir}" >&2
                    return 1
                    ;;
            esac
        fi
    done
}

remove_literal_wildcard_links() {
    local link_path
    local -a generated_link_candidates=(
        "${dependency_install_dir}/include/*"
        "${dependency_install_dir}/lib/*"
    )

    for link_path in "${generated_link_candidates[@]}"; do
        # An empty source directory can make the upstream installer create a
        # dangling link whose literal filename is '*'. Remove only that exact
        # generated artifact; normal dependency symlinks are left untouched.
        if [[ -L "${link_path}" && ! -e "${link_path}" ]]; then
            printf 'Removing invalid generated link: %s -> %s\n' \
                "${link_path}" "$(readlink "${link_path}")"
            unlink -- "${link_path}"
        fi
    done
}

install_dependencies() {
    mkdir -p "${dependency_install_dir}/packages"
    remove_empty_package_cache
    python py/configuration.py dependency.nuspec.in "${dependency_install_dir}/"
    remove_literal_wildcard_links
    check_dependencies
}

build_named_configuration() {
    local name="$1"
    local build_type="$2"
    local named_build_dir="build/${name}"
    local -a cmake_args=(
        -S .
        -B "${named_build_dir}"
        -G Ninja
        "-DCMAKE_BUILD_TYPE=${build_type}"
        -DINTERFACING_BUILD_TESTS=ON
    )

    check_dependencies
    cmake "${cmake_args[@]}"
    cmake --build "${named_build_dir}" -j "${BUILD_JOBS:-4}"
}

test_named_configuration() {
    local name="$1"

    check_dependencies
    ctest --test-dir "build/${name}" --output-on-failure
}

case "${action}" in
    deps)
        install_dependencies
        ;;
    check-deps)
        check_dependencies
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
        echo "Usage: bash compile.sh {deps|check-deps|debug|debug-test|advanced|advanced-test}" >&2
        exit 2
        ;;
esac
