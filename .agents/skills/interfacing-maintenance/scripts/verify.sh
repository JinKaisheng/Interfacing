#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../../.." && pwd)"
cd "${repo_root}"

python -m json.tool .vscode/tasks.json >/dev/null
python -m json.tool .vscode/launch.json >/dev/null

bash compile.sh debug
bash compile.sh debug-test
bash compile.sh advanced
bash compile.sh advanced-test

export LD_LIBRARY_PATH="${repo_root}/install/lib:${repo_root}/build/debug/lib:${LD_LIBRARY_PATH:-}"

static_output="$(build/debug/bin/main.out build/debug/config/impl_a_static.yaml)"
printf '%s\n' "${static_output}"
grep -Fq "Loaded ImplA via static mode" <<<"${static_output}"

dynamic_output="$(build/debug/bin/main.out build/debug/config/impl_a_dynamic.yaml)"
printf '%s\n' "${dynamic_output}"
grep -Fq "Loaded ImplA via dynamic mode" <<<"${dynamic_output}"

deprecated_loader='LoadInterface''FromConfig'
if grep -RIn \
    --exclude-dir=.git \
    --exclude-dir=.agents \
    --exclude-dir=build \
    --exclude=AGENTS.md \
    "${deprecated_loader}" .; then
    printf 'Deprecated loader API reference found.\n' >&2
    exit 1
fi

git diff --check
printf 'Interfacing verification completed successfully.\n'
