#include "MapStorage.h"
#include "PlaySession.h"
#include "SceneWorld.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Invalid command was accepted");
}
int main(int argc, char **argv) {
    try {
        const TileRef tile{0, 0, 0};
        {
            SceneWorld empty;
            check(empty.count() == 1'000'000 && empty.persistentEntityCount() == 0,
                  "Terrain must remain dense, without ECS entities");
            check(empty.visibleRange({0, 0, 80, 80}).maxX == 10, "Visible range remains bounded");
        }
        SceneWorld world(8, 4);
        world.setHistoryEnabled(true);
        world.beginEdit();
        for (int x = 0; x < 8; ++x)
            world.paint({x, 0}, GridLayer::Ground, tile);
        world.endEdit();
        check(world.dirty() && world.undo(), "Stroke creates one undo command");
        check(!world.at({0, 0}).tile(GridLayer::Ground).present() &&
                  !world.at({7, 0}).tile(GridLayer::Ground).present() && !world.canUndo(),
              "Complete stroke undone");
        check(world.redo() && world.isWalkable({7, 0}), "Stroke redo");
        world.createItemDefinition({"wood", "Wood"});
        const auto definition = world.itemDefinitions().at("wood").guid;
        world.markSaved();
        check(!world.dirty(), "Save revision");
        world.undo();
        check(world.dirty() && world.itemDefinitions().empty() && world.hasUsedGuid(definition),
              "Undo saved catalog creation stays dirty and reserves GUID");
        world.redo();
        check(world.itemDefinitions().at("wood").guid == definition, "Catalog redo restores identity");
        world.createObjectType({"chest", "Chest", ObjectType::Container});
        const auto a = world.placeObject({0, 0}, tile, std::string("chest"));
        const auto b = world.placeObject({1, 0}, tile, std::string("chest"));
        world.addItem(a, {"wood", "Wood", 3});
        const auto item = world.findObject(a)->contents()[0].guid;
        check(a != b && a != definition && item != a && world.findObject(b)->contents().empty(),
              "Prefab instances own identities and contents");
        const auto count = world.persistentEntityCount();
        check(world.placeObject({0, 0}, {0, 1, 1}, ObjectType::Door) == a &&
                  world.findObject(a)->contents()[0].guid == item,
              "Occupied painting preserves object and inventory");
        check(!world.moveObject({0, 0}, {1, 0}) && world.persistentEntityCount() == count,
              "Rejected move preserves world");
        rejects([&] { world.updateObject(a, false, {{"wood", "", 0}}); });
        check(world.findObject(a)->contents()[0].guid == item, "Invalid edit preserves entity");
        check(world.moveObject({0, 0}, {2, 0}), "Move succeeded");
        world.undo();
        check(world.at({0, 0}).objectId == a && world.at({2, 0}).objectId.empty(), "Move undo index");
        world.redo();
        check(world.at({2, 0}).objectId == a && world.findObject(a)->contents()[0].guid == item,
              "Move redo identity");
        world.removeObject({2, 0});
        check(!world.findObject(a) && world.persistentEntityCount() == count - 2,
              "Deletion removes owned item entities");
        world.undo();
        check(world.persistentEntityCount() == count && world.findObject(a)->contents()[0].guid == item,
              "Deletion undo restores owned entities");
        world.beginEdit();
        world.removeObject({2, 0});
        world.paint({5, 0}, GridLayer::Walls, tile);
        world.cancelEdit();
        check(world.findObject(a) && !world.at({5, 0}).tile(GridLayer::Walls).present(),
              "Cancelled command rollback");
        world.fillRegion({0, 0, 4, 1}, {});
        check(world.objectCount() == 0, "Region replacement clears owners");
        world.undo();
        check(world.objectCount() == 2 && world.persistentEntityCount() == count,
              "Region undo restores objects and items");
        world.setObjectOpen(a, true);
        world.undo();
        check(!world.findObject(a)->isOpen(), "Property undo");
        world.addItem(a, {"wood", "Second", 9});
        check(!world.canRedo(), "New command clears redo");
        const auto second = world.findObject(a)->contents()[1].guid;
        world.undo();
        check(world.hasUsedGuid(second), "Undo never releases IDs");
        world.redo();
        check(world.findObject(a)->contents()[1].guid == second, "Stack redo restores same identity");
        const auto door = world.placeObject({4, 0}, tile, ObjectType::Door);
        check(world.aStar({3, 0}, {7, 0}).empty() && !world.hasLineOfSight({3, 0}, {7, 0}),
              "Closed door blocks path and LOS");
        world.setObjectOpen(door, true);
        check(!world.aStar({3, 0}, {7, 0}).empty() && !world.breadthFirstSearch({3, 0}, {7, 0}).empty() &&
                  world.hasLineOfSight({3, 0}, {7, 0}),
              "Door state updates all queries");
        world.undo();
        check(world.blocksSight({4, 0}), "Undo immediately updates navigation");
        auto clone = SceneWorld::fromDocument(world.document());
        check(clone.guidState().used == world.guidState().used && clone.findObject(a) == world.findObject(a),
              "Snapshot preserves identities and history");
        clone.setObjectOpen(a, true);
        check(!world.findObject(a)->isOpen(), "Independent worlds have independent components");
        PlaySession play;
        play.start(world);
        check(play.world().findObject(a) == world.findObject(a), "Play includes committed unsaved contents");
        check(play.advance(1.0 / 120) == 0 && play.advance(1.0 / 120) == 1 && play.world().ticks() == 1,
              "60 Hz independent from rendering");
        play.pause(true);
        check(play.advance(10) == 0 && play.world().ticks() == 1, "Pause runs no ticks");
        play.step();
        check(play.world().ticks() == 2, "Step runs exactly one tick");
        play.world().setObjectOpen(a, true);
        const auto runtimeItemOwner = play.world().placeObject({7, 3}, tile, ObjectType::Container);
        play.world().addItem(runtimeItemOwner, {"runtime", "Runtime", 1});
        const auto runtimeItem = play.world().findObject(runtimeItemOwner)->contents()[0].guid;
        play.pause(false);
        check(play.advance(10) == 5 && play.advance(0.001) == 0,
              "Long stall caps catch-up and discards excess");
        play.stop(world);
        check(!world.findObject(a)->isOpen() && !world.findObject(runtimeItemOwner) &&
                  world.hasUsedGuid(runtimeItemOwner) && world.hasUsedGuid(runtimeItem),
              "Stop isolates content but reserves runtime GUIDs");
        play.start(world);
        check(play.world().ticks() == 0 && !play.world().canUndo(),
              "Fresh play gets fresh simulation and history");
        play.stop(world);
        if (argc > 1) {
            saveMap(argv[1], world, {{"test.png", 8, 8}});
            auto loaded = loadMap(argv[1], {{"test.png", 8, 8}});
            check(loaded.findObject(a) == world.findObject(a) &&
                      loaded.guidState().used == world.guidState().used && !loaded.canUndo(),
                  "Version 4 ECS round trip preserves ordering and retired IDs");
        }
        SceneWorld late(2, 1);
        const auto owner = late.placeObject({0, 0}, tile, ObjectType::Container);
        late.addItem(owner, {"late", "Uncatalogued", 1});
        const auto lateItem = late.findObject(owner)->contents()[0].guid;
        late.setHistoryEnabled(true);
        late.createItemDefinition({"late", "Catalogued"});
        const auto references = [&] {
            const auto fields = late.componentNames(lateItem);
            return std::find(fields.begin(), fields.end(), "Definition: late") != fields.end();
        };
        check(references(), "Late definition resolves existing stack reference");
        late.undo();
        check(!references() && late.findObject(owner)->contents()[0].guid == lateItem,
              "Definition undo preserves uncatalogued instance");
        late.redo();
        check(references(), "Definition redo restores relationship");
        check(late.moveObjectById(owner, {1, 0}) && late.objectPosition(owner) == CellPosition{1, 0},
              "GUID movement command");
        late.removeObjectById(owner);
        check(!late.objectPosition(owner) && late.persistentEntityCount() == 1,
              "GUID deletion removes contents but retains definition");
        SceneWorld bounded(1, 1);
        bounded.setHistoryEnabled(true);
        for (int i = 0; i < 105; ++i)
            bounded.paint({0, 0}, GridLayer::Ground, {0, i, 0});
        int undos = 0;
        while (bounded.undo())
            ++undos;
        check(undos == 100, "Retain latest 100 commands");
        std::cout << "ECS ownership, undo, snapshots, and fixed-step Play tests passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
