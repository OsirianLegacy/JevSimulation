#pragma once
#include "Guid.h"
#include "Resource.h"
#include "Position.h"
#include "CreatureCatalog.h"
#include "Inventory.h"
#include "HumanName.h"
#include <optional>
#include <string>
#include <utility>

enum class ControlOwnership : std::uint32_t { Player, AI };

// Living-creature data. Type keys allow future component compositions without
// a subclass per species. Copies are snapshots of the same identity.
class Entity {
  public:
    explicit Entity(std::string type = "creature",
                    scene::ResourcePool health = scene::ResourcePool{},
                    scene::Position position = scene::Position{},
                    ControlOwnership control = ControlOwnership::AI);
    // Copy even from rvalues so no surviving instance loses its required Health.
    Entity(const Entity &) = default;
    Entity &operator=(const Entity &) = default;
    ControlOwnership control() const { return control_; }
    scene::Disposition disposition() const { return disposition_; }
    scene::SpeciesComponent species() const { return species_; }
    scene::VocationComponent vocation() const { return vocation_; }
    const Guid &id() const { return id_; }
    const std::optional<scene::HumanName> &humanName() const { return humanName_; }
    std::string displayName() const { return humanName_?humanName_->full():type_; }
    const std::string &type() const { return type_; }
    const scene::Resource &resources() const { return resources_; }
    // Read-only snapshot. SceneWorld owns and mutates the live inventory stacks.
    const std::vector<Item> &inventory() const { return inventory_; }
    const scene::Position &position() const { return position_; }
    scene::Position &position() { return position_; }
    const scene::ResourcePool &health() const {
        return *resources_.find(scene::Resource::Health);
    }
    void setResource(const std::string &key, scene::ResourcePool pool) {
        resources_.set(key, pool);
    }
    // Health is required even when depleted; other pools are optional.
    bool removeResource(const std::string &key);
    float adjustResource(const std::string &key, float delta) {
        return resources_.adjust(key, delta);
    }
    bool trySpendResource(const std::string &key, float amount) {
        return resources_.trySpend(key, amount);
    }
    static constexpr std::size_t maxTypeBytes = 255;
    bool operator==(const Entity &) const = default;

  private:
    friend class SceneWorld;
    Entity(Guid id, std::string type, scene::Resource resources, scene::Position position, ControlOwnership control,
           scene::SpeciesComponent species={}, scene::VocationComponent vocation={}, std::vector<Item> inventory={}, std::optional<scene::HumanName> name={}, scene::Disposition disposition=scene::Disposition::Neutral)
        : id_(id), type_(std::move(type)), resources_(std::move(resources)), position_(position), control_(control), species_(species), vocation_(vocation), inventory_(std::move(inventory)), humanName_(std::move(name)), disposition_(disposition) {}
    Guid id_;
    std::string type_;
    scene::Resource resources_;
    scene::Position position_;
    ControlOwnership control_ = ControlOwnership::AI;
    scene::SpeciesComponent species_;
    scene::VocationComponent vocation_;
    std::vector<Item> inventory_;
    std::optional<scene::HumanName> humanName_;
    scene::Disposition disposition_=scene::Disposition::Neutral;
};
