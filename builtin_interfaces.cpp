#include "builtin_interfaces.h"

#include "impl_a/impl_a.h"
#include "impl_b/impl_b.h"
#include "interface_registry.h"

void RegisterBuiltInInterfaces(InterfaceRegistry& registry) {
    registry.Register<ImplA>("ImplA");
    registry.Register<ImplB>("ImplB");
}
