#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <raylib.h>

namespace scene {
// A zero maximum is a valid disabled/empty pool. Values always remain in [0, maximum].
class ResourcePool {
  public:
    explicit ResourcePool(float maximum = 100) : ResourcePool(maximum, maximum) {}
    ResourcePool(float maximum, float current) {
        validate(maximum);
        validate(current);
        if (current > maximum)
            throw std::invalid_argument("Resource current exceeds maximum.");
        maximum_ = maximum;
        current_ = current;
    }
    float current() const {
        return current_;
    }
    float maximum() const {
        return maximum_;
    }
    float fraction() const {
        return maximum_ > 0 ? current_ / maximum_ : 0;
    }
    bool depleted() const {
        return current_ == 0;
    }
    bool full() const {
        return current_ == maximum_;
    }
    void setCurrent(float value) {
        validate(value);
        current_ = std::min(value, maximum_);
    }
    void setMaximum(float value) {
        validate(value);
        maximum_ = value;
        current_ = std::min(current_, value);
    }
    void refill() {
        current_ = maximum_;
    }
    void empty() {
        current_ = 0;
    }
    // Returns the actual signed change. Double arithmetic avoids finite float overflow.
    float adjust(float delta) {
        if (!std::isfinite(delta))
            throw std::invalid_argument("Resource change must be finite.");
        const float previous = current_;
        current_ = static_cast<float>(
            std::clamp(static_cast<double>(current_) + delta, 0.0, static_cast<double>(maximum_)));
        return current_ - previous;
    }
    bool trySpend(float amount) {
        validate(amount);
        if (amount > current_)
            return false;
        current_ -= amount;
        return true;
    }
    bool operator==(const ResourcePool &) const = default;

  private:
    float current_ = 100, maximum_ = 100;
    static void validate(float value) {
        if (!std::isfinite(value) || value < 0)
            throw std::invalid_argument("Resource values must be finite and nonnegative.");
    }
};
// One component supports simultaneous, independently named pools on any entity.
// Keys are extensible (health, stamina, mana, shield, fuel, durability, ...).
class Resource {
  public:
    // Borrow the UI atlas for this draw only; components retain no GPU state.
    // Draws the named pool's fill, then its frame, then centered current amount.
    void draw(const std::string &key, Texture2D atlas, Rectangle bounds) const;
    static Color drawColor(const std::string &key);
    static constexpr const char *Health = "health";
    static constexpr const char *Stamina = "stamina";
    static constexpr const char *Mana = "mana";
    static constexpr const char *Hunger = "hunger";
    static constexpr const char *Thirst = "thirst";
    static constexpr const char *Sleep = "sleep";
    static constexpr const char *HarvestUnits = "harvestUnits";
    // Fraction of capacity lost per simulation second; full reserves last 10/5/15 minutes.
    static constexpr float HungerDuration = 600, ThirstDuration = 300, SleepDuration = 900;
    // Only present need pools are advanced. SceneWorld calls this for living creatures.
    void advanceNeeds(float seconds);
    // Shared rules describe implemented behavior, not future gameplay mechanics.
    static constexpr const char *RulesDescription =
        "Named pools track available amounts and capacities. Values stay between zero and maximum. "
        "Spending requires the full amount; adjustments clamp to bounds. Creature needs drain during simulation. "
        "No automatic regeneration or depletion effects.";
    static constexpr const char *HealthRulesDescription =
        "Remaining vitality. Depleted creatures cannot act; configured species become carcasses during simulation.";
    static constexpr const char *StaminaRulesDescription =
        "Pool reserved for stamina. No action costs are configured.";
    static constexpr const char *ManaRulesDescription =
        "Pool reserved for mana. No spell costs are configured.";
    static constexpr const char *CustomRulesDescription =
        "Custom resource pool; gameplay purpose is unspecified.";
    static const char *rulesDescription(const std::string &key) {
        if (key == Health) return HealthRulesDescription;
        if (key == Stamina) return StaminaRulesDescription;
        if (key == Mana) return ManaRulesDescription;
        if (key == Hunger) return "Food reserve: full means fed, zero means hungry. Creatures lose 1/600 of capacity per simulation second.";
        if (key == Thirst) return "Water reserve: full means hydrated, zero means thirsty. Creatures lose 1/300 of capacity per simulation second.";
        if (key == Sleep) return "Rest reserve: full means rested, zero means exhausted. Creatures lose 1/900 of capacity per simulation second.";
        if (key == HarvestUnits) return "Remaining whole harvests. Each successful harvest spends one unit and produces the configured yields.";
        return CustomRulesDescription;
    }
    static constexpr std::size_t maxPools = 64, maxKeyBytes = 64;
    // Read-only JSON snapshot with rulesDescription strings for the component and each pool.
    // Keys must be valid UTF-8; malformed text throws rather than emitting invalid JSON.
    std::string fetchData() const;
    const auto &pools() const {
        return pools_;
    }
    const ResourcePool *find(const std::string &key) const {
        const auto it = pools_.find(key);
        return it == pools_.end() ? nullptr : &it->second;
    }
    void set(const std::string &key, ResourcePool pool) {
        if (key.empty() || key.size() > maxKeyBytes || key.find('\0') != std::string::npos ||
            key.find_first_not_of(" \t\r\n") == std::string::npos)
            throw std::invalid_argument("Resource key must be nonblank and at most 64 bytes.");
        if (!pools_.contains(key) && pools_.size() >= maxPools)
            throw std::length_error("Too many resource pools.");
        pools_.insert_or_assign(key, pool);
    }
    bool remove(const std::string &key) {
        return pools_.erase(key) != 0;
    }
    float adjust(const std::string &key, float delta) {
        return pools_.at(key).adjust(delta);
    }
    bool trySpend(const std::string &key, float amount) {
        return pools_.at(key).trySpend(amount);
    }
    bool operator==(const Resource &) const = default;

  private:
    std::map<std::string, ResourcePool> pools_;
};
} // namespace scene
