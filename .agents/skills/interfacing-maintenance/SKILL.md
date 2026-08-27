---
name: interfacing-maintenance
description: Finish Interfacing repository changes by reconciling VS Code build/debug tasks and user documentation, then verifying the C++ static/dynamic loader paths. Use after implementation changes; do not trigger for explanation-only or read-only requests.
---

# Interfacing maintenance

Use this skill at the end of an implementation task in the Interfacing
repository.

## Reconcile maintained artifacts

Inspect the completed diff and read
[references/completion-matrix.md](references/completion-matrix.md). Check all
of the following, but change only what the implementation made stale:

- `.vscode/tasks.json` for configure/build/test commands, target names, build
  directories, and generated paths.
- `.vscode/launch.json` for executable paths, arguments, working directory,
  debugger, shared-library search paths, and static/dynamic test filters.
- `README.md` for user-visible architecture, prerequisites, commands,
  configuration formats, output paths, test coverage, and runtime behavior.

Do not rewrite documentation for implementation-only refactoring. Never discard
unrelated user edits.

## Verify

For C++, CMake, loader, YAML, test, build-output, CLI, or runtime-environment
changes, run from the repository root:

```bash
bash .agents/skills/interfacing-maintenance/scripts/verify.sh
```

The script deliberately does not restore dependencies. If dependencies are
missing, report the prerequisite and use `bash compile.sh deps` only when the
current task authorizes dependency installation.

For documentation-only work, run the full script only if documented commands,
paths, configuration, or behavior changed. Otherwise validate the affected
files directly.

## Preserve boundaries

Operate only in the active checkout. Do not SSH, synchronize another checkout,
commit, or push unless the current user request explicitly includes that
action. Do not turn a successful local build into permission to modify a
server.

## Report

Before finishing, report:

- files changed;
- maintained files inspected but unchanged;
- Debug and advanced/Release build and CTest results when run;
- static and dynamic smoke-test modes when run;
- skipped checks and the reason;
- remaining risks or prerequisites.
