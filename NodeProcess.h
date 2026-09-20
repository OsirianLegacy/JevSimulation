#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

// Own this object around the game loop. Its private Windows Job Object
// terminates the child on normal shutdown and forced application termination.
class NodeProcess {
public:
    NodeProcess(const std::wstring& executable,
                const std::vector<std::wstring>& arguments,
                const std::wstring& workingDirectory);
    ~NodeProcess();
    NodeProcess(const NodeProcess&) = delete;
    NodeProcess& operator=(const NodeProcess&) = delete;
    unsigned long id() const;
    int wait() const;
    std::optional<int> poll() const; // Empty while Node is running; never blocks.

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
