#include "interface.h"

// This fixed C symbol belongs only in the shared plugin. Keeping it out of the
// static archive avoids duplicate HCL_DynamicLibVersion definitions when A
// and B are both linked into main.out.
HCL_SO_VERSION(INTERFACE_VERSION)
