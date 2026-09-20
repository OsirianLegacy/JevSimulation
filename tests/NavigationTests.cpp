#include "SceneWorld.h"
#include "GridFixtures.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
constexpr double infinity = std::numeric_limits<double>::infinity();

double pathCost(const SceneWorld& grid, const std::vector<CellPosition>& path,
                CellPosition start, CellPosition goal, bool diagonals, bool uniform) {
    if (path.empty()) return infinity;
    check(path.front() == start && path.back() == goal, "Path endpoints.");
    double cost = 0;
    for (std::size_t i = 0; i < path.size(); ++i) {
        check(grid.isWalkable(path[i]), "Path crosses a blocked cell.");
        if (!i) continue;
        const int dx = std::abs(path[i].x - path[i - 1].x), dy = std::abs(path[i].y - path[i - 1].y);
        check(dx <= 1 && dy <= 1 && dx + dy > 0, "Non-adjacent path step.");
        if (dx && dy) {
            check(diagonals, "Unexpected diagonal.");
            check(grid.isWalkable({path[i].x, path[i - 1].y}) && grid.isWalkable({path[i - 1].x, path[i].y}),
                  "Path cuts a blocked corner.");
        }
        cost += uniform || !(dx && dy) ? 1 : std::sqrt(2.0);
    }
    return cost;
}

// Independent O(V^2) Dijkstra reference for small test maps.
double reference(const SceneWorld& grid, CellPosition start, CellPosition goal, bool diagonals, bool uniform) {
    if (!grid.isWalkable(start) || !grid.isWalkable(goal)) return infinity;
    std::vector<double> cost(grid.count(), infinity);
    std::vector<bool> done(grid.count());
    cost[grid.index(start)] = 0;
    for (;;) {
        auto best = grid.count();
        for (std::size_t i = 0; i < grid.count(); ++i)
            if (!done[i] && cost[i] < infinity && (best == grid.count() || cost[i] < cost[best])) best = i;
        if (best == grid.count()) return infinity;
        const auto from = grid.coordinates(best);
        if (from == goal) return cost[best];
        done[best] = true;
        for (int y = 0; y < grid.height(); ++y) for (int x = 0; x < grid.width(); ++x) {
            const int dx = std::abs(x - from.x), dy = std::abs(y - from.y);
            if (dx > 1 || dy > 1 || dx + dy == 0 || (!diagonals && dx + dy > 1) || !grid.isWalkable({x, y})) continue;
            if (dx && dy && (!grid.isWalkable({x, from.y}) || !grid.isWalkable({from.x, y}))) continue;
            const double step = uniform || dx + dy == 1 ? 1 : std::sqrt(2.0);
            auto& next = cost[grid.index({x, y})];
            next = std::min(next, cost[best] + step);
        }
    }
}

// Closed-square segment intersection oracle includes every touched corner.
bool referenceLOS(const SceneWorld& grid, CellPosition from, CellPosition to) {
    if (grid.blocksSight(from) || grid.blocksSight(to)) return false;
    for (int y = 0; y < grid.height(); ++y) for (int x = 0; x < grid.width(); ++x) {
        if (!grid.blocksSight({x, y})) continue;
        double low = 0, high = 1;
        const auto clip = [&](double start, double delta, double edge) {
            if (delta == 0) return start >= edge && start <= edge + 1;
            double a = (edge - start) / delta, b = (edge + 1 - start) / delta;
            if (a > b) std::swap(a, b);
            low = std::max(low, a); high = std::min(high, b);
            return low <= high + 1e-12;
        };
        if (clip(from.x + 0.5, to.x - from.x, x) && clip(from.y + 0.5, to.y - from.y, y)) return false;
    }
    return true;
}

int main() {
    try {
        SceneWorld grid(5, 5);
        grid.fill(groundCell());
        check(grid.aStar({2, 2}, {2, 2}).size() == 1, "Same-cell A*.");
        check(grid.breadthFirstSearch({2, 2}, {2, 2}).size() == 1, "Same-cell BFS.");
        check(grid.hasLineOfSight({2, 2}, {2, 2}), "Same-cell LOS.");
        check(grid.aStar({-1, 0}, {2, 2}).empty() && grid.breadthFirstSearch({0, 0}, {5, 0}).empty(), "Invalid endpoints.");
        check(!grid.hasLineOfSight({-1, 0}, {0, 0}), "Invalid LOS endpoint.");
        grid.paint({2, 2}, GridLayer::Walls, {0, 0, 0});
        check(grid.aStar({2, 2}, {2, 2}).empty() && grid.breadthFirstSearch({2, 2}, {2, 2}).empty(), "Blocked endpoints.");
        check(!grid.hasLineOfSight({0, 2}, {4, 2}) && !grid.hasLineOfSight({2, 0}, {2, 4}), "Axial LOS obstacle.");
        grid.fillRegion({1, 0, 2, 5}, wallCell());
        check(grid.aStar({0, 0}, {4, 4}, true).empty() && grid.breadthFirstSearch({0, 0}, {4, 4}, true).empty(), "Unreachable goal.");
        grid.clear();
        grid.fill(groundCell());
        grid.paint({1, 0}, GridLayer::Walls, {0, 0, 0});
        check(!grid.hasLineOfSight({0, 0}, {1, 1}), "Corner touching blocks LOS.");
        check(grid.aStar({0, 0}, {1, 1}, true).size() == 3, "A* must go around blocked corner.");

        std::mt19937 rng(12345);
        for (int trial = 0; trial < 80; ++trial) {
            SceneWorld map(8, 8);
            map.fill(groundCell());
            for (std::size_t i = 0; i < map.count(); ++i) {
                const auto cell = map.coordinates(i);
                switch (rng() % 6) {
                    case 0: map.paint(cell, GridLayer::Walls, {0, 0, 0}); break;
                    case 1: map.paint(cell, GridLayer::Entities, {0, 0, 0}); break;
                    case 2: map.paint(cell, GridLayer::Objects, {0, 0, 0}); break;
                    case 3: map.erase(cell, GridLayer::Ground); break;
                    default: break;
                }
            }
            const CellPosition start{static_cast<int>(rng() % 8), static_cast<int>(rng() % 8)};
            const CellPosition goal{static_cast<int>(rng() % 8), static_cast<int>(rng() % 8)};
            for (bool diagonal : {false, true}) for (bool bfs : {false, true}) {
                const auto path = bfs ? map.breadthFirstSearch(start, goal, diagonal) : map.aStar(start, goal, diagonal);
                const double actual = pathCost(map, path, start, goal, diagonal, bfs);
                const double expected = reference(map, start, goal, diagonal, bfs);
                check(actual == expected || std::abs(actual - expected) < 1e-9, "Search differs from optimal reference.");
            }
            check(map.hasLineOfSight(start, goal) == referenceLOS(map, start, goal), "LOS differs from intersection oracle.");
            check(map.hasLineOfSight(start, goal) == map.hasLineOfSight(goal, start), "LOS must be symmetric.");
        }
        SceneWorld large;
        large.fill(groundCell());
        check(large.aStar({0, 0}, {999, 999}, true).size() == 1000, "A* on million-cell world.");
        check(large.breadthFirstSearch({0, 0}, {999, 999}).size() == 1999, "BFS on million-cell world.");
        check(large.hasLineOfSight({0, 0}, {999, 999}), "Long-distance LOS.");
        std::cout << "Navigation tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
