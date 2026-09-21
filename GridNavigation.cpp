#include "SceneWorld.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>

namespace {
constexpr std::array<CellPosition, 8> directions{{{0, -1}, {-1, 0}, {1, 0}, {0, 1},
                                                {-1, -1}, {1, -1}, {-1, 1}, {1, 1}}};
constexpr auto unseen = std::numeric_limits<std::size_t>::max();
constexpr double diagonalCost = 1.4142135623730951;

template<class Visit>
void visitSteps(const SceneWorld& grid, CellPosition from, bool diagonals, Visit visit) {
    for (std::size_t i = 0; i < (diagonals ? 8u : 4u); ++i) {
        const auto d = directions[i];
        const CellPosition next{from.x + d.x, from.y + d.y};
        if (!grid.isWalkable(next)) continue;
        if (d.x != 0 && d.y != 0 &&
            (!grid.isWalkable({from.x + d.x, from.y}) || !grid.isWalkable({from.x, from.y + d.y}))) continue;
        visit(next, i < 4 ? 1.0 : diagonalCost);
    }
}

std::vector<CellPosition> reconstruct(const SceneWorld& grid, const std::vector<std::size_t>& parent,
                                      std::size_t start, std::size_t goal) {
    std::vector<CellPosition> path;
    for (auto current = goal;; current = parent[current]) {
        path.push_back(grid.coordinates(current));
        if (current == start) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

double heuristic(CellPosition cell, CellPosition goal, bool diagonals) {
    const double dx = std::abs(cell.x - goal.x), dy = std::abs(cell.y - goal.y);
    return diagonals ? std::max(dx, dy) + (diagonalCost - 1) * std::min(dx, dy) : dx + dy;
}
}

std::vector<CellPosition> SceneWorld::aStar(CellPosition start, CellPosition goal, bool diagonals) const {
    if (!isWalkable(start) || !isWalkable(goal)) return {};
    if (start == goal) return {start};
    struct Entry { std::size_t index; double g, h; };
    const auto later = [](const Entry& a, const Entry& b) {
        if (a.g + a.h != b.g + b.h) return a.g + a.h > b.g + b.h;
        if (a.h != b.h) return a.h > b.h;
        return a.index > b.index;
    };
    std::priority_queue<Entry, std::vector<Entry>, decltype(later)> open(later);
    std::vector<double> cost(count(), std::numeric_limits<double>::infinity());
    std::vector<std::size_t> parent(count(), unseen);
    const auto startIndex = index(start), goalIndex = index(goal);
    cost[startIndex] = 0;
    parent[startIndex] = startIndex;
    open.push({startIndex, 0, heuristic(start, goal, diagonals)});
    while (!open.empty()) {
        const auto current = open.top();
        open.pop();
        if (current.g != cost[current.index]) continue; // Superseded queue entry.
        if (current.index == goalIndex) return reconstruct(*this, parent, startIndex, goalIndex);
        visitSteps(*this, coordinates(current.index), diagonals, [&](CellPosition next, double stepCost) {
            const auto nextIndex = index(next);
            const double candidate = current.g + stepCost;
            if (candidate >= cost[nextIndex]) return;
            cost[nextIndex] = candidate;
            parent[nextIndex] = current.index;
            open.push({nextIndex, candidate, heuristic(next, goal, diagonals)});
        });
    }
    return {};
}

std::vector<CellPosition> SceneWorld::breadthFirstSearch(CellPosition start, CellPosition goal, bool diagonals) const {
    if (!isWalkable(start) || !isWalkable(goal)) return {};
    if (start == goal) return {start};
    std::vector<std::size_t> parent(count(), unseen);
    std::queue<std::size_t> open;
    const auto startIndex = index(start), goalIndex = index(goal);
    parent[startIndex] = startIndex;
    open.push(startIndex);
    while (!open.empty()) {
        const auto current = open.front();
        open.pop();
        if (current == goalIndex) return reconstruct(*this, parent, startIndex, goalIndex);
        visitSteps(*this, coordinates(current), diagonals, [&](CellPosition next, double) {
            const auto nextIndex = index(next);
            if (parent[nextIndex] != unseen) return;
            parent[nextIndex] = current;
            open.push(nextIndex);
        });
    }
    return {};
}

bool SceneWorld::hasLineOfSight(CellPosition start, CellPosition goal,bool allowOccupiedEndpoints) const {
    const auto blocked=[&](CellPosition cell) {
        if(!allowOccupiedEndpoints || (cell!=start && cell!=goal))return blocksSight(cell);
        const auto terrain=tryGet(cell);
        if(!terrain || terrain->layersBlockSight())return true;
        const auto object=objectAt(cell);
        return object && object->type()==ObjectType::Door && !object->isOpen();
    };
    if (blocked(start) || blocked(goal)) return false;
    const std::int64_t nx = std::abs(goal.x - start.x), ny = std::abs(goal.y - start.y);
    const int sx = (goal.x > start.x) - (goal.x < start.x);
    const int sy = (goal.y > start.y) - (goal.y < start.y);
    auto current = start;
    std::int64_t ix = 0, iy = 0;
    while (ix < nx || iy < ny) {
        // Compare exact boundary-crossing times; no floating-point slope error.
        const auto crossX = (1 + 2 * ix) * ny;
        const auto crossY = (1 + 2 * iy) * nx;
        if (crossX == crossY) {
            if (blocked({current.x + sx, current.y}) ||
                blocked({current.x, current.y + sy})) return false;
            current.x += sx; current.y += sy; ++ix; ++iy;
        } else if (crossX < crossY) {
            current.x += sx; ++ix;
        } else {
            current.y += sy; ++iy;
        }
        if (blocked(current)) return false;
    }
    return true;
}
