# Completion impact matrix

Use the smallest set of edits that keeps the development workflow truthful.

| Change | VS Code tasks | Debug launch | README | Full verify |
|---|---|---|---|---|
| Internal C++ refactor with unchanged behavior | Inspect | Inspect | Usually unchanged | Yes |
| CMake target, build directory, or output name | Update | Update if path/target is used | Update commands and outputs | Yes |
| CLI argument, YAML shape, or generated config name | Update run tasks | Update arguments | Update usage and examples | Yes |
| Loader mode, registry, plugin ABI, or version behavior | Inspect/update | Update relevant static/dynamic cases | Update architecture and behavior | Yes |
| Test suite or gtest name | Update test tasks if referenced | Update gtest filters | Update coverage summary | Yes |
| Runtime library path, debugger, or environment | Update | Update | Update environment guidance | Yes |
| Documentation wording only | No | No | Update requested text | Only if commands/paths changed |

Rules:

- “Inspect” means confirm the file remains correct; it does not require a
  content change.
- Do not hard-code a test count in automation; CTest discovers the current set.
- Keep one runnable static case and one runnable dynamic case in both task and
  debugger workflows.
- Treat `LoadedInterface.mode`, not log inference or implementation behavior,
  as the programmatic loading-mode proof.
