#pragma once
#include "Grid.h"
#include "Resource.h"
#include <flecs.h>
#include <functional>

namespace scene {
struct PersistentId {
    Guid value;
};
struct GridPosition {
    CellPosition value;
};
struct TileSprite {
    TileRef value;
};
struct DoorState {
    bool open = false;
};
struct ContainerState {
    bool open = false;
};
struct Gatherable {};
struct Inventory {};
struct PlacedObject {};
struct ContainedBy {};
struct ItemOrder {
    std::uint32_t value = 0;
};
struct ItemStack {
    std::string definitionId, displayName;
    std::uint32_t quantity = 1;
};
struct DefinitionKey {
    std::string value;
};
struct DisplayName {
    std::string value;
};
struct ItemDefinitionTag {};
struct ObjectDefinitionTag {};
struct Behavior {
    ObjectType value = ObjectType::Container;
};
struct SimulationClock {
    std::uint64_t ticks = 0;
    double seconds = 0;
};
struct FieldSchema {
    std::string name, editor;
    bool writable = false;
    std::function<std::string(flecs::entity)> read;
};
struct ComponentSchema {
    flecs::entity_t component;
    std::string name;
    std::vector<FieldSchema> fields;
};
// The curated inspector's single registration point. Identity/composition are read-only.
inline std::vector<ComponentSchema> registerComponents(flecs::world &ecs) {
    ecs.component<Resource>().add(flecs::OnInstantiate, flecs::Override);
    ecs.component<PersistentId>().add(flecs::OnInstantiate, flecs::DontInherit);
    ecs.component<GridPosition>().add(flecs::OnInstantiate, flecs::DontInherit);
    ecs.component<TileSprite>().add(flecs::OnInstantiate, flecs::DontInherit);
    ecs.component<ItemStack>().add(flecs::OnInstantiate, flecs::DontInherit);
    ecs.component<DoorState>().member<bool>("open");
    ecs.component<ContainerState>().member<bool>("open");
    ecs.component<ContainedBy>().add(flecs::Exclusive).add(flecs::OnDeleteTarget, flecs::Delete);
    return {
        {ecs.id<Resource>(),
         "Resource",
         {{"pools", "resource-pools", false,
           [](flecs::entity e) {
               std::string text;
               for (const auto &[key, pool] : e.get<Resource>().pools()) {
                   if (!text.empty())
                       text += "; ";
                   text +=
                       key + ": " + std::to_string(pool.current()) + " / " + std::to_string(pool.maximum());
               }
               return text;
           }}}},
        {ecs.id<PersistentId>(),
         "PersistentId",
         {{"GUID", "guid", false, [](flecs::entity e) { return e.get<PersistentId>().value.toString(); }}}},
        {ecs.id<GridPosition>(),
         "GridPosition",
         {{"cell", "cell", false,
           [](flecs::entity e) {
               const auto p = e.get<GridPosition>().value;
               return std::to_string(p.x) + ", " + std::to_string(p.y);
           }}}},
        {ecs.id<TileSprite>(),
         "TileSprite",
         {{"tile", "tile", false,
           [](flecs::entity e) {
               const auto t = e.get<TileSprite>().value;
               return std::to_string(t.tileset) + ": " + std::to_string(t.column) + ", " +
                      std::to_string(t.row);
           }}}},
        {ecs.id<DoorState>(),
         "DoorState",
         {{"open", "bool", true,
           [](flecs::entity e) { return e.get<DoorState>().open ? "true" : "false"; }}}},
        {ecs.id<ContainerState>(),
         "ContainerState",
         {{"open", "bool", true,
           [](flecs::entity e) { return e.get<ContainerState>().open ? "true" : "false"; }}}},
        {ecs.id<Inventory>(),
         "Inventory",
         {{"contents", "item-list", true,
           [](flecs::entity e) {
               int count = 0;
               e.children<ContainedBy>([&](flecs::entity) { ++count; });
               return std::to_string(count) + " stacks";
           }}}},
        {ecs.id<Gatherable>(), "Gatherable", {}},
        {ecs.id<ItemStack>(),
         "ItemStack",
         {{"definitionId", "text", true, [](flecs::entity e) { return e.get<ItemStack>().definitionId; }},
          {"displayName", "text", true, [](flecs::entity e) { return e.get<ItemStack>().displayName; }},
          {"quantity", "uint", true,
           [](flecs::entity e) { return std::to_string(e.get<ItemStack>().quantity); }}}},
        {ecs.id<ItemOrder>(),
         "ItemOrder",
         {{"order", "uint", false,
           [](flecs::entity e) { return std::to_string(e.get<ItemOrder>().value); }}}},
        {ecs.id<DefinitionKey>(), "DefinitionKey", {{"key", "text", false, [](flecs::entity e) {
                                                         return e.get<DefinitionKey>().value;
                                                     }}}}};
}
} // namespace scene
