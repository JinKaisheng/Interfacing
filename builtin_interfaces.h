#ifndef INTERFACING_BUILTIN_INTERFACES_H
#define INTERFACING_BUILTIN_INTERFACES_H

class InterfaceRegistry;

// Keep concrete implementation knowledge out of main.cpp and the generic
// loader. This translation unit is the single composition root for built-ins.
void RegisterBuiltInInterfaces(InterfaceRegistry& registry);

#endif  // INTERFACING_BUILTIN_INTERFACES_H
