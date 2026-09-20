#include "SceneWorld.h"
#include "SceneComponents.h"
#include "TilesetLibrary.h"
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

using namespace scene;
namespace {
struct CellSnapshot {
    GridCell cell;
    std::optional<Object> object;
    bool operator==(const CellSnapshot &) const = default;
};
struct Delta {
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
    std::unordered_map<Guid, flecs::entity_t, GuidHash> entities;
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
void SceneWorld::draw(Rectangle view, const TilesetLibrary *library) const {
    impl_->terrain.draw(view, library);
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
    std::uint32_t order = 0;
    for (const auto &item : object.contents()) {
        auto child = impl_->create(item.guid)
                         .set<ItemStack>({item.definitionId, item.displayName, item.quantity})
                         .set<ItemOrder>({order++})
                         .add<ContainedBy>(entity);
        const auto definition = impl_->itemKeys.find(item.definitionId);
        if (definition != impl_->itemKeys.end())
            child.is_a(impl_->entity(definition->second));
    }
    auto &cell = impl_->terrain.cells_[index(c)];
    cell.objectId = object.id();
    cell.tile(GridLayer::Objects) = tile; // Derived render/spatial cache.
}
void SceneWorld::eraseInstance(Guid id) {
    touchResources(id);
    auto entity = impl_->entity(id);
    if (!entity)
        return;
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
    if (id.empty() || hasUsedGuid(id) || !cell.objectId.empty() || !tile.present() || tile.column < 0 ||
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
    if (objectAt(to))
        return false;
    Operation command(*this);
    touch(from);
    touch(to);
    auto &source = impl_->terrain.cells_[index(from)];
    auto &target = impl_->terrain.cells_[index(to)];
    impl_->entity(source.objectId).set<GridPosition>({to});
    target.objectId = source.objectId;
    target.tile(GridLayer::Objects) = source.tile(GridLayer::Objects);
    source.objectId = {};
    source.tile(GridLayer::Objects) = {};
    return true;
}
void SceneWorld::updateObject(Guid id, bool open, std::vector<Item> items) {
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
    const auto object = findObject(id);
    if (!object)
        throw std::out_of_range("Object not found.");
    auto items = object->contents();
    item.guid = {};
    items.push_back(item);
    updateObject(id, object->isOpen(), std::move(items));
}
void SceneWorld::updateItem(Guid id, std::size_t position, Item item) {
    const auto object = findObject(id);
    if (!object)
        throw std::out_of_range("Object not found.");
    auto items = object->contents();
    const auto old = items.at(position).guid;
    if (!item.guid.empty() && item.guid != old)
        throw std::invalid_argument("Cannot change GUID.");
    item.guid = old;
    items[position] = item;
    updateObject(id, object->isOpen(), std::move(items));
}
void SceneWorld::removeItem(Guid id, std::size_t position) {
    const auto object = findObject(id);
    if (!object)
        throw std::out_of_range("Object not found.");
    auto items = object->contents();
    if (position >= items.size())
        throw std::out_of_range("Item index invalid.");
    items.erase(items.begin() + position);
    updateObject(id, object->isOpen(), std::move(items));
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
    if (at(c) == value)
        return;
    if (value.tile(GridLayer::Objects).present() && impl_->used.size() >= maxGuidHistory)
        throw std::length_error("GUID history full.");
    Operation command(*this);
    touch(c);
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
    if (!cell || !cell->tile(GridLayer::Ground).present() || cell->tile(GridLayer::Walls).present() ||
        cell->tile(GridLayer::Entities).present())
        return false;
    if (cell->objectId.empty())
        return true;
    const auto entity = impl_->entity(cell->objectId);
    return entity && entity.has<DoorState>() && entity.get<DoorState>().open;
}
bool SceneWorld::blocksSight(CellPosition c) const {
    const auto *cell = tryGet(c);
    if (!cell || cell->layersBlockSight())
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
    for (const auto &[id, handle] : impl_->entities)
        if (auto value = resources(id))
            doc.resources.emplace(id, *value);
    for (const auto &[id, handle] : impl_->entities)
        if (impl_->ecs.entity(handle).has<PlacedObject>())
            doc.objects_.emplace(id, *findObject(id));
    doc.itemDefinitions_ = itemDefinitions();
    doc.objectTypes_ = objectTypes();
    doc.usedGuids_ = impl_->used;
    return doc;
}
SceneWorld SceneWorld::fromDocument(const WorldDocument &doc) {
    // Validate the complete identity table before building any runtime relationships.
    doc.validateGuidState(doc.guidState());
    SceneWorld result(doc.width_, doc.height_, doc.cellSize_, doc.origin_);
    auto &impl = *result.impl_;
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
    if (doc.resources.size() > maxGuidHistory)
        throw std::invalid_argument("Too many resource owners.");
    for (const auto &[id, value] : doc.resources) {
        auto entity = impl.entity(id);
        if (!entity || entity.has(flecs::Prefab) || value.pools().empty())
            throw std::invalid_argument("Invalid resource owner.");
        entity.set<Resource>(value);
    }
    result.reserveHistory(doc.usedGuids_);
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
    for (auto it = delta.resourcesBefore.begin(); it != delta.resourcesBefore.end();) {
        auto after = resources(it->first);
        if ((!after && !it->second) || (after == it->second && delta.before.empty()))
            it = delta.resourcesBefore.erase(it);
        else {
            delta.resourcesAfter.emplace(it->first, after);
            ++it;
        }
    }
    if (delta.before.empty() && delta.itemsBefore.empty() && delta.typesBefore.empty() &&
        delta.resourcesBefore.empty())
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
    for (const auto &[id, value] : forward ? delta.resourcesAfter : delta.resourcesBefore) {
        if (auto entity = impl_->entity(id)) {
            if (value)
                entity.set<Resource>(*value);
            else
                entity.remove<Resource>();
        }
    }
    impl_->revision = forward ? delta.afterRevision : delta.beforeRevision;
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
}
void SceneWorld::removeResource(Guid id, const std::string &key) {
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
