#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_set>

struct Guid {
    std::array<std::uint8_t,16> bytes{};
    bool empty() const { return *this == Guid{}; }
    bool operator==(const Guid&) const = default;
    static Guid generate(); // Compatibility wrapper for GenerateUniqueGuid().
    std::string toString() const;
};
struct GuidHash {
    std::size_t operator()(const Guid& id) const noexcept {
        std::size_t hash=0;
        for(auto byte:id.bytes) hash=hash*131+byte;
        return hash;
    }
};
class GuidRegistry {
public:
    // Candidate injection permits deterministic collision tests.
    Guid generate(const std::function<Guid()>& candidate);
    bool reserve(Guid id); // Idempotent when restoring an existing identity.
    bool hasUsed(Guid id) const;
private:
    mutable std::mutex mutex_;
    std::unordered_set<Guid,GuidHash> used_;
};
GuidRegistry& GlobalGuidRegistry();
// Generates, collision-checks, and reserves a UUID atomically across systems.
Guid GenerateUniqueGuid();
