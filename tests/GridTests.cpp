#include "SceneWorld.h"
#include "GridFixtures.h"
#include "WorldCamera.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void check(bool valid, const char* message) { if (!valid) throw std::runtime_error(message); }
template<class Function> void rejects(Function function) {
    bool threw = false;
    try { function(); } catch (const std::exception&) { threw = true; }
    check(threw, "Expected an invalid operation to throw.");
}
bool near(float a, float b) { return std::abs(a - b) < 0.05f; }

int main() {
    try {
        SceneWorld world(1000, 1000); // Explicit legacy dimensions remain supported.
        check(world.cellSize() == 8 && !world.isWalkable({0, 0}) && !world.isWalkable({999, 999}), "Empty 8x8 grid is not walkable.");
        check(world.count() == 1'000'000, "Million-cell grid allocation.");
        check(world.index({999, 999}) == 999'999, "Last cell index.");
        check(world.coordinates(999'999) == CellPosition{999, 999}, "Last index conversion.");
        check(world.tryGet({-1, 0}) == nullptr && !world.isWalkable({1000, 0}), "Outside grid access.");
        rejects([&] { world.at({1000, 0}); });
        rejects([&] { world.coordinates(world.count()); });
        rejects([] { SceneWorld invalid(0, 100); });
        rejects([] { SceneWorld invalid(2, 2, 0); });
        rejects([] { SceneWorld invalid(2, 2, std::numeric_limits<float>::quiet_NaN()); });

        SceneWorld grid(4, 3, 10, {-20, -10});
        grid.fill(groundCell());
        for (std::size_t i = 0; i < grid.count(); ++i) {
            const auto cell = grid.coordinates(i);
            check(grid.index(cell) == i, "Row-major index round trip.");
            check(grid.worldToCell(grid.cellCenter(cell)) == cell, "World/cell round trip.");
        }
        check(grid.worldToCell({-20, -10}) == CellPosition{0, 0}, "Origin included.");
        check(!grid.worldToCell({-20.01f, -10}), "Negative fractional coordinates outside.");
        check(!grid.worldToCell({20, 0}) && !grid.worldToCell({0, 20}), "Maximum world edges excluded.");
        check(!grid.worldToCell({std::numeric_limits<float>::infinity(), 0}), "Infinite world coordinate rejected.");
        check(grid.cellBounds({3, 2}).x == 10, "Cell bounds conversion.");

        grid.set({1, 1}, wallCell());
        check(!grid.isWalkable({1, 1}) && grid.at({1, 1}).tile(GridLayer::Walls).column == 1, "Cell data writes.");
        check(grid.neighbors({0, 0}).size() == 2, "Corner four-way neighbors.");
        check(grid.neighbors({1, 1}, true).size() == 8, "Interior eight-way neighbors.");
        check(grid.neighbors({0, 0}, true, true).size() == 2, "Walkable neighbor filter.");
        check(grid.neighbors({-1, 0}).empty(), "Out-of-bounds neighbors.");
        grid.clear();
        grid.fill(groundCell());
        grid.fillRegion({-2, -2, 2, 2}, wallCell());
        check(!grid.isWalkable({0, 0}) && !grid.isWalkable({1, 1}) && grid.isWalkable({2, 2}), "Clipped region fill.");
        grid.fillRegion({3, 2, 1, 0}, wallCell());
        check(grid.isWalkable({2, 2}), "Reversed region is empty.");
        grid.fill(wallCell());
        grid.erase({3, 2}, GridLayer::Walls);
        check(grid.isWalkable({3, 2}) && !grid.isWalkable({0, 0}), "Fill and walkability.");
        grid.clear();
        check(grid.at({0, 0}) == GridCell{}, "Reset restores defaults.");

        const auto exact = grid.visibleRange({-20, -10, 10, 10});
        check(exact.minX == 0 && exact.maxX == 1 && exact.maxY == 1, "Exact visible boundary.");
        const auto partial = grid.visibleRange({-21, -11, 12, 12});
        check(partial.maxX == 2 && partial.maxY == 2, "Partially visible cells included.");
        check(grid.visibleRange({20, 20, 100, 100}).empty(), "Disjoint view culled.");
        check(grid.visibleRange({0, 0, 0, 10}).empty(), "Empty viewport.");
        const auto visible = world.visibleRange({3000, 3000, 1100, 700});
        check(!visible.empty() && (visible.maxX - visible.minX) * (visible.maxY - visible.minY) < 13000,
              "Viewport must not traverse the million-cell world.");

        WorldCamera camera(world.worldBounds());
        camera.resize(1100, 700);
        const auto start = camera.camera().target;
        camera.pan({1, 0}, 1);
        check(near(camera.camera().target.x - start.x, 600), "Pan speed.");
        WorldCamera diagonal(world.worldBounds());
        diagonal.resize(1100, 700);
        diagonal.pan({1, 1}, 1);
        check(near(std::hypot(diagonal.camera().target.x - start.x, diagonal.camera().target.y - start.y), 600),
              "Diagonal speed is normalized.");
        WorldCamera split(world.worldBounds());
        split.resize(1100, 700);
        for (int i = 0; i < 60; ++i) split.pan({1, 0}, 1.0f / 60);
        check(near(split.camera().target.x, camera.camera().target.x), "Frame-independent movement.");
        camera.pan({-1, -1}, 1000);
        check(near(camera.visibleBounds().x, 0) && near(camera.visibleBounds().y, 0), "Top-left camera clamp.");
        camera.pan({1, 1}, 1000);
        auto view = camera.visibleBounds();
        check(near(view.x + view.width, 8000) && near(view.y + view.height, 8000), "Bottom-right camera clamp.");
        camera.resize(64000, 64000);
        check(near(camera.camera().target.x, 4000) && near(camera.camera().target.y, 4000), "Oversized viewport centers world.");
        for (unsigned mask = 0; mask < 16; ++mask) {
            SceneWorld layered(1, 1);
            for (unsigned layer = 0; layer < 4; ++layer)
                if (mask & (1u << layer)) layered.paint({0, 0}, static_cast<GridLayer>(layer), {2, 3, 4});
            check(layered.isWalkable({0, 0}) == (mask == 1), "Walkability must derive from all four layers.");
            check(layered.blocksSight({0, 0}) == ((mask & 10) != 0), "Only walls/entities block LOS.");
            for (unsigned layer = 0; layer < 4; ++layer) layered.erase({0, 0}, static_cast<GridLayer>(layer));
            check(layered.at({0, 0}) == GridCell{} && !layered.isWalkable({0, 0}), "Layer erasure resets occupancy.");
        }
        SceneWorld layered(3, 1);
        layered.paint({1, 0}, GridLayer::Walls, {0, 1, 0});
        layered.paint({1, 0}, GridLayer::Ground, {1, 0, 0});
        check(!layered.isWalkable({1, 0}), "Ground painted under a wall cannot enable movement.");
        layered.erase({1, 0}, GridLayer::Walls);
        check(layered.isWalkable({1, 0}), "Removing wall reveals walkable ground.");
        layered.erase({1, 0}, GridLayer::Ground);
        check(!layered.isWalkable({1, 0}) && layered.hasLineOfSight({0, 0}, {2, 0}), "Missing ground blocks movement, not LOS.");
        layered.paint({1, 0}, GridLayer::Objects, {0, 0, 0});
        check(layered.hasLineOfSight({0, 0}, {2, 0}), "Objects do not block LOS.");
        layered.paint({1, 0}, GridLayer::Entities, {0, 0, 0});
        check(!layered.hasLineOfSight({0, 0}, {2, 0}), "Entities block LOS.");
        rejects([&] { layered.paint({0, 0}, GridLayer::Ground, {}); });
        rejects([&] { layered.paint({0, 0}, GridLayer::Count, {0, 0, 0}); });
        std::cout << "SceneWorld and camera checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
