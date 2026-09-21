#pragma once
#include "Grid.h"
#include "Resource.h"
#include "Entity.h"
#include <map>
#include <unordered_map>
struct CreatureRecord {
    Guid id;
    std::string type;
    ControlOwnership control = ControlOwnership::AI;
    scene::Position position;
    bool operator==(const CreatureRecord &) const = default;
};
struct GuidState {
    std::map<std::string, Guid> items, types;
    std::unordered_map<Guid, std::vector<Guid>, GuidHash> contents;
    std::unordered_set<Guid, GuidHash> used;
};

// ECS-free serialization/snapshot data. Import helpers only construct validated records;
// gameplay, navigation, editing, and runtime ownership belong to SceneWorld.
class WorldDocument {
  public:
    explicit WorldDocument(int width = 2048, int height = 2048, float cellSize = 8, Vector2 origin = {});
    int width() const {
        return width_;
    }
    int height() const {
        return height_;
    }
    float cellSize() const {
        return cellSize_;
    }
    std::size_t count() const {
        return cells_.size();
    }
    bool contains(CellPosition) const;
    std::size_t index(CellPosition) const;
    CellPosition coordinates(std::size_t) const;
    const GridCell &at(CellPosition) const;
    const GridCell *tryGet(CellPosition) const;
    Rectangle worldBounds() const;
    void paint(CellPosition, GridLayer, TileRef);
    Guid placeObject(CellPosition, TileRef, ObjectType);
    Guid placeObject(CellPosition, TileRef, const std::string &);
    void restoreObject(CellPosition, TileRef, Guid, ObjectType, bool, std::vector<Item>);
    void bindObjectType(Guid, const std::string &);
    const Object *findObject(Guid) const;
    const Object *objectAt(CellPosition) const;
    std::size_t objectCount() const {
        return objects_.size();
    }
    void createItemDefinition(ItemDefinition);
    void createObjectType(ObjectTypeDefinition);
    const auto &itemDefinitions() const {
        return itemDefinitions_;
    }
    const auto &objectTypes() const {
        return objectTypes_;
    }
    std::vector<CreatureRecord> creatures;
    // Optional extension records, keyed by persistent instance identity.
    std::unordered_map<Guid, scene::Resource, GuidHash> resources;
    GuidState guidState() const;
    void validateGuidState(const GuidState &) const;
    void restoreGuidState(GuidState);
    static constexpr std::size_t maxDefinitions = 4096, maxGuidHistory = 4'000'000;

  private:
    friend class SceneWorld;
    int width_, height_;
    float cellSize_;
    Vector2 origin_;
    std::vector<GridCell> cells_;
    std::unordered_map<Guid, Object, GuidHash> objects_;
    std::map<std::string, ItemDefinition> itemDefinitions_;
    std::map<std::string, ObjectTypeDefinition> objectTypes_;
    std::unordered_set<Guid, GuidHash> usedGuids_;
    Guid allocateGuid();
};
