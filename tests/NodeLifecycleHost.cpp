#include "NodeProcess.h"
#include <windows.h>
#include "NodeConfig.h"
#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--stdio-test") {
            // IDEs can supply usable handles that are not inheritable by children.
            for (const DWORD stream : {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
                if (!SetHandleInformation(GetStdHandle(stream), HANDLE_FLAG_INHERIT, 0)) {
                    throw std::runtime_error("Cannot configure non-inheritable test handles.");
                }
            }
            NodeProcess node(JevNodeExecutable, {L"-e",
                L"console.log('NODE_STDOUT_VISIBLE'); console.error('NODE_STDERR_VISIBLE'); process.exitCode = 7;"},
                JevProjectDirectory);
            const int result = node.wait();
            if (node.poll() != result) throw std::runtime_error("Poll lost the completed exit code.");
            return result;
        }
        NodeProcess node(JevNodeExecutable,
            {L"-e", L"setInterval(() => {}, 1000)"}, JevProjectDirectory);
        if (node.poll()) throw std::runtime_error("Running Node unexpectedly reported completion.");
        std::cout << node.id() << std::endl;
        // The test controller either closes stdin or forcibly kills this host.
        std::cin.get();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
