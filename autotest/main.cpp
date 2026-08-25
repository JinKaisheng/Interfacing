#include "interface_loader.h"

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <config.yaml>" << std::endl;
        return 1;
    }

    try {
        std::unique_ptr<Interface> instance =
            LoadInterfaceFromConfig(argv[1]);

        std::cout << "Loaded interface version "
                  << instance->GetVersion() << std::endl;
        instance->print();
        instance->foo();
        instance->bar();
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