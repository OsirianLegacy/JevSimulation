#pragma once
#include "Resource.h"
#include "Harvestable.h"
#include "Grid.h"
#include "Disposition.h"
#include "third_party/nlohmann/json.hpp"
#include <cstdint>
#include <map>

namespace scene {
// Named built-ins; editor-created entries use stable additional numeric values.
enum class Species : std::uint32_t { Human = 0 };
enum class SubSpecies : std::uint32_t { Default = 0 };
struct SpeciesComponent {
    Species species = Species::Human;
    SubSpecies subSpecies = SubSpecies::Default;
    bool operator==(const SpeciesComponent&) const = default;
};
struct VocationComponent {
    std::uint32_t id = 0; // Unassigned is always available.
    bool operator==(const VocationComponent&) const = default;
};
struct SpriteFrames {
    int column=3, aiRow=4, playerRow=8, count=3;
    float fps=3;
    bool operator==(const SpriteFrames&) const = default;
};
struct SpeciesDefinition {
    std::string name="Human", sheet="Units.png";
    std::map<std::uint32_t,std::string> subSpecies{{0,"Default"}};
    std::map<std::string,float> resources{{Resource::Health,100}, {Resource::Hunger,100},
                                          {Resource::Thirst,100}, {Resource::Sleep,100}};
    SpriteFrames idle;
    std::string carcassHarvest; // Empty preserves the legacy health-only lifecycle.
    TileRef carcassTile{0,0,0};
    Disposition defaultDisposition=Disposition::Neutral;
    bool operator==(const SpeciesDefinition&) const = default;
};
struct VocationDefinition {
    std::string name;
    // Membership is permission; each species can choose its own frame mapping.
    std::map<std::uint32_t,SpriteFrames> species;
    bool operator==(const VocationDefinition&) const = default;
};
struct CreatureCatalog {
    std::map<std::uint32_t,SpeciesDefinition> species{{0,{}}};
    std::map<std::uint32_t,VocationDefinition> vocations;
    std::map<std::string,HarvestableDefinition> harvestables = defaultHarvestables();
    void validate() const;
    bool permits(SpeciesComponent species, VocationComponent vocation) const;
    const SpriteFrames& frames(SpeciesComponent species, VocationComponent vocation) const;
    nlohmann::json json() const;
    static CreatureCatalog fromJson(const nlohmann::json& json);
    bool operator==(const CreatureCatalog&) const = default;
};
}
