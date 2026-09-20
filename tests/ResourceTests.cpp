#include "MapStorage.h"
#include "PlaySession.h"
#include "SceneComponents.h"
#include "SceneWorld.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace scene;
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
    check(rejected, "Invalid resource accepted");
}
int main(int argc, char **argv) {
    try {
        ResourcePool pool(50);
        check(pool.current() == 50 && pool.full(), "Pools start full");
        check(pool.adjust(-70) == -50 && pool.depleted(), "Drain clamps to zero");
        check(pool.adjust(200) == 50 && pool.fraction() == 1, "Restore clamps to maximum");
        check(!pool.trySpend(51) && pool.current() == 50, "Unaffordable spending is atomic");
        check(pool.trySpend(12.5f) && pool.current() == 37.5f, "Fractional resource costs");
        pool.setMaximum(20);
        check(pool.current() == 20, "Lowering capacity clamps existing contents");
        pool.setMaximum(80);
        check(pool.current() == 20, "Raising capacity does not refill");
        pool.refill();
        pool.empty();
        check(pool.current() == 0, "Refill and empty");
        ResourcePool zero(0);
        check(zero.fraction() == 0 && zero.depleted(), "Zero capacity is well-defined");
        rejects([&] { ResourcePool bad(-1); });
        rejects([&] { ResourcePool bad(5, 6); });
        rejects([&] { pool.adjust(std::numeric_limits<float>::infinity()); });
        rejects([&] { pool.trySpend(-1); });
        rejects([&] { pool.setMaximum(std::numeric_limits<float>::quiet_NaN()); });
        check(pool.maximum() == 80, "Invalid changes leave pool intact");
        Resource names;
        names.set(Resource::Health, ResourcePool(100));
        names.set("fuel", ResourcePool(20));
        check(names.pools().size() == 2, "Custom simultaneous resources");
        rejects([&] { names.set(" ", pool); });
        for (int i = 2; i < 64; ++i)
            names.set(std::to_string(i), pool);
        rejects([&] { names.set("overflow", pool); });
        {
            flecs::world ecs;
            registerComponents(ecs);
            Resource defaults;
            defaults.set("health", ResourcePool(100));
            auto prefab = ecs.prefab().set<Resource>(defaults);
            auto a = ecs.entity().is_a(prefab), b = ecs.entity().is_a(prefab);
            auto changed = a.get<Resource>();
            changed.adjust("health", -10);
            a.set<Resource>(changed);
            check(a.owns<Resource>() && b.owns<Resource>() &&
                      b.get<Resource>().find("health")->current() == 100 &&
                      prefab.get<Resource>().find("health")->current() == 100,
                  "Prefab resource state is copied and independently owned");
        }
        SceneWorld world(3, 1);
        world.setHistoryEnabled(true);
        const auto owner = world.placeObject({0, 0}, {0, 0, 0}, ObjectType::Container);
        world.addItem(owner, {"wand", "Wand", 1});
        const auto item = world.findObject(owner)->contents()[0].guid;
        world.setResource(owner, Resource::Health, ResourcePool(100, 70));
        world.setResource(owner, Resource::Stamina, ResourcePool(80));
        world.setResource(item, Resource::Mana, ResourcePool(10));
        check(world.undo() && !world.resource(item, Resource::Mana), "Component creation undo");
        world.redo();
        check(world.trySpendResource(item, Resource::Mana, 4) &&
                  world.resource(item, Resource::Mana)->current() == 6,
              "Item entities can own resources");
        world.undo();
        check(world.resource(item, Resource::Mana)->current() == 10, "Spending undo");
        world.redo();
        check(!world.trySpendResource(item, Resource::Mana, 20) &&
                  world.resource(item, Resource::Mana)->current() == 6,
              "Failed command leaves state intact");
        rejects([&] { world.setResource(Guid{}, "health", pool); });
        world.setObjectOpen(owner, true);
        world.updateItem(owner, 0, {"wand", "Renamed", 1});
        check(world.resource(owner, "health")->current() == 70 &&
                  world.resource(item, "mana")->current() == 6,
              "Existing object/contents commands preserve components");
        world.moveObjectById(owner, {1, 0});
        world.undo();
        world.redo();
        check(world.resource(item, "mana")->current() == 6, "Moving and undo preserve child resources");
        world.removeObjectById(owner);
        check(!world.resources(owner) && !world.resources(item), "Deletion removes resources");
        world.undo();
        check(world.resource(owner, "health")->current() == 70 &&
                  world.resource(item, "mana")->current() == 6,
              "Deletion undo restores resources");
        world.removeResource(owner, "stamina");
        world.undo();
        check(world.resource(owner, "stamina")->maximum() == 80, "Removing individual pool can be undone");
        world.clearHistory();
        world.markSaved();
        world.adjustResource(owner, "health", 0);
        world.setObjectOpen(owner, true);
        check(!world.canUndo() && !world.dirty(),
              "Unchanged resource/object edits do not create undo entries");
        bool shown = false;
        for (const auto &field : world.componentNames(owner))
            if (field == "Resource")
                shown = true;
        check(shown, "Resource is registered in component inspector");
        PlaySession play;
        play.start(world);
        play.world().adjustResource(owner, "health", -50);
        check(world.resource(owner, "health")->current() == 70, "Play resource pools are independent");
        play.stop(world);
        auto doc = world.document();
        doc.resources.emplace(GenerateUniqueGuid(), names);
        rejects([&] { SceneWorld::fromDocument(doc); });
        if (argc > 1) {
            saveMap(argv[1], world, {{"test.png", 8, 8}});
            auto loaded = loadMap(argv[1], {{"test.png", 8, 8}});
            check(loaded.resources(owner) == world.resources(owner) &&
                      loaded.resources(item) == world.resources(item),
                  "Resource save round trip");
            // Corrupt the final float to NaN; a failed load cannot replace the live world.
            std::fstream file(argv[1], std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(-4, std::ios::end);
            const unsigned char nan[]{0, 0, 0xc0, 0x7f};
            file.write(reinterpret_cast<const char *>(nan), 4);
            file.close();
            rejects([&] { loaded = loadMap(argv[1], {{"test.png", 8, 8}}); });
            check(loaded.resources(owner) == world.resources(owner),
                  "Malformed resource leaves active world unchanged");
        }
        std::cout << "Resource pool and ECS integration tests passed.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
