#pragma once
#include "Grid.h"
#include <cstdint>
#include <string>
#include <vector>

namespace scene {
// Reusable cell-distance budget. Supply the current center from Position so
// moving an entity cannot leave a cached range center behind.
class Range {
  public:
    explicit Range(int radius = 0);
    int radius() const { return radius_; }
    void setRadius(int radius);
    static constexpr int CardinalCost = 1, DiagonalCost = 2;
    static constexpr const char *RulesDescription =
        "Cardinal steps cost 1; diagonal steps cost 2. Cells are in range when "
        "abs(dx) + abs(dy) <= radius. Includes the center and boundary. "
        "Range alone does not imply visibility, audibility, walkability, or valid world bounds.";
    static std::int64_t distance(CellPosition from, CellPosition to);
    bool contains(CellPosition center, CellPosition target) const;
    // Returns in-range cells in row-major order, clipped to half-open bounds.
    std::vector<CellPosition> cells(CellPosition center, CellRange bounds) const;
    std::string fetchData(CellPosition center) const;
    bool operator==(const Range &) const = default;

  private:
    int radius_ = 0;
};
} // namespace scene
