#pragma once
#include "Grid.h"
#include "Resource.h"
#include "Entity.h"
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
struct PlacedObject {};
struct Creature { std::string type; ControlOwnership control = ControlOwnership::AI; };
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
    ecs.component<HumanName>().add(flecs::OnInstantiate, flecs::DontInherit);
    ecs.component<SpeciesComponent>().add(flecs::OnInstantiate, flecs::DontInherit);
    ecs.component<DispositionComponent>().add(flecs::OnInstantiate, flecs::DontInherit);
    ecs.component<VocationComponent>().add(flecs::OnInstantiate, flecs::DontInherit);
    ecs.component<Position>().add(flecs::OnInstantiate, flecs::DontInherit);
    ecs.component<Creature>().add(flecs::OnInstantiate, flecs::DontInherit);
    ecs.component<Resource>().add(flecs::OnInstantiate, flecs::Override);
    ecs.component<PersistentId>().add(flecs::OnInstantiate, flecs::DontInherit);
    ecs.component<GridPosition>().add(flecs::OnInstantiate, flecs::DontInherit);
    ecs.component<TileSprite>().add(flecs::OnInstantiate, flecs::DontInherit);
    ecs.component<ItemStack>().add(flecs::OnInstantiate, flecs::DontInherit);
    ecs.component<Inventory>().add(flecs::OnInstantiate, flecs::DontInherit);
    ecs.component<Harvestable>().add(flecs::OnInstantiate, flecs::Override);
    ecs.component<DoorState>().member<bool>("open");
    ecs.component<ContainerState>().member<bool>("open");
    ecs.component<ContainedBy>().add(flecs::Exclusive).add(flecs::OnDeleteTarget, flecs::Delete);
    return {
        {ecs.id<DispositionComponent>(), "Disposition", {{"state", "enum", false,
            [](flecs::entity e) {return std::string(dispositionName(e.get<DispositionComponent>().value));}}}},
        {ecs.id<HumanName>(), "Human name", {
            {"first", "text", false, [](flecs::entity e) { return e.get<HumanName>().first; }},
            {"last", "text", false, [](flecs::entity e) { return e.get<HumanName>().last; }}
        }},
        {ecs.id<Harvestable>(), "Harvestable", {
            {"definition", "text", false, [](flecs::entity e) { return e.get<Harvestable>().definitionId; }},
            {"regeneration seconds", "number", false, [](flecs::entity e) { return std::to_string(e.get<Harvestable>().regenerationRemaining); }}
        }},
        {ecs.id<SpeciesComponent>(), "Species", {{"species / subspecies", "enum", false, [](flecs::entity e) {const auto s=e.get<SpeciesComponent>();return std::to_string(static_cast<std::uint32_t>(s.species))+" / "+std::to_string(static_cast<std::uint32_t>(s.subSpecies));}}}},
        {ecs.id<VocationComponent>(), "Vocation", {{"vocation", "enum", false, [](flecs::entity e) {return std::to_string(e.get<VocationComponent>().id);}}}},
        {ecs.id<Position>(), "Position", {{"location", "json", false, [](flecs::entity e) { return e.get<Position>().fetchData(); }}}},
        {ecs.id<Creature>(), "Creature", {{"type", "text", false, [](flecs::entity e) { return e.get<Creature>().type; }}}},
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
