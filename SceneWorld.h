#pragma once
#include "WorldDocument.h"
#include <memory>
class EntityPresentation;
class SceneWorld {
  public:
    explicit SceneWorld(int width = 2048, int height = 2048, float cellSize = 8.0f, Vector2 origin = {});
    static constexpr int chunkSize = 32;
    CellPosition chunkOf(CellPosition cell) const;
    std::vector<Guid> creaturesInChunk(CellPosition chunk) const;
    std::vector<Guid> objectsInChunk(CellPosition chunk) const;
    std::vector<Guid> creatureIds() const;
    std::uint64_t creatureMembershipRevision() const;
    const scene::CreatureCatalog& creatureCatalog() const;
    void setCreatureCatalog(scene::CreatureCatalog catalog);
    void setCreatureIdentity(Guid id, scene::SpeciesComponent species, scene::VocationComponent vocation);
    void setCreatureDisposition(Guid id, scene::Disposition disposition);
    // Shared melee rules for player orders, local AI and Jev selections.
    static constexpr float attackDamage=10;
    static constexpr double attackSeconds=1;
    struct AttackCheck { bool allowed=false; std::string reason; };
    AttackCheck canAttack(Guid actor, Guid target, bool requireAdjacent=true) const;
    AttackCheck attack(Guid actor, Guid target);
    bool isEnemy(Guid actor, Guid target) const;
    std::vector<CellPosition> attackPath(Guid actor, Guid target) const;
    std::vector<Guid> nearbyCreatures(Guid actor, int radius) const;
    Guid spawnCreature(CellPosition cell, scene::SpeciesComponent species, scene::VocationComponent vocation, ControlOwnership control);
    Guid spawnCreature(CellPosition cell, std::string type = "creature",
                       ControlOwnership control = ControlOwnership::AI,
                       scene::ResourcePool health = scene::ResourcePool{});
    std::optional<Entity> findCreature(Guid id) const;
    std::optional<Guid> creatureAt(CellPosition cell) const;
    bool moveCreature(Guid id, CellPosition destination);
    void removeCreature(Guid id);
    std::vector<CellPosition> creaturePath(Guid id, CellPosition destination, std::size_t maxNodes = 4096) const;
    std::vector<std::pair<CellPosition, int>> reachableCells(Guid id, int radius) const;
    int width() const;
    int height() const;
    float cellSize() const;
    std::size_t count() const;
    bool contains(CellPosition cell) const;
    std::size_t index(CellPosition cell) const; // Throws outside grid.
    CellPosition coordinates(std::size_t index) const;
    const GridCell &at(CellPosition cell) const;
    const GridCell *tryGet(CellPosition cell) const;
    void set(CellPosition cell, GridCell value);
    void fill(GridCell value);
    void clear();
    void fillRegion(CellRange region, GridCell value); // Clips to grid bounds.
    bool isWalkable(CellPosition cell) const;          // False outside grid.
    bool blocksSight(CellPosition cell) const;         // True outside grid.
    void paint(CellPosition cell, GridLayer layer, TileRef tile);
    void erase(CellPosition cell, GridLayer layer);
    Guid placeObject(CellPosition cell, TileRef tile, ObjectType type);
    Guid placeObject(CellPosition cell, TileRef tile, const std::string &definitionId);
    void bindObjectType(Guid object, const std::string &definitionId);
    void createItemDefinition(ItemDefinition definition);
    void createObjectType(ObjectTypeDefinition definition);
    std::map<std::string, ItemDefinition> itemDefinitions() const;
    std::map<std::string, ObjectTypeDefinition> objectTypes() const;
    static constexpr std::size_t maxDefinitions = 4096;
    static constexpr std::size_t maxGuidHistory = 4'000'000;
    bool hasUsedGuid(Guid id) const;
    GuidState guidState() const;
    // Deserialization-only: validates a complete identity table before applying it.
    void restoreGuidState(GuidState state);
    std::optional<Object> findObject(Guid id) const;
    std::optional<Object> objectAt(CellPosition cell) const;
    std::optional<CellPosition> objectPosition(Guid id) const;
    bool moveObjectById(Guid id, CellPosition destination);
    void removeObjectById(Guid id);
    bool moveObject(CellPosition from, CellPosition to);
    void removeObject(CellPosition cell);
    void updateObject(Guid id, bool open, std::vector<Item> contents);
    std::optional<scene::Resource> resources(Guid id) const;
    std::optional<scene::ResourcePool> resource(Guid id, const std::string &key) const;
    void setResource(Guid id, const std::string &key, scene::ResourcePool pool);
    void removeResource(Guid id, const std::string &key);
    float adjustResource(Guid id, const std::string &key, float delta);
    bool trySpendResource(Guid id, const std::string &key, float amount);
    std::optional<scene::Harvestable> harvestable(Guid id) const;
    void setHarvestable(Guid id, const std::string &definitionId); // Explicit assignment/refill.
    void removeHarvestable(Guid id);
    scene::HarvestCheck canHarvest(Guid actor, Guid target, bool requireAdjacent = true) const;
    scene::HarvestCheck completeHarvest(Guid actor, Guid target);
    std::vector<CellPosition> harvestPath(Guid actor, Guid target) const;
    std::vector<Guid> nearbyHarvestables(Guid actor, int radius) const;
    // Converts a depleted creature with a configured carcass definition, preserving carried stacks.
    std::optional<Guid> createCarcass(Guid creature);
    void setObjectOpen(Guid id, bool open);
    // Creatures, containers and gatherables own inventories. Missing owners/doors return nullopt.
    std::optional<std::vector<Item>> inventory(Guid id) const;
    void updateInventory(Guid id, std::vector<Item> items);
    // Add allocates a new stack identity; update preserves it. Stacks do not auto-merge.
    void addItem(Guid id, Item item);
    void updateItem(Guid id, std::size_t index, Item item);
    void removeItem(Guid id, std::size_t index);
    std::size_t objectCount() const;
    // Validated deserialization entry point; rejects occupied cells and duplicate IDs.
    void restoreObject(CellPosition cell, TileRef tile, Guid id, ObjectType type, bool open,
                       std::vector<Item> contents);
    std::vector<CellPosition> neighbors(CellPosition cell, bool diagonals = false,
                                        bool walkableOnly = false) const;
    // Paths include both endpoints; empty means invalid, blocked, or unreachable.
    // Diagonal paths never cut across blocked corners.
    std::vector<CellPosition> aStar(CellPosition start, CellPosition goal, bool diagonals = false) const;
    std::vector<CellPosition> breadthFirstSearch(CellPosition start, CellPosition goal,
                                                 bool diagonals = false) const;
    // Cell-center supercover LOS: Walls/Entities/closed Doors (including touched corners) occlude.
    bool hasLineOfSight(CellPosition start, CellPosition goal, bool allowOccupiedEndpoints=false) const;
    std::optional<CellPosition> worldToCell(Vector2 world) const;
    Vector2 cellToWorld(CellPosition cell) const; // Top-left corner.
    Vector2 cellCenter(CellPosition cell) const;
    Rectangle cellBounds(CellPosition cell) const;
    Rectangle worldBounds() const;
    CellRange visibleRange(Rectangle worldView) const;
    void draw(Rectangle worldView, const TilesetLibrary *tilesets = nullptr, const EntityPresentation *presentation = nullptr) const;

    ~SceneWorld();
    SceneWorld(SceneWorld &&) noexcept;
    SceneWorld &operator=(SceneWorld &&) noexcept;
    SceneWorld(const SceneWorld &) = delete;
    SceneWorld &operator=(const SceneWorld &) = delete;
    WorldDocument document() const;
    static SceneWorld fromDocument(const WorldDocument &document);
    void setHistoryEnabled(bool enabled);
    void beginEdit();
    void endEdit();
    void cancelEdit();
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    void clearHistory();
    void markSaved();
    bool dirty() const;
    void tick();
    std::uint64_t ticks() const;
    double simulationTime() const;
    std::size_t persistentEntityCount() const;
    bool editableField(Guid id, const std::string &field) const;
    std::vector<std::string> componentNames(Guid id) const;
    void reserveHistory(const std::unordered_set<Guid, GuidHash> &history);

  private:
    struct Impl;
    struct Operation;
    std::unique_ptr<Impl> impl_;
    void touchCreature(Guid id);
    void installCreature(const CreatureRecord &record, const scene::Resource &resources);
    void installInventory(Guid owner, const std::vector<Item> &items);
    void eraseCreature(Guid id);
    void touch(CellPosition cell);
    void touchResources(Guid id);
    void touchHarvestable(Guid id);
    void advanceHarvestLifecycle(double seconds);
    void collectGatherables();
    void install(CellPosition cell, const Object &object, TileRef tile);
    void eraseInstance(Guid id);
    void applyHistory(bool forward);
};
