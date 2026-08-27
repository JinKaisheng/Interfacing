# Interfacing repository instructions

## Authoritative workspace

- The only active Interfacing checkout is on the server:
  `jinkaisheng@10.214.2.51:/home/jinkaisheng/InterfacingJin/Interfacing`.
- Perform project inspection, edits, builds, tests, and documentation updates
  in that server checkout. Use SSH from a local client when necessary.
- Do not use or recreate `C:\LocalCode\Interfacing`. The user has retired that
  local copy; it is not a source for synchronizing or overwriting server files.
- Report server paths and server verification results. If SSH is unavailable,
  report the blocker instead of falling back to a local checkout.

- The user's Chinese name must always be written exactly as 金凯胜.

## Completion contract

For any task that changes C++, CMake, YAML configuration, tests, plugin loading,
build outputs, command-line behavior, or VS Code configuration, use the
repository skill `interfacing-maintenance` before the final response.

- Reconcile `.vscode/tasks.json`, `.vscode/launch.json`, and `README.md` with
  the resulting behavior. Inspect all three every time, but edit only files
  affected by the change.
- Run the full repository verification for code, CMake, loader, configuration,
  test, output-path, or runtime-environment changes. Documentation-only changes
  need the full build only when they alter documented commands or paths.
- Preserve existing user changes. Do not commit, push, install dependencies,
  or synchronize another checkout unless the user explicitly requests it.
- In the final response, distinguish files changed from files inspected but
  unchanged, and report build, test, static smoke, and dynamic smoke results.

## Project invariants

- The public file-loading entry point is
  `LoadInterfaceWithModeFromConfig()`.
- Callers and tests use `LoadedInterface.mode` as the authoritative evidence of
  whether static or dynamic loading ran.
- Debug artifacts live under `build/debug`; advanced Release artifacts live
  under `build/advanced`.
