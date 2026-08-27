#include <higgsIS/ClassLoader.h>

// This fixed C symbol belongs only in the shared plugin. Keeping it out of the
// static archive avoids duplicate HCL_DynamicLibVersion definitions when A
// and B are both linked into main.out.
// CMake supplies this plugin product version and uses the same value in YAML.
// It is independent of the Interface contract returned by ImplA::GetVersion().
HCL_SO_VERSION(INTERFACING_IMPL_A_PLUGIN_VERSION)
