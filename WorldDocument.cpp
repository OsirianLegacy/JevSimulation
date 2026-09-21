#include "WorldDocument.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

WorldDocument::WorldDocument(int width, int height, float cellSize, Vector2 origin)
    : width_(width), height_(height), cellSize_(cellSize), origin_(origin) {
    if (width <= 0 || height <= 0 || !std::isfinite(cellSize) || cellSize <= 0 || !std::isfinite(origin.x) ||
        !std::isfinite(origin.y) || !std::isfinite(origin.x + width * cellSize) ||
        !std::isfinite(origin.y + height * cellSize)) {
        throw std::invalid_argument("Grid dimensions and cell size must be positive and finite.");
    }
    const auto size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (size / static_cast<std::size_t>(width) != static_cast<std::size_t>(height)) {
        throw std::length_error("Grid cell count overflow.");
    }
    cells_.resize(size);
}

bool WorldDocument::contains(CellPosition c) const {
    return c.x >= 0 && c.y >= 0 && c.x < width_ && c.y < height_;
}

std::size_t WorldDocument::index(CellPosition c) const {
    if (!contains(c))
        throw std::out_of_range("Cell outside grid.");
    return static_cast<std::size_t>(c.y) * width_ + c.x;
}

CellPosition WorldDocument::coordinates(std::size_t i) const {
    if (i >= count())
        throw std::out_of_range("Grid index out of range.");
    return {static_cast<int>(i % width_), static_cast<int>(i / width_)};
}

const GridCell &WorldDocument::at(CellPosition c) const {
    return cells_[index(c)];
}
const GridCell *WorldDocument::tryGet(CellPosition c) const {
    return contains(c) ? &at(c) : nullptr;
}

void WorldDocument::paint(CellPosition c, GridLayer layer, TileRef tile) {
    if (!tile.present() || tile.column < 0 || tile.row < 0)
        throw std::invalid_argument("Invalid painted tile.");
    if (layer == GridLayer::Objects) {
        placeObject(c, tile, ObjectType::Container);
        return;
    }
    cells_[index(c)].tile(layer) = tile;
}

const Object *WorldDocument::findObject(Guid id) const {
    const auto found = objects_.find(id);
    return found == objects_.end() ? nullptr : &found->second;
}
Guid WorldDocument::allocateGuid() {
    if (usedGuids_.size() >= maxGuidHistory)
        throw std::length_error("World GUID history is full.");
    Guid id;
    do {
        id = GenerateUniqueGuid();
    } while (usedGuids_.contains(id));
    usedGuids_.insert(id);
    return id;
}
void WorldDocument::createItemDefinition(ItemDefinition definition) {
    Object::validate(ObjectType::Container, false, {{definition.id, definition.displayName, 1}});
    if (itemDefinitions_.contains(definition.id))
        throw std::invalid_argument("An item with this ID already exists.");
    if (itemDefinitions_.size() >= maxDefinitions)
        throw std::invalid_argument("Item catalog is full.");
    definition.guid = allocateGuid();
    const auto id = definition.id;
    itemDefinitions_.emplace(id, std::move(definition));
}
void WorldDocument::createObjectType(ObjectTypeDefinition definition) {
    Object::validate(ObjectType::Container, false, {{definition.id, definition.displayName, 1}});
    Object::validate(definition.behavior, false, {});
    if (objectTypes_.contains(definition.id))
        throw std::invalid_argument("An object type with this ID already exists.");
    if (objectTypes_.size() >= maxDefinitions)
        throw std::invalid_argument("Object type catalog is full.");
    definition.guid = allocateGuid();
    const auto id = definition.id;
    objectTypes_.emplace(id, std::move(definition));
}
Guid WorldDocument::placeObject(CellPosition cell, TileRef tile, const std::string &definitionId) {
    const auto &definition = objectTypes_.at(definitionId);
    if (const auto *existing = objectAt(cell))
        return existing->id();
    const auto id = placeObject(cell, tile, definition.behavior);
    bindObjectType(id, definitionId);
    return id;
}
void WorldDocument::bindObjectType(Guid id, const std::string &definitionId) {
    const auto &definition = objectTypes_.at(definitionId);
    auto &object = objects_.at(id);
    if (object.type() != definition.behavior || !object.typeDefinitionId_.empty())
        throw std::invalid_argument("Object type binding is incompatible or already set.");
    object.typeDefinitionId_ = definitionId;
}
const Object *WorldDocument::objectAt(CellPosition c) const {
    const auto *cell = tryGet(c);
    return cell && !cell->objectId.empty() ? findObject(cell->objectId) : nullptr;
}
Guid WorldDocument::placeObject(CellPosition c, TileRef tile, ObjectType type) {
    const auto &cell = at(c);
    Object::validate(type, false, {});
    if (!tile.present() || tile.column < 0 || tile.row < 0)
        throw std::invalid_argument("Invalid object tile.");
    if (!cell.objectId.empty())
        return cell.objectId;
    const auto id = GenerateUniqueGuid();
    restoreObject(c, tile, id, type, false, {});
    return id;
}
void WorldDocument::restoreObject(CellPosition c, TileRef tile, Guid id, ObjectType type, bool open,
                                  std::vector<Item> contents) {
    auto &cell = cells_[index(c)];
    Object::validate(type, open, contents);
    if (id.empty() || usedGuids_.contains(id) || !cell.objectId.empty() ||
        cell.tile(GridLayer::Objects).present() || !tile.present() || tile.column < 0 || tile.row < 0)
        throw std::invalid_argument("Invalid object placement or GUID.");
    std::unordered_set<Guid, GuidHash> incoming{id};
    for (const auto &item : contents)
        if (!item.guid.empty()) {
            if (usedGuids_.contains(item.guid) || !incoming.insert(item.guid).second)
                throw std::invalid_argument("Item GUID has already been used.");
        }
    if (usedGuids_.size() + contents.size() + 1 > maxGuidHistory)
        throw std::length_error("World GUID history is full.");
    for (auto guid : incoming) {
        GlobalGuidRegistry().reserve(guid);
        usedGuids_.insert(guid);
    }
    for (auto &item : contents)
        if (item.guid.empty())
            item.guid = allocateGuid();
    Object object;
    object.id_ = id;
    object.type_ = type;
    object.isOpen_ = open;
    object.contents_ = std::move(contents);
    objects_.emplace(id, std::move(object));
    cell.objectId = id;
    cell.tile(GridLayer::Objects) = tile;
}

GuidState WorldDocument::guidState() const {
    GuidState state;
    for (const auto &[key, item] : itemDefinitions_)
        state.items.emplace(key, item.guid);
    for (const auto &[key, type] : objectTypes_)
        state.types.emplace(key, type.guid);
    for (const auto &[id, object] : objects_) {
        auto &ids = state.contents[id];
        for (const auto &item : object.contents())
            ids.push_back(item.guid);
    }
    state.used = usedGuids_;
    return state;
}
void WorldDocument::validateGuidState(const GuidState &state) const {
    if (state.items.size() != itemDefinitions_.size() || state.types.size() != objectTypes_.size() ||
        state.contents.size() != objects_.size() || state.used.size() > maxGuidHistory ||
        state.used.contains(Guid{}))
        throw std::invalid_argument("GUID metadata does not match world data.");
    std::unordered_set<Guid, GuidHash> active;
    const auto claim = [&](Guid id) {
        if (id.empty() || !state.used.contains(id) || !active.insert(id).second)
            throw std::invalid_argument("Missing, empty, or duplicate GUID across world systems.");
    };
    for (const auto &creature : creatures) claim(creature.id);
    for (const auto &[key, item] : itemDefinitions_)
        claim(state.items.at(key));
    for (const auto &[key, type] : objectTypes_)
        claim(state.types.at(key));
    for (const auto &[id, object] : objects_) {
        claim(id);
        const auto &items = state.contents.at(id);
        if (items.size() != object.contents().size())
            throw std::invalid_argument("Item GUID count mismatch.");
        for (auto item : items)
            claim(item);
    }
}
void WorldDocument::restoreGuidState(GuidState state) {
    validateGuidState(state);
    for (auto id : state.used)
        GlobalGuidRegistry().reserve(id);
    for (auto &[key, item] : itemDefinitions_)
        item.guid = state.items.at(key);
    for (auto &[key, type] : objectTypes_)
        type.guid = state.types.at(key);
    for (auto &[id, object] : objects_) {
        const auto &items = state.contents.at(id);
        for (std::size_t i = 0; i < items.size(); ++i)
            object.contents_[i].guid = items[i];
    }
    usedGuids_ = std::move(state.used);
}

Rectangle WorldDocument::worldBounds() const {
    return {origin_.x, origin_.y, width_ * cellSize_, height_ * cellSize_};
}
