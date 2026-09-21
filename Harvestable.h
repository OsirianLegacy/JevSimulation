#pragma once
#include "Object.h"
#include "Grid.h"
#include "Resource.h"
#include "third_party/nlohmann/json.hpp"
#include <map>

namespace scene {
struct HarvestableDefinition {
    std::string name, action = "chop";
    float workSeconds = 2;
    std::uint32_t units = 5;
    std::vector<Item> yields; // Recipes, never owned stacks; GUIDs must be empty.
    std::string requiredTool; // Optional item definition ID, presence is sufficient.
    std::string depletedState = "Depleted";
    bool removeWhenDepleted = false;
    double regenerationSeconds = 0;
    TileRef depletedTile; // Optional replacement artwork; otherwise keep the source tile.
    void validate() const;
    nlohmann::json json() const;
    static HarvestableDefinition fromJson(const nlohmann::json &);
    bool operator==(const HarvestableDefinition &) const = default;
};
struct Harvestable {
    std::string definitionId;
    double regenerationRemaining = 0;
    TileRef sourceTile;
    static constexpr const char *Units = Resource::HarvestUnits;
    static constexpr const char *RulesDescription =
        "Work adjacent to reduce health. Every 25% lost scatters random yields on free walkable cells. "
        "Adjacent units collect loose drops automatically; players have priority. Stored contents are separate.";
    bool operator==(const Harvestable &) const = default;
};
struct HarvestCheck {
    bool allowed = false;
    std::string reason;
};
std::map<std::string, HarvestableDefinition> defaultHarvestables();
void validateHarvestPool(const ResourcePool &);
}
