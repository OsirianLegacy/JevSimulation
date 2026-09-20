#include "Range.h"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

scene::Range::Range(int radius) { setRadius(radius); }

void scene::Range::setRadius(int radius) {
    if (radius < 0)
        throw std::invalid_argument("Range radius must be nonnegative.");
    radius_ = radius;
}

std::int64_t scene::Range::distance(CellPosition from, CellPosition to) {
    // Widen before subtraction to support the full cell-coordinate domain.
    return std::abs(static_cast<std::int64_t>(to.x) - from.x) +
           std::abs(static_cast<std::int64_t>(to.y) - from.y);
}

bool scene::Range::contains(CellPosition center, CellPosition target) const {
    return distance(center, target) <= radius_;
}

std::vector<CellPosition> scene::Range::cells(CellPosition center, CellRange bounds) const {
    std::vector<CellPosition> result;
    if (bounds.empty()) return result;
    const auto firstY = std::max<std::int64_t>(bounds.minY, static_cast<std::int64_t>(center.y) - radius_);
    const auto lastY = std::min<std::int64_t>(static_cast<std::int64_t>(bounds.maxY) - 1,
                                            static_cast<std::int64_t>(center.y) + radius_);
    for (auto y = firstY; y <= lastY; ++y) {
        const auto reach = radius_ - std::abs(y - center.y);
        const auto firstX = std::max<std::int64_t>(bounds.minX, static_cast<std::int64_t>(center.x) - reach);
        const auto lastX = std::min<std::int64_t>(static_cast<std::int64_t>(bounds.maxX) - 1,
                                                static_cast<std::int64_t>(center.x) + reach);
        for (auto x = firstX; x <= lastX; ++x)
            result.push_back({static_cast<int>(x), static_cast<int>(y)});
    }
    return result;
}

std::string scene::Range::fetchData(CellPosition center) const {
    // The fixed rules literal contains no characters requiring JSON escaping.
    return std::string("{\"component\":\"Range\",\"rulesDescription\":\"") + RulesDescription +
           "\",\"radius\":" + std::to_string(radius_) +
           ",\"center\":{\"x\":" + std::to_string(center.x) + ",\"y\":" + std::to_string(center.y) +
           "},\"cardinalCost\":" + std::to_string(CardinalCost) +
           ",\"diagonalCost\":" + std::to_string(DiagonalCost) + "}";
}
