#pragma once
#include "Guid.h"
#include "Resource.h"
#include "Position.h"
#include <string>

// Living-creature data. Type keys allow future component compositions without
// a subclass per species. Copies are snapshots of the same identity.
class Entity {
  public:
    explicit Entity(std::string type = "creature",
                    scene::ResourcePool health = scene::ResourcePool{},
                    scene::Position position = scene::Position{});
    // Copy even from rvalues so no surviving instance loses its required Health.
    Entity(const Entity &) = default;
    Entity &operator=(const Entity &) = default;
    const Guid &id() const { return id_; }
    const std::string &type() const { return type_; }
    const scene::Resource &resources() const { return resources_; }
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
    Guid id_;
    std::string type_;
    scene::Resource resources_;
    scene::Position position_;
};
