#include "interface_loader.h"

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <config.yaml>" << std::endl;
        return 1;
    }

    try {
        LoadedInterface loaded =
            LoadInterfaceWithModeFromConfig(argv[1]);

        // Mode is reported by the dispatcher, not guessed from implementation
        // behavior. The same ImplA class can be created by either path.
        std::cout << "Loaded " << loaded.class_name << " via "
                  << ToString(loaded.mode) << " mode" << std::endl;
        std::cout << "Loaded interface version "
                  << loaded.instance->GetVersion() << std::endl;
        loaded.instance->print();
        loaded.instance->foo();
        loaded.instance->bar();
        return 0;
    } catch (const HiggsIS::Exception& error) {
        std::cerr << "Failed to load implementation: "
                  << error.GetMessage() << std::endl;
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Failed to load implementation: "
                  << error.what() << std::endl;
        return 2;
    }
}
