#include "SceneWorld.h"
#include "SceneComponents.h"
#include "TilesetLibrary.h"
#include "EntityPresentation.h"
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>
#include <queue>
#include <set>
#include <random>

using namespace scene;
namespace {
std::mt19937 &harvestRandom() { static thread_local std::mt19937 rng(std::random_device{}()); return rng; }
struct CellSnapshot {
    GridCell cell;
    std::optional<Object> object;
    bool operator==(const CellSnapshot &) const = default;
};
struct Delta {
    std::unordered_map<Guid, std::optional<Harvestable>, GuidHash> harvestBefore, harvestAfter;
    std::optional<CreatureCatalog> catalogBefore, catalogAfter;
    std::unordered_map<Guid, std::optional<Entity>, GuidHash> creaturesBefore, creaturesAfter;
    std::unordered_map<Guid, std::optional<Resource>, GuidHash> resourcesBefore, resourcesAfter;
    std::map<std::size_t, CellSnapshot> before, after;
    std::map<std::string, std::optional<ItemDefinition>> itemsBefore, itemsAfter;
    std::map<std::string, std::optional<ObjectTypeDefinition>> typesBefore, typesAfter;
    std::uint64_t beforeRevision = 0, afterRevision = 0;
};
} // namespace
struct SceneWorld::Impl {
    Grid terrain;
    flecs::world ecs;
    CreatureCatalog catalog;
    std::unordered_map<Guid,HumanName,GuidHash> humanNames;
    std::unordered_set<std::string> reservedNames;
    void nameHuman(Guid id, Species species) {
        auto owner=entity(id);
        if(species!=Species::Human){owner.remove<HumanName>();return;}
        if(!humanNames.contains(id)) {
            auto name=generateHumanName(reservedNames);reservedNames.insert(name.full());humanNames.emplace(id,std::move(name));
        }
        owner.set<HumanName>(humanNames.at(id));
    }
    std::unordered_map<Guid, flecs::entity_t, GuidHash> entities;
    using ChunkKey = std::pair<int, int>;
    using ChunkIndex = std::map<ChunkKey, std::unordered_set<Guid, GuidHash>>;
    ChunkIndex creatureChunks, objectChunks;
    std::unordered_map<std::size_t, Guid> occupied;
    std::unordered_set<Guid, GuidHash> creatureIds;
    std::uint64_t membershipRevision = 0;
    static ChunkKey chunk(CellPosition p) { return {p.x / SceneWorld::chunkSize, p.y / SceneWorld::chunkSize}; }
    static void unindex(ChunkIndex &index, CellPosition p, Guid id) {
        auto it = index.find(chunk(p));
        if (it != index.end() && (it->second.erase(id), it->second.empty())) index.erase(it);
    }
    std::map<std::string, Guid> itemKeys, typeKeys; // Lookup indexes, data lives in ECS.
    std::unordered_set<Guid, GuidHash> used;
    std::vector<ComponentSchema> schemas;
    flecs::entity clock;
    bool history = false, applying = false;
    int depth = 0;
    std::optional<Delta> pending;
    std::vector<Delta> undo, redo;
    std::uint64_t revision = 0, savedRevision = 0, nextRevision = 0;
    std::size_t savedGuidCount = 0;
    Impl(int w, int h, float size, Vector2 origin) : terrain(w, h, size, origin) {
        schemas = registerComponents(ecs);
        const auto commands = ecs.entity("CommandProcessing").add(flecs::Phase).depends_on(flecs::PreUpdate);
        const auto gameplay = ecs.entity("Gameplay").add(flecs::Phase).depends_on(commands);
        const auto spatial = ecs.entity("SpatialReconciliation").add(flecs::Phase).depends_on(gameplay);
        clock = ecs.entity("SimulationClock").set<SimulationClock>({});
        ecs.system("CommandBoundary").kind(commands).run([](flecs::iter &) {});
        ecs.system<SimulationClock>("AdvanceSimulation").kind(gameplay).each([](SimulationClock &time) {
            ++time.ticks;
            time.seconds += 1.0 / 60;
        });
        ecs.system<Resource>("AdvanceCreatureNeeds").kind(gameplay).with<Creature>()
            .each([](Resource &resources) { resources.advanceNeeds(1.0f / 60); });
        ecs.system<const PersistentId, const GridPosition, const TileSprite>("ReconcileSpatial")
            .kind(spatial)
            .with<PlacedObject>()
            .each([this](const PersistentId &id, const GridPosition &position, const TileSprite &sprite) {
                auto &cell = terrain.cells_[terrain.index(position.value)];
                // Mutation commands already clear the previous cell; this phase publishes
                // the ECS-owned position/sprite before subsequent readers run.
                cell.objectId = id.value;
                cell.tile(GridLayer::Objects) = sprite.value;
            });
    }
    flecs::entity entity(Guid id) const {
        const auto found = entities.find(id);
        return found == entities.end() ? flecs::entity{} : ecs.entity(found->second);
    }
    void bindItemDefinition(const std::string &key, Guid definition) {
        for (const auto &[id, handle] : entities) {
            auto item = ecs.entity(handle);
            if (item.has<ItemStack>() && item.get<ItemStack>().definitionId == key)
                item.is_a(entity(definition));
        }
    }
    Guid allocate() {
        if (used.size() >= SceneWorld::maxGuidHistory)
            throw std::length_error("World GUID history is full.");
        auto id = GenerateUniqueGuid();
        used.insert(id);
        return id;
    }
    flecs::entity create(Guid id, bool prefab = false) {
        if (id.empty() || entities.contains(id))
            throw std::invalid_argument("Duplicate or empty entity GUID.");
        auto entity = ecs.entity();
        if (prefab)
            entity.add(flecs::Prefab);
        entity.set<PersistentId>({id});
        entities.emplace(id, entity.id());
        used.insert(id);
        GlobalGuidRegistry().reserve(id);
        return entity;
    }
};
struct SceneWorld::Operation {
    SceneWorld &world;
    int exceptions = std::uncaught_exceptions();
    explicit Operation(SceneWorld &world) : world(world) {
        world.beginEdit();
    }
    ~Operation() noexcept(false) {
        if (std::uncaught_exceptions() > exceptions)
            world.cancelEdit();
        else
            world.endEdit();
    }
};
SceneWorld::SceneWorld(int w, int h, float size, Vector2 origin)
    : impl_(std::make_unique<Impl>(w, h, size, origin)) {}
SceneWorld::~SceneWorld() = default;
SceneWorld::SceneWorld(SceneWorld &&) noexcept = default;
SceneWorld &SceneWorld::operator=(SceneWorld &&) noexcept = default;
int SceneWorld::width() const {
    return impl_->terrain.width();
}
int SceneWorld::height() const {
    return impl_->terrain.height();
}
float SceneWorld::cellSize() const {
    return impl_->terrain.cellSize();
}
std::size_t SceneWorld::count() const {
    return impl_->terrain.count();
}
bool SceneWorld::contains(CellPosition c) const {
    return impl_->terrain.contains(c);
}
std::size_t SceneWorld::index(CellPosition c) const {
    return impl_->terrain.index(c);
}
CellPosition SceneWorld::coordinates(std::size_t i) const {
    return impl_->terrain.coordinates(i);
}
const GridCell &SceneWorld::at(CellPosition c) const {
    return impl_->terrain.at(c);
}
const GridCell *SceneWorld::tryGet(CellPosition c) const {
    return impl_->terrain.tryGet(c);
}
std::optional<CellPosition> SceneWorld::worldToCell(Vector2 p) const {
    return impl_->terrain.worldToCell(p);
}
Vector2 SceneWorld::cellToWorld(CellPosition c) const {
    return impl_->terrain.cellToWorld(c);
}
Vector2 SceneWorld::cellCenter(CellPosition c) const {
    return impl_->terrain.cellCenter(c);
}
Rectangle SceneWorld::cellBounds(CellPosition c) const {
    return impl_->terrain.cellBounds(c);
}
Rectangle SceneWorld::worldBounds() const {
    return impl_->terrain.worldBounds();
}
CellRange SceneWorld::visibleRange(Rectangle r) const {
    return impl_->terrain.visibleRange(r);
}
void SceneWorld::draw(Rectangle view, const TilesetLibrary *library, const EntityPresentation *presentation) const {
    impl_->terrain.draw(view, library);
    const auto range = visibleRange(view);
    if (!range.empty()) for (int cy=range.minY/chunkSize;cy<=(range.maxY-1)/chunkSize;++cy)
        for(int cx=range.minX/chunkSize;cx<=(range.maxX-1)/chunkSize;++cx)
            for(auto id:objectsInChunk({cx,cy})) {
                const auto object=findObject(id);const auto cell=*objectPosition(id);
                const auto bounds=cellBounds(cell);
                if(harvestable(id)) {
                    const auto pool=resource(id,Resource::Health).value_or(*resource(id,Harvestable::Units));
                    if(!pool.full()) {
                        DrawRectangleRec({bounds.x,bounds.y,bounds.width,cellSize()*0.12f},DARKGRAY);
                        DrawRectangleRec({bounds.x,bounds.y,bounds.width*pool.fraction(),cellSize()*0.12f},GREEN);
                    }
                } else if(object->type()==ObjectType::Gatherable && !object->contents().empty()) {
                    const auto center=cellCenter(cell);
                    DrawCircleV(center,cellSize()*0.3f,DARKBROWN);
                    DrawRectangleRec({center.x-cellSize()*0.18f,center.y-cellSize()*0.18f,cellSize()*0.36f,cellSize()*0.36f},GOLD);
                }
            }
    if (!range.empty()) for (int cy = range.minY / chunkSize; cy <= (range.maxY - 1) / chunkSize; ++cy)
        for (int cx = range.minX / chunkSize; cx <= (range.maxX - 1) / chunkSize; ++cx)
            for (auto id : creaturesInChunk({cx, cy})) {
                auto e = impl_->entity(id);
                if (presentation) presentation->drawCreature(*findCreature(id),impl_->catalog,simulationTime());
                else DrawCircleV(e.get<Position>().worldPosition(), cellSize() * 0.35f,
                    e.get<Creature>().control == ControlOwnership::Player ? SKYBLUE : ORANGE);
            }
}
bool SceneWorld::hasUsedGuid(Guid id) const {
    return impl_->used.contains(id);
}
std::size_t SceneWorld::persistentEntityCount() const {
    return impl_->entities.size();
}
std::size_t SceneWorld::objectCount() const {
    std::size_t count = 0;
    for (const auto &[id, handle] : impl_->entities)
        if (impl_->ecs.entity(handle).has<PlacedObject>())
            ++count;
    return count;
}
std::optional<Object> SceneWorld::findObject(Guid id) const {
    auto entity = impl_->entity(id);
    if (!entity || !entity.has<PlacedObject>())
        return {};
    Object result;
    result.id_ = id;
    result.type_ = entity.has<DoorState>()        ? ObjectType::Door
                   : entity.has<ContainerState>() ? ObjectType::Container
                                                  : ObjectType::Gatherable;
    if (entity.has<DoorState>())
        result.isOpen_ = entity.get<DoorState>().open;
    if (entity.has<ContainerState>())
        result.isOpen_ = entity.get<ContainerState>().open;
    const auto definition = entity.target(flecs::IsA);
    if (definition && definition.has<ObjectDefinitionTag>())
        result.typeDefinitionId_ = definition.get<DefinitionKey>().value;
    std::vector<std::pair<std::uint32_t, Item>> items;
    entity.children<ContainedBy>([&](flecs::entity child) {
        const auto &item = child.get<ItemStack>();
        items.push_back(
            {child.get<ItemOrder>().value,
             {item.definitionId, item.displayName, item.quantity, child.get<PersistentId>().value}});
    });
    std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (auto &[order, item] : items)
        result.contents_.push_back(std::move(item));
    return result;
}
std::optional<Object> SceneWorld::objectAt(CellPosition c) const {
    const auto *cell = tryGet(c);
    return cell ? findObject(cell->objectId) : std::nullopt;
}
std::map<std::string, ItemDefinition> SceneWorld::itemDefinitions() const {
    std::map<std::string, ItemDefinition> result;
    for (const auto &[key, id] : impl_->itemKeys)
        result.emplace(key, ItemDefinition{key, impl_->entity(id).get<DisplayName>().value, id});
    return result;
}
std::map<std::string, ObjectTypeDefinition> SceneWorld::objectTypes() const {
    std::map<std::string, ObjectTypeDefinition> result;
    for (const auto &[key, id] : impl_->typeKeys) {
        auto entity = impl_->entity(id);
        result.emplace(key, ObjectTypeDefinition{key, entity.get<DisplayName>().value,
                                                 entity.get<Behavior>().value, id});
    }
    return result;
}
void SceneWorld::createItemDefinition(ItemDefinition value) {
    Object::validate(ObjectType::Container, false, {{value.id, value.displayName, 1}});
    if (impl_->itemKeys.contains(value.id) || impl_->itemKeys.size() >= maxDefinitions)
        throw std::invalid_argument("Duplicate item ID or full catalog.");
    Operation command(*this);
    if (impl_->pending)
        impl_->pending->itemsBefore.try_emplace(value.id, std::nullopt);
    value.guid = impl_->allocate();
    impl_->create(value.guid, true)
        .add<ItemDefinitionTag>()
        .set<DefinitionKey>({value.id})
        .set<DisplayName>({value.displayName});
    impl_->itemKeys.emplace(value.id, value.guid);
    impl_->bindItemDefinition(value.id, value.guid);
}
void SceneWorld::createObjectType(ObjectTypeDefinition value) {
    Object::validate(ObjectType::Container, false, {{value.id, value.displayName, 1}});
    Object::validate(value.behavior, false, {});
    if (impl_->typeKeys.contains(value.id) || impl_->typeKeys.size() >= maxDefinitions)
        throw std::invalid_argument("Duplicate object type ID or full catalog.");
    Operation command(*this);
    if (impl_->pending)
        impl_->pending->typesBefore.try_emplace(value.id, std::nullopt);
    value.guid = impl_->allocate();
    impl_->create(value.guid, true)
        .add<ObjectDefinitionTag>()
        .set<DefinitionKey>({value.id})
        .set<DisplayName>({value.displayName})
        .set<Behavior>({value.behavior});
    impl_->typeKeys.emplace(value.id, value.guid);
}
void SceneWorld::install(CellPosition c, const Object &object, TileRef tile) {
    auto entity =
        impl_->create(object.id()).add<PlacedObject>().set<GridPosition>({c}).set<TileSprite>({tile});
    if (object.type() == ObjectType::Door)
        entity.set<DoorState>({object.isOpen()});
    else if (object.type() == ObjectType::Container)
        entity.set<ContainerState>({object.isOpen()}).add<Inventory>();
    else
        entity.add<Gatherable>().add<Inventory>();
    if (!object.typeDefinitionId().empty())
        entity.is_a(impl_->entity(impl_->typeKeys.at(object.typeDefinitionId())));
    installInventory(object.id(), object.contents());
    impl_->objectChunks[Impl::chunk(c)].insert(object.id());
    auto &cell = impl_->terrain.cells_[index(c)];
    cell.objectId = object.id();
    cell.tile(GridLayer::Objects) = tile; // Derived render/spatial cache.
}
void SceneWorld::installInventory(Guid owner, const std::vector<Item> &items) {
    const auto entity = impl_->entity(owner);
    std::uint32_t order = 0;
    for (const auto &item : items) {
        auto child = impl_->create(item.guid)
                         .set<ItemStack>({item.definitionId, item.displayName, item.quantity})
                         .set<ItemOrder>({order++})
                         .add<ContainedBy>(entity);
        const auto definition = impl_->itemKeys.find(item.definitionId);
        if (definition != impl_->itemKeys.end())
            child.is_a(impl_->entity(definition->second));
    }
}
void SceneWorld::eraseInstance(Guid id) {
    touchHarvestable(id);
    touchResources(id);
    auto entity = impl_->entity(id);
    if (!entity)
        return;
    if (entity.has<PlacedObject>()) Impl::unindex(impl_->objectChunks, entity.get<GridPosition>().value, id);
    std::vector<Guid> children;
    entity.children<ContainedBy>(
        [&](flecs::entity child) { children.push_back(child.get<PersistentId>().value); });
    for (auto child : children) {
        touchResources(child);
        impl_->entity(child).destruct();
        impl_->entities.erase(child);
    }
    entity.destruct();
    impl_->entities.erase(id);
}
Guid SceneWorld::placeObject(CellPosition c, TileRef tile, ObjectType type) {
    at(c);
    Object::validate(type, false, {});
    if (!tile.present() || tile.column < 0 || tile.row < 0)
        throw std::invalid_argument("Invalid object tile.");
    if (creatureAt(c)) throw std::invalid_argument("Cell occupied by creature.");
    if (auto existing = objectAt(c))
        return existing->id();
    const auto id = GenerateUniqueGuid();
    restoreObject(c, tile, id, type, false, {});
    return id;
}
Guid SceneWorld::placeObject(CellPosition c, TileRef tile, const std::string &key) {
    const auto type = impl_->entity(impl_->typeKeys.at(key)).get<Behavior>().value;
    if (auto existing = objectAt(c))
        return existing->id();
    Operation command(*this);
    const auto id = placeObject(c, tile, type);
    bindObjectType(id, key);
    return id;
}
void SceneWorld::bindObjectType(Guid id, const std::string &key) {
    const auto definition = impl_->entity(impl_->typeKeys.at(key));
    const auto object = findObject(id);
    if (!object || object->type() != definition.get<Behavior>().value || !object->typeDefinitionId().empty())
        throw std::invalid_argument("Incompatible object definition.");
    Operation command(*this);
    touch(impl_->entity(id).get<GridPosition>().value);
    impl_->entity(id).is_a(definition);
}
void SceneWorld::restoreObject(CellPosition c, TileRef tile, Guid id, ObjectType type, bool open,
                               std::vector<Item> items) {
    const auto &cell = at(c);
    Object::validate(type, open, items);
    if (id.empty() || hasUsedGuid(id) || creatureAt(c) || !cell.objectId.empty() || !tile.present() || tile.column < 0 ||
        tile.row < 0)
        throw std::invalid_argument("Invalid object placement or GUID.");
    std::unordered_set<Guid, GuidHash> incoming{id};
    for (const auto &item : items)
        if (!item.guid.empty() && (hasUsedGuid(item.guid) || !incoming.insert(item.guid).second))
            throw std::invalid_argument("Duplicate item GUID.");
    if (impl_->used.size() + items.size() + 1 > maxGuidHistory)
        throw std::length_error("GUID history full.");
    Operation command(*this);
    touch(c);
    for (auto &item : items)
        if (item.guid.empty())
            item.guid = impl_->allocate();
    Object object;
    object.id_ = id;
    object.type_ = type;
    object.isOpen_ = open;
    object.contents_ = std::move(items);
    install(c, object, tile);
}
void SceneWorld::removeObject(CellPosition c) {
    if (at(c).objectId.empty())
        return;
    Operation command(*this);
    touch(c);
    eraseInstance(at(c).objectId);
    auto &cell = impl_->terrain.cells_[index(c)];
    cell.objectId = {};
    cell.tile(GridLayer::Objects) = {};
}
bool SceneWorld::moveObject(CellPosition from, CellPosition to) {
    if (!contains(from) || !contains(to) || !objectAt(from))
        return false;
    if (from == to)
        return true;
    if (objectAt(to) || creatureAt(to))
        return false;
    Operation command(*this);
    touch(from);
    touch(to);
    auto &source = impl_->terrain.cells_[index(from)];
    auto &target = impl_->terrain.cells_[index(to)];
    Impl::unindex(impl_->objectChunks, from, source.objectId);
    impl_->objectChunks[Impl::chunk(to)].insert(source.objectId);
    impl_->entity(source.objectId).set<GridPosition>({to});
    target.objectId = source.objectId;
    target.tile(GridLayer::Objects) = source.tile(GridLayer::Objects);
    source.objectId = {};
    source.tile(GridLayer::Objects) = {};
    return true;
}
void SceneWorld::updateObject(Guid id, bool open, std::vector<Item> items) {
    const auto harvest = harvestable(id);
    auto object = findObject(id);
    if (!object)
        throw std::out_of_range("Object not found.");
    Object::validate(object->type(), open, items);
    std::unordered_set<Guid, GuidHash> current, seen;
    for (const auto &item : object->contents())
        current.insert(item.guid);
    std::size_t count = 0;
    for (const auto &item : items) {
        if (item.guid.empty()) {
            ++count;
            continue;
        }
        if (!current.contains(item.guid) || !seen.insert(item.guid).second)
            throw std::invalid_argument("Foreign or duplicated item GUID.");
    }
    if (impl_->used.size() + count > maxGuidHistory)
        throw std::length_error("GUID history full.");
    Operation command(*this);
    const auto entity = impl_->entity(id);
    const auto cell = entity.get<GridPosition>().value;
    const auto tile = entity.get<TileSprite>().value;
    touch(cell);
    for (auto &item : items)
        if (item.guid.empty())
            item.guid = impl_->allocate();
    object->isOpen_ = open;
    object->contents_ = std::move(items);
    std::unordered_map<Guid, Resource, GuidHash> retained;
    if (auto value = resources(id))
        retained.emplace(id, *value);
    entity.children<ContainedBy>([&](flecs::entity child) {
        auto guid = child.get<PersistentId>().value;
        if (auto value = resources(guid))
            retained.emplace(guid, *value);
    });
    eraseInstance(id);
    install(cell, *object, tile);
    if (harvest) impl_->entity(id).set<Harvestable>(*harvest);
    for (const auto &[guid, value] : retained)
        if (auto target = impl_->entity(guid))
            target.set<Resource>(value);
}
void SceneWorld::setObjectOpen(Guid id, bool open) {
    const auto object = findObject(id);
    if (!object)
        throw std::out_of_range("Object not found.");
    updateObject(id, open, object->contents());
}
void SceneWorld::addItem(Guid id, Item item) {
    auto current = inventory(id);
    if (!current) throw std::out_of_range("Inventory not found.");
    auto items = std::move(*current);
    item.guid = {};
    items.push_back(item);
    updateInventory(id, std::move(items));
}
void SceneWorld::updateItem(Guid id, std::size_t position, Item item) {
    auto current = inventory(id);
    if (!current) throw std::out_of_range("Inventory not found.");
    auto items = std::move(*current);
    const auto old = items.at(position).guid;
    if (!item.guid.empty() && item.guid != old)
        throw std::invalid_argument("Cannot change GUID.");
    item.guid = old;
    items[position] = item;
    updateInventory(id, std::move(items));
}
void SceneWorld::removeItem(Guid id, std::size_t position) {
    auto current = inventory(id);
    if (!current) throw std::out_of_range("Inventory not found.");
    auto items = std::move(*current);
    if (position >= items.size())
        throw std::out_of_range("Item index invalid.");
    items.erase(items.begin() + position);
    updateInventory(id, std::move(items));
}
std::optional<std::vector<Item>> SceneWorld::inventory(Guid id) const {
    const auto owner = impl_->entity(id);
    if (!owner || !owner.has<Inventory>()) return {};
    std::map<std::uint32_t, Item> ordered;
    owner.children<ContainedBy>([&](flecs::entity child) {
        const auto &stack = child.get<ItemStack>();
        ordered.emplace(child.get<ItemOrder>().value,
            Item{stack.definitionId, stack.displayName, stack.quantity, child.get<PersistentId>().value});
    });
    std::vector<Item> items;
    for (const auto &[order, item] : ordered) items.push_back(item);
    return items;
}
void SceneWorld::updateInventory(Guid id, std::vector<Item> items) {
    auto current = inventory(id);
    if (!current) throw std::out_of_range("Inventory not found.");
    if (auto object = findObject(id)) {
        updateObject(id, object->isOpen(), std::move(items));
        return;
    }
    Inventory::validate(items);
    std::unordered_set<Guid, GuidHash> owned, seen;
    for (const auto &item : *current) owned.insert(item.guid);
    std::size_t newStacks = 0;
    for (const auto &item : items) {
        if (item.guid.empty()) ++newStacks;
        else if (!owned.contains(item.guid) || !seen.insert(item.guid).second)
            throw std::invalid_argument("Foreign or duplicated item GUID.");
    }
    if (impl_->used.size() + newStacks > maxGuidHistory)
        throw std::length_error("GUID history full.");
    if (*current == items) return;
    Operation command(*this);
    touchCreature(id);
    for (auto &item : items) if (item.guid.empty()) item.guid = impl_->allocate();
    std::unordered_map<Guid, Resource, GuidHash> retained;
    for (const auto &item : *current) {
        if (auto resource = resources(item.guid)) retained.emplace(item.guid, *resource);
        impl_->entity(item.guid).destruct();
        impl_->entities.erase(item.guid);
    }
    installInventory(id, items);
    for (const auto &[guid, value] : retained)
        if (auto child = impl_->entity(guid)) child.set<Resource>(value);
}
void SceneWorld::paint(CellPosition c, GridLayer layer, TileRef tile) {
    at(c).tile(layer);
    if (!tile.present() || tile.column < 0 || tile.row < 0)
        throw std::invalid_argument("Invalid tile.");
    if (layer == GridLayer::Objects) {
        placeObject(c, tile, ObjectType::Container);
        return;
    }
    if (at(c).tile(layer) == tile)
        return;
    Operation command(*this);
    touch(c);
    impl_->terrain.cells_[index(c)].tile(layer) = tile;
}
void SceneWorld::erase(CellPosition c, GridLayer layer) {
    if (layer == GridLayer::Objects) {
        removeObject(c);
        return;
    }
    if (!at(c).tile(layer).present())
        return;
    Operation command(*this);
    touch(c);
    impl_->terrain.cells_[index(c)].tile(layer) = {};
}
void SceneWorld::set(CellPosition c, GridCell value) {
    at(c);
    if (!value.objectId.empty())
        throw std::invalid_argument("Cannot copy object GUIDs.");
    for (const auto &tile : value.layers)
        if (tile.present() && (tile.column < 0 || tile.row < 0))
            throw std::invalid_argument("Invalid tile.");
    if (at(c) == value && !creatureAt(c))
        return;
    if (value.tile(GridLayer::Objects).present() && impl_->used.size() >= maxGuidHistory)
        throw std::length_error("GUID history full.");
    Operation command(*this);
    touch(c);
    if (auto creature = creatureAt(c)) removeCreature(*creature);
    removeObject(c);
    const auto objectTile = value.tile(GridLayer::Objects);
    value.tile(GridLayer::Objects) = {};
    impl_->terrain.cells_[index(c)] = value;
    if (objectTile.present())
        placeObject(c, objectTile, ObjectType::Container);
}
void SceneWorld::fill(GridCell value) {
    fillRegion({0, 0, width(), height()}, value);
}
void SceneWorld::clear() {
    fill({});
}
void SceneWorld::fillRegion(CellRange region, GridCell value) {
    if (!value.objectId.empty())
        throw std::invalid_argument("Cannot copy object GUIDs.");
    for (const auto &tile : value.layers)
        if (tile.present() && (tile.column < 0 || tile.row < 0))
            throw std::invalid_argument("Invalid tile.");
    if (value.tile(GridLayer::Objects).present()) {
        const auto width = std::max(0, std::min(this->width(), region.maxX) - std::max(0, region.minX));
        const auto height = std::max(0, std::min(this->height(), region.maxY) - std::max(0, region.minY));
        if (impl_->used.size() + static_cast<std::size_t>(width) * height > maxGuidHistory)
            throw std::length_error("GUID history full.");
    }
    Operation command(*this);
    for (int y = std::max(0, region.minY); y < std::min(height(), region.maxY); ++y)
        for (int x = std::max(0, region.minX); x < std::min(width(), region.maxX); ++x)
            set({x, y}, value);
}
bool SceneWorld::isWalkable(CellPosition c) const {
    const auto *cell = tryGet(c);
    if (!cell || creatureAt(c) || !cell->tile(GridLayer::Ground).present() || cell->tile(GridLayer::Walls).present() ||
        cell->tile(GridLayer::Entities).present())
        return false;
    if (cell->objectId.empty())
        return true;
    const auto entity = impl_->entity(cell->objectId);
    return entity && entity.has<DoorState>() && entity.get<DoorState>().open;
}
bool SceneWorld::blocksSight(CellPosition c) const {
    const auto *cell = tryGet(c);
    if (!cell || creatureAt(c) || cell->layersBlockSight())
        return true;
    const auto entity = impl_->entity(cell->objectId);
    return entity && entity.has<DoorState>() && !entity.get<DoorState>().open;
}
std::vector<CellPosition> SceneWorld::neighbors(CellPosition c, bool diagonals, bool walkable) const {
    std::vector<CellPosition> result;
    if (!contains(c))
        return result;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            CellPosition next{c.x + x, c.y + y};
            if ((x || y) && (diagonals || !x || !y) && contains(next) && (!walkable || isWalkable(next)))
                result.push_back(next);
        }
    return result;
}

WorldDocument SceneWorld::document() const {
    const auto bounds = worldBounds();
    WorldDocument doc(width(), height(), cellSize(), {bounds.x, bounds.y});
    doc.cells_ = impl_->terrain.cells_;
    for (const auto &[id,handle] : impl_->entities)
        if (auto value=harvestable(id)) doc.harvestables.emplace(id,*value);
    for (const auto &[id, handle] : impl_->entities)
        if (auto value = resources(id))
            doc.resources.emplace(id, *value);
    for (const auto &[id, handle] : impl_->entities)
        if (impl_->ecs.entity(handle).has<PlacedObject>())
            doc.objects_.emplace(id, *findObject(id));
    for (auto id : impl_->creatureIds) {
        auto e = findCreature(id);
        doc.creatures.push_back({id, e->type(), e->control(), e->position(),e->species(),e->vocation(),e->inventory(),e->disposition()});
    }
    doc.itemDefinitions_ = itemDefinitions();
    doc.creatureCatalog=impl_->catalog;
    doc.humanNames=impl_->humanNames;
    doc.objectTypes_ = objectTypes();
    doc.usedGuids_ = impl_->used;
    return doc;
}
SceneWorld SceneWorld::fromDocument(const WorldDocument &doc) {
    // Validate the complete identity table before building any runtime relationships.
    doc.validateGuidState(doc.guidState());
    doc.validateHarvestables();
    doc.validateHumanNames();
    SceneWorld result(doc.width_, doc.height_, doc.cellSize_, doc.origin_);
    auto &impl = *result.impl_;
    impl.humanNames=doc.humanNames;
    for(const auto &[id,name]:doc.humanNames)impl.reservedNames.insert(name.full());
    doc.creatureCatalog.validate();impl.catalog=doc.creatureCatalog;
    for (const auto &[key, value] : doc.itemDefinitions_) {
        Object::validate(ObjectType::Container, false, {{key, value.displayName, 1}});
        if (key != value.id)
            throw std::invalid_argument("Item definition key mismatch.");
        impl.create(value.guid, true)
            .add<ItemDefinitionTag>()
            .set<DefinitionKey>({key})
            .set<DisplayName>({value.displayName});
        impl.itemKeys.emplace(key, value.guid);
    }
    for (const auto &[key, value] : doc.objectTypes_) {
        Object::validate(value.behavior, false, {});
        Object::validate(ObjectType::Container, false, {{key, value.displayName, 1}});
        if (key != value.id)
            throw std::invalid_argument("Object definition key mismatch.");
        impl.create(value.guid, true)
            .add<ObjectDefinitionTag>()
            .set<DefinitionKey>({key})
            .set<DisplayName>({value.displayName})
            .set<Behavior>({value.behavior});
        impl.typeKeys.emplace(key, value.guid);
    }
    impl.terrain.cells_ = doc.cells_;
    std::unordered_set<Guid, GuidHash> placed;
    for (std::size_t i = 0; i < doc.cells_.size(); ++i) {
        const auto &cell = doc.cells_[i];
        for (const auto &tile : cell.layers)
            if (tile.present() && (tile.column < 0 || tile.row < 0))
                throw std::invalid_argument("Invalid tile coordinates.");
        if (cell.objectId.empty()) {
            if (cell.tile(GridLayer::Objects).present())
                throw std::invalid_argument("Object tile has no owner.");
            continue;
        }
        if (!placed.insert(cell.objectId).second || !doc.objects_.contains(cell.objectId) ||
            !cell.tile(GridLayer::Objects).present())
            throw std::invalid_argument("Invalid object spatial reference.");
        const auto &object = doc.objects_.at(cell.objectId);
        Object::validate(object.type(), object.isOpen(), object.contents());
        if (object.id() != cell.objectId)
            throw std::invalid_argument("Object identity mismatch.");
        if (!object.typeDefinitionId().empty() &&
            (!doc.objectTypes_.contains(object.typeDefinitionId()) ||
             doc.objectTypes_.at(object.typeDefinitionId()).behavior != object.type()))
            throw std::invalid_argument("Object definition behavior mismatch.");
        result.install(result.coordinates(i), object, cell.tile(GridLayer::Objects));
    }
    if (placed.size() != doc.objects_.size())
        throw std::invalid_argument("Orphan object.");
    for (const auto &record : doc.creatures) {
        const auto resource = doc.resources.find(record.id);
        if (resource == doc.resources.end() || !resource->second.find(Resource::Health))
            throw std::invalid_argument("Creature must have health.");
        result.installCreature(record, resource->second);
    }
    if (doc.resources.size() > maxGuidHistory)
        throw std::invalid_argument("Too many resource owners.");
    for (const auto &[id, value] : doc.resources) {
        auto entity = impl.entity(id);
        if (!entity || entity.has(flecs::Prefab) || value.pools().empty())
            throw std::invalid_argument("Invalid resource owner.");
        entity.set<Resource>(value);
    }
    result.reserveHistory(doc.usedGuids_);
    for (const auto &[id,h] : doc.harvestables) {
        const auto object=result.findObject(id);
        const auto pool=result.resource(id,Harvestable::Units);
        if (!object || object->type()!=ObjectType::Gatherable || !impl.catalog.harvestables.contains(h.definitionId) || !pool ||
            !std::isfinite(h.regenerationRemaining) || h.regenerationRemaining<0)
            throw std::invalid_argument("Invalid harvestable owner, definition, units, or timer.");
        validateHarvestPool(*pool);
        const auto &d=impl.catalog.harvestables.at(h.definitionId);
        if (h.regenerationRemaining>d.regenerationSeconds || (!pool->depleted() && h.regenerationRemaining!=0))
            throw std::invalid_argument("Invalid harvest regeneration state.");
        impl.entity(id).set<Harvestable>(h);
    }
    result.markSaved();
    return result;
}
GuidState SceneWorld::guidState() const {
    GuidState state;
    state.used = impl_->used;
    state.items = impl_->itemKeys;
    state.types = impl_->typeKeys;
    for (const auto &[id, handle] : impl_->entities)
        if (impl_->ecs.entity(handle).has<PlacedObject>()) {
            const auto object = findObject(id);
            auto &contents = state.contents[id];
            for (const auto &item : object->contents())
                contents.push_back(item.guid);
        }
    return state;
}
void SceneWorld::restoreGuidState(GuidState state) {
    auto doc = document();
    doc.restoreGuidState(std::move(state));
    auto replacement = fromDocument(doc);
    *this = std::move(replacement);
}
void SceneWorld::reserveHistory(const std::unordered_set<Guid, GuidHash> &history) {
    auto merged = impl_->used;
    merged.insert(history.begin(), history.end());
    if (merged.size() > maxGuidHistory || merged.contains(Guid{}))
        throw std::invalid_argument("Invalid GUID history.");
    for (auto id : merged)
        GlobalGuidRegistry().reserve(id);
    impl_->used = std::move(merged);
}
void SceneWorld::setHistoryEnabled(bool enabled) {
    if (impl_->depth)
        throw std::logic_error("Finish current edit first.");
    impl_->history = enabled;
}
void SceneWorld::beginEdit() {
    if (impl_->depth++ == 0 && impl_->history && !impl_->applying) {
        impl_->pending.emplace();
        impl_->pending->beforeRevision = impl_->revision;
    }
}
void SceneWorld::touch(CellPosition cell) {
    const auto owner = at(cell).objectId;
    if (!owner.empty()) {
        touchHarvestable(owner);
        touchResources(owner);
        impl_->entity(owner).children<ContainedBy>(
            [&](flecs::entity child) { touchResources(child.get<PersistentId>().value); });
    }
    if (impl_->pending)
        impl_->pending->before.try_emplace(index(cell), CellSnapshot{at(cell), objectAt(cell)});
}
void SceneWorld::endEdit() {
    if (impl_->depth <= 0 || --impl_->depth)
        return;
    if (!impl_->pending)
        return;
    auto delta = std::move(*impl_->pending);
    impl_->pending.reset();
    for (auto it = delta.before.begin(); it != delta.before.end();) {
        const auto cell = coordinates(it->first);
        CellSnapshot after{at(cell), objectAt(cell)};
        if (it->second == after)
            it = delta.before.erase(it);
        else {
            delta.after.emplace(it->first, std::move(after));
            ++it;
        }
    }
    const auto items = itemDefinitions();
    const auto types = objectTypes();
    for (const auto &[key, value] : delta.itemsBefore)
        delta.itemsAfter[key] = items.contains(key) ? std::optional(items.at(key)) : std::nullopt;
    for (const auto &[key, value] : delta.typesBefore)
        delta.typesAfter[key] = types.contains(key) ? std::optional(types.at(key)) : std::nullopt;
    for (auto it = delta.creaturesBefore.begin(); it != delta.creaturesBefore.end();) {
        auto after = findCreature(it->first);
        if (after == it->second) it = delta.creaturesBefore.erase(it);
        else { delta.creaturesAfter.emplace(it->first, after); ++it; }
    }
    for (auto it = delta.resourcesBefore.begin(); it != delta.resourcesBefore.end();) {
        auto after = resources(it->first);
        if ((!after && !it->second) || (after == it->second && delta.before.empty() && delta.creaturesBefore.empty()))
            it = delta.resourcesBefore.erase(it);
        else {
            delta.resourcesAfter.emplace(it->first, after);
            ++it;
        }
    }
    if(delta.catalogBefore) {if(*delta.catalogBefore==impl_->catalog)delta.catalogBefore.reset();else delta.catalogAfter=impl_->catalog;}
    for (auto it=delta.harvestBefore.begin();it!=delta.harvestBefore.end();) {
        auto after=harvestable(it->first);
        if (after==it->second && delta.before.empty()) it=delta.harvestBefore.erase(it);
        else {delta.harvestAfter.emplace(it->first,after);++it;}
    }
    if (!delta.catalogBefore && delta.creaturesBefore.empty() && delta.before.empty() && delta.itemsBefore.empty() && delta.typesBefore.empty() &&
        delta.resourcesBefore.empty() && delta.harvestBefore.empty())
        return;
    delta.afterRevision = impl_->revision = ++impl_->nextRevision;
    impl_->undo.push_back(std::move(delta));
    impl_->redo.clear();
    if (impl_->undo.size() > 100)
        impl_->undo.erase(impl_->undo.begin());
}
void SceneWorld::applyHistory(bool forward) {
    auto &source = forward ? impl_->redo : impl_->undo;
    auto &destination = forward ? impl_->undo : impl_->redo;
    if (source.empty())
        return;
    auto delta = std::move(source.back());
    source.pop_back();
    const auto &creatures = forward ? delta.creaturesAfter : delta.creaturesBefore;
    if(delta.catalogBefore)impl_->catalog=forward?*delta.catalogAfter:*delta.catalogBefore;
    for (const auto &[id, value] : creatures) eraseCreature(id);
    const auto &cells = forward ? delta.after : delta.before;
    const auto &items = forward ? delta.itemsAfter : delta.itemsBefore;
    const auto &types = forward ? delta.typesAfter : delta.typesBefore;
    for (const auto &[i, snapshot] : cells) {
        auto id = impl_->terrain.cells_[i].objectId;
        if (!id.empty())
            eraseInstance(id);
    }
    for (const auto &[key, value] : items) {
        if (impl_->itemKeys.contains(key)) {
            auto id = impl_->itemKeys.at(key);
            impl_->entity(id).destruct();
            impl_->entities.erase(id);
            impl_->itemKeys.erase(key);
        }
        if (value) {
            impl_->create(value->guid, true)
                .add<ItemDefinitionTag>()
                .set<DefinitionKey>({key})
                .set<DisplayName>({value->displayName});
            impl_->itemKeys[key] = value->guid;
            impl_->bindItemDefinition(key, value->guid);
        }
    }
    for (const auto &[key, value] : types) {
        if (impl_->typeKeys.contains(key)) {
            auto id = impl_->typeKeys.at(key);
            impl_->entity(id).destruct();
            impl_->entities.erase(id);
            impl_->typeKeys.erase(key);
        }
        if (value) {
            impl_->create(value->guid, true)
                .add<ObjectDefinitionTag>()
                .set<DefinitionKey>({key})
                .set<DisplayName>({value->displayName})
                .set<Behavior>({value->behavior});
            impl_->typeKeys[key] = value->guid;
        }
    }
    for (const auto &[i, snapshot] : cells) {
        impl_->terrain.cells_[i] = snapshot.cell;
        if (snapshot.object)
            install(coordinates(i), *snapshot.object, snapshot.cell.tile(GridLayer::Objects));
    }
    for (const auto &[id, value] : creatures)
        if (value) installCreature({id, value->type(), value->control(), value->position(),value->species(),value->vocation(),value->inventory(),value->disposition()}, value->resources());
    for (const auto &[id, value] : forward ? delta.resourcesAfter : delta.resourcesBefore) {
        if (auto entity = impl_->entity(id)) {
            if (value)
                entity.set<Resource>(*value);
            else
                entity.remove<Resource>();
        }
    }
    impl_->revision = forward ? delta.afterRevision : delta.beforeRevision;
    for (const auto &[id,value] : forward?delta.harvestAfter:delta.harvestBefore)
        if (auto entity=impl_->entity(id)) {
            if (value) entity.set<Harvestable>(*value); else entity.remove<Harvestable>();
        }
    destination.push_back(std::move(delta));
}
void SceneWorld::cancelEdit() {
    impl_->depth = 0;
    if (!impl_->pending)
        return;
    // Roll back only this transaction; keep the existing undo and redo lists.
    auto redo = std::move(impl_->redo);
    impl_->undo.push_back(std::move(*impl_->pending));
    impl_->pending.reset();
    applyHistory(false);
    impl_->redo = std::move(redo);
}
bool SceneWorld::undo() {
    if (impl_->depth || !canUndo())
        return false;
    applyHistory(false);
    return true;
}
bool SceneWorld::redo() {
    if (impl_->depth || !canRedo())
        return false;
    applyHistory(true);
    return true;
}
bool SceneWorld::canUndo() const {
    return !impl_->undo.empty();
}
bool SceneWorld::canRedo() const {
    return !impl_->redo.empty();
}
void SceneWorld::clearHistory() {
    while (impl_->depth)
        endEdit();
    impl_->undo.clear();
    impl_->redo.clear();
}
void SceneWorld::markSaved() {
    impl_->savedRevision = impl_->revision;
    impl_->savedGuidCount = impl_->used.size();
}
bool SceneWorld::dirty() const {
    return impl_->revision != impl_->savedRevision || impl_->used.size() != impl_->savedGuidCount;
}
void SceneWorld::tick() {
    if (impl_->depth)
        throw std::logic_error("Cannot simulate a pending authoring stroke.");
    impl_->ecs.progress(1.0f / 60);
    advanceHarvestLifecycle(1.0 / 60);
    collectGatherables();
}
std::uint64_t SceneWorld::ticks() const {
    return impl_->clock.get<SimulationClock>().ticks;
}
double SceneWorld::simulationTime() const {
    return impl_->clock.get<SimulationClock>().seconds;
}
std::vector<std::string> SceneWorld::componentNames(Guid id) const {
    std::vector<std::string> names;
    const auto entity = impl_->entity(id);
    if (!entity)
        return names;
    for (const auto &schema : impl_->schemas)
        if (entity.has(schema.component)) {
            names.push_back(schema.name);
            for (const auto &field : schema.fields)
                names.push_back("  " + field.name + ": " + (field.read ? field.read(entity) : field.editor) +
                                (field.writable ? "" : " [locked]"));
        }
    const auto definition = entity.target(flecs::IsA);
    if (auto h=harvestable(id)) {
        const auto &d=impl_->catalog.harvestables.at(h->definitionId);
        const auto pool=resource(id,Harvestable::Units);
        names.push_back("  state: "+(pool->depleted()?d.depletedState:d.name));
        names.push_back("  action: "+d.action+" / "+std::to_string(d.workSeconds)+" seconds");
        names.push_back("  tool: "+(d.requiredTool.empty()?std::string("None"):d.requiredTool));
        for (const auto &item:d.yields) names.push_back("  yield: "+item.displayName+" x"+std::to_string(item.quantity));
    }
    if (definition)
        names.push_back("Definition: " + definition.get<DefinitionKey>().value);
    const auto owner = entity.target<ContainedBy>();
    if (owner)
        names.push_back("ContainedBy: " + owner.get<PersistentId>().value.toString());
    return names;
}

bool SceneWorld::editableField(Guid id, const std::string &name) const {
    const auto entity = impl_->entity(id);
    if (!entity)
        return false;
    for (const auto &schema : impl_->schemas)
        if (entity.has(schema.component))
            for (const auto &field : schema.fields)
                if (field.name == name && field.writable)
                    return true;
    return false;
}

std::optional<CellPosition> SceneWorld::objectPosition(Guid id) const {
    const auto entity = impl_->entity(id);
    return entity && entity.has<PlacedObject>() ? std::optional(entity.get<GridPosition>().value)
                                                : std::nullopt;
}
bool SceneWorld::moveObjectById(Guid id, CellPosition destination) {
    const auto from = objectPosition(id);
    return from && moveObject(*from, destination);
}
void SceneWorld::removeObjectById(Guid id) {
    if (const auto cell = objectPosition(id))
        removeObject(*cell);
}

std::optional<Resource> SceneWorld::resources(Guid id) const {
    const auto entity = impl_->entity(id);
    if (!entity || !entity.has<Resource>())
        return {};
    return entity.get<Resource>();
}
std::optional<ResourcePool> SceneWorld::resource(Guid id, const std::string &key) const {
    const auto value = resources(id);
    if (!value)
        return {};
    const auto pool = value->find(key);
    return pool ? std::optional(*pool) : std::nullopt;
}
void SceneWorld::touchResources(Guid id) {
    if (impl_->pending)
        impl_->pending->resourcesBefore.try_emplace(id, resources(id));
}
void SceneWorld::setResource(Guid id, const std::string &key, ResourcePool pool) {
    if (key==Harvestable::Units && harvestable(id)) validateHarvestPool(pool);
    auto entity = impl_->entity(id);
    if (!entity || entity.has(flecs::Prefab))
        throw std::invalid_argument("Resources require a persistent instance.");
    auto value = resources(id).value_or(Resource{});
    value.set(key, pool);
    if (resources(id) == value)
        return;
    Operation command(*this);
    touchResources(id);
    entity.set<Resource>(value);
    if (key==Harvestable::Units) if (auto h=harvestable(id)) {
        touchHarvestable(id);
        const auto &d=impl_->catalog.harvestables.at(h->definitionId);
        h->regenerationRemaining=pool.depleted()?d.regenerationSeconds:0;
        const auto cell=entity.get<GridPosition>().value;
        const auto tile=pool.depleted() && d.depletedTile.present()?d.depletedTile:h->sourceTile;
        if (at(cell).tile(GridLayer::Objects)!=tile) {
            touch(cell);entity.set<TileSprite>({tile});impl_->terrain.cells_[index(cell)].tile(GridLayer::Objects)=tile;
        }
        entity.set<Harvestable>(*h);
        auto health=resource(id,Resource::Health).value_or(ResourcePool(100));
        health.setCurrent(health.maximum()*pool.fraction());
        auto synced=entity.get<Resource>();synced.set(Resource::Health,health);entity.set<Resource>(synced);
    }
}
void SceneWorld::removeResource(Guid id, const std::string &key) {
    if (key==Harvestable::Units && harvestable(id)) throw std::invalid_argument("Harvestable requires harvest units.");
    if (impl_->creatureIds.contains(id) && key == Resource::Health)
        throw std::invalid_argument("Creatures require Health.");
    auto value = resources(id);
    if (!value || !value->remove(key))
        return;
    Operation command(*this);
    touchResources(id);
    if (value->pools().empty())
        impl_->entity(id).remove<Resource>();
    else
        impl_->entity(id).set<Resource>(*value);
}
float SceneWorld::adjustResource(Guid id, const std::string &key, float delta) {
    auto pool = resource(id, key);
    if (!pool)
        throw std::out_of_range("Resource pool not found.");
    const auto changed = pool->adjust(delta);
    setResource(id, key, *pool);
    return changed;
}
bool SceneWorld::trySpendResource(Guid id, const std::string &key, float amount) {
    auto pool = resource(id, key);
    if (!pool)
        throw std::out_of_range("Resource pool not found.");
    if (!pool->trySpend(amount))
        return false;
    setResource(id, key, *pool);
    return true;
}

CellPosition SceneWorld::chunkOf(CellPosition c) const {
    if (!contains(c)) throw std::out_of_range("Cell outside world.");
    return {c.x / chunkSize, c.y / chunkSize};
}
std::vector<Guid> SceneWorld::creaturesInChunk(CellPosition chunk) const {
    auto it = impl_->creatureChunks.find({chunk.x, chunk.y});
    return it == impl_->creatureChunks.end() ? std::vector<Guid>{} : std::vector<Guid>(it->second.begin(), it->second.end());
}
std::vector<Guid> SceneWorld::objectsInChunk(CellPosition chunk) const {
    auto it = impl_->objectChunks.find({chunk.x, chunk.y});
    return it == impl_->objectChunks.end() ? std::vector<Guid>{} : std::vector<Guid>(it->second.begin(), it->second.end());
}
std::vector<Guid> SceneWorld::creatureIds() const { return {impl_->creatureIds.begin(), impl_->creatureIds.end()}; }
std::uint64_t SceneWorld::creatureMembershipRevision() const { return impl_->membershipRevision; }
std::optional<Guid> SceneWorld::creatureAt(CellPosition c) const {
    if (!contains(c)) return {};
    auto it = impl_->occupied.find(index(c));
    return it == impl_->occupied.end() ? std::nullopt : std::optional(it->second);
}
std::optional<Entity> SceneWorld::findCreature(Guid id) const {
    auto e = impl_->entity(id);
    if (!e || !e.has<Creature>()) return {};
    const auto &c = e.get<Creature>();
    return Entity(id, c.type, e.get<Resource>(), e.get<Position>(), c.control,e.get<SpeciesComponent>(),e.get<VocationComponent>(),*inventory(id),e.has<HumanName>()?std::optional(e.get<HumanName>()):std::nullopt,e.get<DispositionComponent>().value);
}
void SceneWorld::touchCreature(Guid id) {
    if (impl_->pending) impl_->pending->creaturesBefore.try_emplace(id, findCreature(id));
    touchResources(id);
    if (auto owner = impl_->entity(id))
        owner.children<ContainedBy>([&](flecs::entity child) { touchResources(child.get<PersistentId>().value); });
}
void SceneWorld::installCreature(const CreatureRecord &r, const Resource &resources) {
    dispositionName(r.disposition);
    Inventory::validate(r.inventory);
    if(!impl_->catalog.permits(r.species,r.vocation))throw std::invalid_argument("Invalid species, subspecies or vocation.");
    const auto c = r.position.cellCoordinates();
    const auto bounds = worldBounds();
    if (!contains(c) || creatureAt(c) || r.position.cellSize() != cellSize() ||
        r.position.gridOrigin().x != bounds.x || r.position.gridOrigin().y != bounds.y ||
        !resources.find(Resource::Health) || r.type.empty() || r.type.size() > Entity::maxTypeBytes ||
        r.type.find('\0') != std::string::npos || r.type.find_first_not_of(" \t\r\n") == std::string::npos ||
        (r.control != ControlOwnership::AI && r.control != ControlOwnership::Player))
        throw std::invalid_argument("Invalid creature record.");
    impl_->create(r.id).set<Creature>({r.type, r.control}).set<Position>(r.position).set<Resource>(resources).set<SpeciesComponent>(r.species).set<VocationComponent>(r.vocation).set<DispositionComponent>({r.disposition}).add<Inventory>();
    impl_->nameHuman(r.id,r.species.species);
    installInventory(r.id, r.inventory);
    impl_->occupied.emplace(index(c), r.id);
    impl_->creatureChunks[Impl::chunk(c)].insert(r.id);
    impl_->creatureIds.insert(r.id);
    ++impl_->membershipRevision;
}
Guid SceneWorld::spawnCreature(CellPosition c, std::string type, ControlOwnership control, ResourcePool health) {
    if (!isWalkable(c)) throw std::invalid_argument("Creature spawn cell is blocked.");
    const auto bounds = worldBounds();
    Entity draft(std::move(type), health, Position(cellCenter(c), cellSize(), {bounds.x, bounds.y}), control);
    for(const auto& [key,maximum]:impl_->catalog.species.at(0).resources)
        if(key!=Resource::Health)draft.setResource(key,ResourcePool(maximum));
    if (impl_->used.size() >= maxGuidHistory) throw std::length_error("GUID history full.");
    Operation command(*this);
    touchCreature(draft.id());
    installCreature({draft.id(), draft.type(), control, draft.position(),{},{},{},impl_->catalog.species.at(0).defaultDisposition}, draft.resources());
    return draft.id();
}
void SceneWorld::eraseCreature(Guid id) {
    auto e = impl_->entity(id);
    if (!e || !e.has<Creature>()) return;
    const auto c = e.get<Position>().cellCoordinates();
    impl_->occupied.erase(index(c));
    Impl::unindex(impl_->creatureChunks, c, id);
    impl_->creatureIds.erase(id);
    eraseInstance(id);
    ++impl_->membershipRevision;
}
void SceneWorld::removeCreature(Guid id) {
    if (!impl_->creatureIds.contains(id)) return;
    Operation command(*this); touchCreature(id); eraseCreature(id);
}
bool SceneWorld::moveCreature(Guid id, CellPosition to) {
    auto e = impl_->entity(id);
    if (!e || !e.has<Creature>() || !contains(to)) return false;
    if (e.get<Resource>().find(Resource::Health)->depleted()) return false;
    const auto from = e.get<Position>().cellCoordinates();
    if (from == to) return true;
    if (!isWalkable(to)) return false;
    Operation command(*this); touchCreature(id);
    auto position = e.get<Position>(); position.setWorldPosition(cellCenter(to));
    e.set<Position>(position);
    impl_->occupied.erase(index(from)); impl_->occupied.emplace(index(to), id);
    Impl::unindex(impl_->creatureChunks, from, id);
    impl_->creatureChunks[Impl::chunk(to)].insert(id);
    return true;
}
// Sparse four-way search avoids allocating world-sized scratch arrays for every creature.
std::vector<CellPosition> SceneWorld::creaturePath(Guid id, CellPosition goal, std::size_t maxNodes) const {
    auto creature = findCreature(id);
    if (!creature || !contains(goal)) return {};
    auto start = creature->position().cellCoordinates();
    if (start == goal) return {start};
    if (!isWalkable(goal)) return {};
    std::unordered_map<std::size_t, std::size_t> parent;
    std::queue<CellPosition> open; open.push(start); parent[index(start)] = index(start);
    while (!open.empty()) {
        const auto c = open.front(); open.pop();
        for (auto next : neighbors(c, false, true)) {
            const auto ni = index(next);
            if (parent.contains(ni)) continue;
            if (parent.size() >= maxNodes) return {};
            parent[ni] = index(c);
            if (next == goal) {
                std::vector<CellPosition> result{goal};
                for (auto i = ni; i != index(start);) { i = parent.at(i); result.push_back(coordinates(i)); }
                std::reverse(result.begin(), result.end()); return result;
            }
            open.push(next);
        }
    }
    return {};
}
std::vector<std::pair<CellPosition, int>> SceneWorld::reachableCells(Guid id, int radius) const {
    if (radius < 0 || radius > 128) throw std::invalid_argument("Perception radius outside supported range.");
    auto creature = findCreature(id); if (!creature) return {};
    std::vector<std::pair<CellPosition,int>> found{{creature->position().cellCoordinates(),0}};
    std::unordered_set<std::size_t> visited{index(found.front().first)};
    for (std::size_t i = 0; i < found.size(); ++i) {
        const auto [c,d] = found[i]; if (d == radius) continue;
        for (auto n : neighbors(c, false, true)) if (visited.insert(index(n)).second) found.emplace_back(n,d+1);
    }
    return found;
}

const scene::CreatureCatalog& SceneWorld::creatureCatalog() const {return impl_->catalog;}
void SceneWorld::setCreatureIdentity(Guid id, SpeciesComponent species, VocationComponent vocation) {
    auto actor=findCreature(id);
    if(!actor || !impl_->catalog.permits(species,vocation))throw std::invalid_argument("Invalid creature assignment.");
    const auto& definition=impl_->catalog.species.at(static_cast<std::uint32_t>(species.species));
    Resource pools;
    for(const auto& [key,maximum]:definition.resources) {
        auto prior=actor->resources().find(key);
        pools.set(key,ResourcePool(maximum,prior?std::min(prior->current(),maximum):maximum));
    }
    Operation command(*this);touchCreature(id);
    impl_->entity(id).set<SpeciesComponent>(species).set<VocationComponent>(vocation).set<Resource>(pools)
        .set<Creature>({definition.name,actor->control()});
    if(actor->species().species!=species.species)
        impl_->entity(id).set<DispositionComponent>({definition.defaultDisposition});
    impl_->nameHuman(id,species.species);
}
void SceneWorld::setCreatureCatalog(CreatureCatalog catalog) {
    catalog.validate();
    for (const auto &[id,handle] : impl_->entities) if (auto h=harvestable(id)) {
        if (!catalog.harvestables.contains(h->definitionId)) throw std::invalid_argument("Harvest definition is in use.");
        // Existing work and timers must retain the recipe they started with.
        if (catalog.harvestables.at(h->definitionId)!=impl_->catalog.harvestables.at(h->definitionId))
            throw std::invalid_argument("Create a new definition to change a recipe already assigned to nodes.");
    }
    for(auto id:impl_->creatureIds) {
        auto e=findCreature(id);if(!catalog.permits(e->species(),e->vocation()))
            throw std::invalid_argument("This change would invalidate an existing creature's species or vocation.");
    }
    Operation command(*this);
    if(impl_->pending && !impl_->pending->catalogBefore)impl_->pending->catalogBefore=impl_->catalog;
    const auto previous=impl_->catalog;impl_->catalog=std::move(catalog);
    for(auto id:impl_->creatureIds) {
        auto e=findCreature(id);const auto s=static_cast<std::uint32_t>(e->species().species);
        if(previous.species.at(s).resources!=impl_->catalog.species.at(s).resources || previous.species.at(s).name!=impl_->catalog.species.at(s).name)
            setCreatureIdentity(id,e->species(),e->vocation());
    }
}
Guid SceneWorld::spawnCreature(CellPosition cell,SpeciesComponent species,VocationComponent vocation,ControlOwnership control) {
    if(!impl_->catalog.permits(species,vocation))throw std::invalid_argument("Vocation is not permitted for this species.");
    const auto& definition=impl_->catalog.species.at(static_cast<std::uint32_t>(species.species));
    if(!isWalkable(cell))throw std::invalid_argument("Creature spawn cell is blocked.");
    const auto bounds=worldBounds();
    Entity draft(definition.name,ResourcePool(definition.resources.at(Resource::Health)),
        Position(cellCenter(cell),cellSize(),{bounds.x,bounds.y}),control);
    for(const auto &[key,maximum]:definition.resources)draft.setResource(key,ResourcePool(maximum));
    if(impl_->used.size()>=maxGuidHistory)throw std::length_error("GUID history full.");
    Operation command(*this);touchCreature(draft.id());
    installCreature({draft.id(),draft.type(),control,draft.position(),species,vocation,{},definition.defaultDisposition},draft.resources());
    return draft.id();
}

void SceneWorld::setCreatureDisposition(Guid id,Disposition disposition) {
    dispositionName(disposition);
    const auto actor=findCreature(id);
    if(!actor)throw std::invalid_argument("Creature not found.");
    if(actor->disposition()==disposition)return;
    Operation command(*this);touchCreature(id);
    impl_->entity(id).set<DispositionComponent>({disposition});
}
SceneWorld::AttackCheck SceneWorld::canAttack(Guid actorId,Guid targetId,bool requireAdjacent) const {
    const auto actor=findCreature(actorId),target=findCreature(targetId);
    if(!actor)return {false,"entity_removed"};
    if(actor->health().depleted())return {false,"actor_dead"};
    if(actorId==targetId)return {false,"self_target"};
    if(!target)return {false,"target_removed"};
    if(target->health().depleted())return {false,"target_dead"};
    const auto a=actor->position().cellCoordinates(),b=target->position().cellCoordinates();
    if(requireAdjacent && std::abs(a.x-b.x)+std::abs(a.y-b.y)!=1)return {false,"not_adjacent"};
    if(requireAdjacent && !hasLineOfSight(a,b,true))return {false,"blocked"};
    return {true,"ready"};
}
SceneWorld::AttackCheck SceneWorld::attack(Guid actor,Guid target) {
    const auto check=canAttack(actor,target);
    if(!check.allowed)return check;
    adjustResource(target,Resource::Health,-attackDamage);
    return {true,resource(target,Resource::Health)->depleted()?"target_defeated":"hit"};
}
bool SceneWorld::isEnemy(Guid actorId,Guid targetId) const {
    const auto actor=findCreature(actorId),target=findCreature(targetId);
    if(!actor || !target || actorId==targetId || actor->health().depleted() || target->health().depleted())return false;
    if(actor->control()==ControlOwnership::Player && target->control()==ControlOwnership::Player)return false;
    if(actor->disposition()==Disposition::Hostile)return target->disposition()!=Disposition::Hostile;
    return target->disposition()==Disposition::Hostile;
}
std::vector<CellPosition> SceneWorld::attackPath(Guid actor,Guid target) const {
    if(!canAttack(actor,target,false).allowed)return {};
    const auto position=findCreature(target)->position().cellCoordinates();
    std::vector<CellPosition> best;
    for(auto cell:neighbors(position)) {
        if(!hasLineOfSight(cell,position,true))continue;
        auto path=creaturePath(actor,cell);
        if(!path.empty() && (best.empty() || path.size()<best.size()))best=std::move(path);
    }
    return best;
}
std::vector<Guid> SceneWorld::nearbyCreatures(Guid actor,int radius) const {
    if(radius<0 || radius>128)throw std::invalid_argument("Invalid creature perception radius.");
    const auto e=findCreature(actor);if(!e)return {};
    const auto center=e->position().cellCoordinates();
    std::vector<Guid> found;
    for(int cy=std::max(0,center.y-radius)/chunkSize;cy<=std::min(height()-1,center.y+radius)/chunkSize;++cy)
        for(int cx=std::max(0,center.x-radius)/chunkSize;cx<=std::min(width()-1,center.x+radius)/chunkSize;++cx)
            for(auto id:creaturesInChunk({cx,cy})) {
                const auto other=findCreature(id);const auto p=other->position().cellCoordinates();
                if(id!=actor && std::abs(p.x-center.x)+std::abs(p.y-center.y)<=radius && hasLineOfSight(center,p,true))found.push_back(id);
            }
    std::sort(found.begin(),found.end(),[&](Guid a,Guid b) {
        const auto pa=findCreature(a)->position().cellCoordinates(),pb=findCreature(b)->position().cellCoordinates();
        const int da=std::abs(pa.x-center.x)+std::abs(pa.y-center.y),db=std::abs(pb.x-center.x)+std::abs(pb.y-center.y);
        return da==db?a.bytes<b.bytes:da<db;
    });
    return found;
}
std::optional<scene::Harvestable> SceneWorld::harvestable(Guid id) const {
    auto e=impl_->entity(id);
    return e && e.has<Harvestable>()?std::optional(e.get<Harvestable>()):std::nullopt;
}
void SceneWorld::touchHarvestable(Guid id) {
    if (impl_->pending) impl_->pending->harvestBefore.try_emplace(id,harvestable(id));
}
void SceneWorld::setHarvestable(Guid id,const std::string &key) {
    const auto object=findObject(id);
    if (!object || object->type()!=ObjectType::Gatherable || !impl_->catalog.harvestables.contains(key))
        throw std::invalid_argument("Harvestables require a Gatherable object and a valid definition.");
    auto pools=resources(id).value_or(Resource{});
    pools.set(Harvestable::Units,ResourcePool(static_cast<float>(impl_->catalog.harvestables.at(key).units)));
    pools.set(Resource::Health,ResourcePool(100));
    Operation command(*this);touchHarvestable(id);touchResources(id);
    const auto cell=*objectPosition(id);const auto previous=harvestable(id);
    const auto tile=previous?previous->sourceTile:at(cell).tile(GridLayer::Objects);
    touch(cell);impl_->entity(id).set<Harvestable>({key,0,tile}).set<Resource>(pools).set<TileSprite>({tile});
    impl_->terrain.cells_[index(cell)].tile(GridLayer::Objects)=tile;
}
void SceneWorld::removeHarvestable(Guid id) {
    if (!harvestable(id)) return;
    Operation command(*this);touchHarvestable(id);
    const auto tile=harvestable(id)->sourceTile;const auto cell=*objectPosition(id);touch(cell);
    impl_->entity(id).set<TileSprite>({tile});impl_->terrain.cells_[index(cell)].tile(GridLayer::Objects)=tile;
    impl_->entity(id).remove<Harvestable>();removeResource(id,Harvestable::Units);
}
scene::HarvestCheck SceneWorld::canHarvest(Guid actorId,Guid target,bool adjacent) const {
    const auto actor=findCreature(actorId);
    if (!actor) return {false,"actor_removed"};
    if (actor->health().depleted()) return {false,"actor_dead"};
    const auto h=harvestable(target);
    if (!h) return {false,"target_removed"};
    const auto &d=impl_->catalog.harvestables.at(h->definitionId);
    const auto pool=resource(target,Harvestable::Units);
    if (!pool || pool->depleted()) return {false,"depleted"};
    if (adjacent) {
        const auto position=objectPosition(target);
        const auto a=actor->position().cellCoordinates();
        if (!position || std::abs(a.x-position->x)+std::abs(a.y-position->y)!=1)
            return {false,"not_adjacent"};
    }
    if (!d.requiredTool.empty() && std::none_of(actor->inventory().begin(),actor->inventory().end(),
        [&](const Item &item){return item.definitionId==d.requiredTool;})) return {false,"missing_tool"};

    return {true,"ready"};
}
scene::HarvestCheck SceneWorld::completeHarvest(Guid actor,Guid target) {
    const auto check=canHarvest(actor,target);
    if (!check.allowed) return check;
    const auto d=impl_->catalog.harvestables.at(harvestable(target)->definitionId);
    const auto units=*resource(target,Harvestable::Units);
    const auto health=resource(target,Resource::Health).value_or(ResourcePool(100,100*units.fraction()));
    const float remaining=std::max(0.0f,units.current()-1);
    // Integer units keep threshold crossings stable through save/load and competing workers.
    const auto crossed=[&](float current){return 4-static_cast<int>(std::ceil(4.0*current/units.maximum()));};
    const int bursts=crossed(remaining)-crossed(units.current());
    const std::size_t count=bursts*d.yields.size();
    std::vector<CellPosition> cells;
    const auto center=*objectPosition(target);
    for(int y=-4;y<=4;++y)for(int x=-4;x<=4;++x) {
        const CellPosition cell{center.x+x,center.y+y};
        if((x || y) && isWalkable(cell) && !objectAt(cell) && !creatureAt(cell))cells.push_back(cell);
    }
    if(cells.size()<count)return {false,"no_drop_space"};
    if(impl_->used.size()+2*count>maxGuidHistory)return {false,"identity_limit"};
    std::shuffle(cells.begin(),cells.end(),harvestRandom());
    Operation command(*this);
    std::size_t slot=0;
    for(int burst=0;burst<bursts;++burst)for(auto item:d.yields) {
        item.quantity=std::uniform_int_distribution<std::uint32_t>(1,item.quantity)(harvestRandom());
        const auto cell=cells[slot++];
        auto tile=at(cell).tile(GridLayer::Ground);
        if(!tile.present())tile=harvestable(target)->sourceTile;
        const auto drop=placeObject(cell,tile,ObjectType::Gatherable);
        addItem(drop,item);
    }
    setResource(target,Resource::Health,ResourcePool(health.maximum(),health.maximum()*remaining/units.maximum()));
    trySpendResource(target,Harvestable::Units,1);
    if (resource(target,Harvestable::Units)->depleted() && d.removeWhenDepleted && inventory(target)->empty())
        removeObjectById(target); // Never destroy separately carried/stored loot.
    return {true,"harvested"};
}
std::vector<CellPosition> SceneWorld::harvestPath(Guid actor,Guid target) const {
    const auto check=canHarvest(actor,target,false);
    if (!check.allowed) return {};
    const auto position=objectPosition(target);
    std::vector<CellPosition> best;
    for (auto cell:neighbors(*position,false,false)) {
        auto path=creaturePath(actor,cell);
        if (!path.empty() && (best.empty() || path.size()<best.size())) best=std::move(path);
    }
    return best;
}
std::vector<Guid> SceneWorld::nearbyHarvestables(Guid actor,int radius) const {
    if (radius<0 || radius>128) throw std::invalid_argument("Invalid harvest perception radius.");
    const auto e=findCreature(actor);if (!e) return {};
    const auto center=e->position().cellCoordinates();
    std::vector<Guid> found;
    for (int cy=std::max(0,center.y-radius)/chunkSize;cy<=std::min(height()-1,center.y+radius)/chunkSize;++cy)
        for (int cx=std::max(0,center.x-radius)/chunkSize;cx<=std::min(width()-1,center.x+radius)/chunkSize;++cx)
            for (auto id:objectsInChunk({cx,cy})) if (harvestable(id)) {
                const auto p=*objectPosition(id);
                if (std::abs(p.x-center.x)+std::abs(p.y-center.y)<=radius) found.push_back(id);
            }
    std::sort(found.begin(),found.end(),[&](Guid a,Guid b) {
        const auto pa=*objectPosition(a),pb=*objectPosition(b);
        const int da=std::abs(pa.x-center.x)+std::abs(pa.y-center.y),db=std::abs(pb.x-center.x)+std::abs(pb.y-center.y);
        return da==db?a.bytes<b.bytes:da<db;
    });
    return found;
}
std::optional<Guid> SceneWorld::createCarcass(Guid id) {
    const auto actor=findCreature(id);
    if (!actor || !actor->health().depleted()) return {};
    const auto &species=impl_->catalog.species.at(static_cast<std::uint32_t>(actor->species().species));
    if (species.carcassHarvest.empty()) return {};
    auto cell=actor->position().cellCoordinates();
    if (objectAt(cell)) {
        const auto adjacent=neighbors(cell,false,true);
        if (adjacent.empty()) return {}; // Keep the dead creature inert until space becomes available.
        cell=adjacent.front();
    }
    if (impl_->used.size()>=maxGuidHistory) return {};
    std::unordered_map<Guid,Resource,GuidHash> retained;
    for (const auto &item:actor->inventory()) if (auto value=resources(item.guid)) retained.emplace(item.guid,*value);
    Operation command(*this);touchCreature(id);touch(cell);
    const auto corpseId=impl_->allocate();
    Object corpse;corpse.id_=corpseId;corpse.type_=ObjectType::Gatherable;corpse.contents_=actor->inventory();
    eraseCreature(id);
    install(cell,corpse,species.carcassTile);
    for (const auto &[child,value]:retained) impl_->entity(child).set<Resource>(value);
    setHarvestable(corpseId,species.carcassHarvest);
    return corpseId;
}
void SceneWorld::advanceHarvestLifecycle(double seconds) {
    std::vector<Guid> creatures;
    impl_->ecs.each([&](const PersistentId &id,const Creature &,const Resource &r) {
        if (r.find(Resource::Health)->depleted()) creatures.push_back(id.value);
    });
    for (auto id:creatures) createCarcass(id);
    std::vector<Guid> nodes;
    impl_->ecs.each([&](const PersistentId &id,const Harvestable &h,const Resource &r) {
        if (r.find(Harvestable::Units)->depleted() && impl_->catalog.harvestables.at(h.definitionId).regenerationSeconds>0)
            nodes.push_back(id.value);
    });
    for (auto id:nodes) {
        auto h=*harvestable(id);
        auto pool=*resource(id,Harvestable::Units);
        const auto &d=impl_->catalog.harvestables.at(h.definitionId);
        if (!pool.depleted() || d.regenerationSeconds<=0) continue;
        h.regenerationRemaining=std::max(0.0,h.regenerationRemaining-seconds);
        if (h.regenerationRemaining<=1e-9) {
            Operation command(*this);touchHarvestable(id);
            pool.refill();setResource(id,Harvestable::Units,pool);
        } else impl_->entity(id).set<Harvestable>(h);
    }
}

void SceneWorld::collectGatherables() {
    // Only inspect the four cells adjacent to living creatures, not every object in the map.
    std::unordered_set<Guid,GuidHash> nearby;
    for(auto id:creatureIds()) {
        const auto actor=findCreature(id);
        if(actor->health().depleted())continue;
        for(auto cell:neighbors(actor->position().cellCoordinates()))if(auto object=objectAt(cell))
            if(object->type()==ObjectType::Gatherable && !harvestable(object->id()) && !object->contents().empty())nearby.insert(object->id());
    }
    for(auto id:nearby) {
        const auto object=findObject(id);if(!object)continue;
        const auto cell=*objectPosition(id);
        struct PickupStack { Item item; std::optional<Resource> resource; };
        const auto planPickup=[&](const std::vector<Item> &inventory) {
            std::vector<PickupStack> plan;
            for(const auto &item:inventory)plan.push_back({item,resources(item.guid)});
            for(auto item:object->contents()) {
                const auto resource=resources(item.guid);
                for(auto &target:plan) {
                    if(target.item.definitionId!=item.definitionId || target.item.displayName!=item.displayName ||
                       target.resource!=resource || target.item.quantity>=Inventory::gatherableStackLimit)continue;
                    const auto moved=std::min(item.quantity,Inventory::gatherableStackLimit-target.item.quantity);
                    target.item.quantity+=moved;item.quantity-=moved;
                    if(!item.quantity)break;
                }
                while(item.quantity) {
                    if(plan.size()==Inventory::maxStacks)return std::vector<PickupStack>{};
                    auto part=item;part.quantity=std::min(item.quantity,Inventory::gatherableStackLimit);
                    plan.push_back({part,resource});item.quantity-=part.quantity;item.guid={};
                }
            }
            return plan;
        };
        std::vector<Guid> players,others;
        for(auto adjacent:neighbors(cell))if(auto occupant=creatureAt(adjacent)) {
            const auto actor=findCreature(*occupant);
            if(actor->health().depleted() || planPickup(actor->inventory()).empty())continue;
            (actor->control()==ControlOwnership::Player?players:others).push_back(*occupant);
        }
        const auto &eligible=players.empty()?others:players;
        if(eligible.empty())continue;
        const auto recipient=eligible[std::uniform_int_distribution<std::size_t>(0,eligible.size()-1)(harvestRandom())];
        auto plan=planPickup(*inventory(recipient));
        const auto newStacks=std::count_if(plan.begin(),plan.end(),[](const auto &entry){return entry.item.guid.empty();});
        if(impl_->used.size()+newStacks>maxGuidHistory)continue;
        Operation command(*this);touch(cell);touchCreature(recipient);
        std::uint32_t order=0;
        for(auto &entry:plan) {
            auto &item=entry.item;
            if(item.guid.empty())item.guid=impl_->allocate();
            touchResources(item.guid);
            auto child=impl_->entity(item.guid);
            if(!child) {
                child=impl_->create(item.guid);
                const auto definition=impl_->itemKeys.find(item.definitionId);
                if(definition!=impl_->itemKeys.end())child.is_a(impl_->entity(definition->second));
            }
            child.set<ItemStack>({item.definitionId,item.displayName,item.quantity})
                 .add<ContainedBy>(impl_->entity(recipient)).set<ItemOrder>({order++});
            if(entry.resource)child.set<Resource>(*entry.resource);
        }
        removeObjectById(id); // Also retires source stacks that were fully merged.
    }
}
