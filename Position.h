#pragma once
#include "Grid.h"
#include <string>

namespace scene {
// Coordinates only; placement bounds, occupancy and movement belong to the world.
class Position {
  public:
    explicit Position(Vector2 worldPosition = {}, float cellSize = 8, Vector2 gridOrigin = {});
    Vector2 worldPosition() const { return worldPosition_; }
    CellPosition cellCoordinates() const { return cellCoordinates_; }
    float cellSize() const { return cellSize_; }
    Vector2 gridOrigin() const { return gridOrigin_; }
    void setWorldPosition(Vector2 position);
    // Moving by cell places the entity at that cell's top-left corner.
    void setCellCoordinates(CellPosition cell);
    std::string fetchData() const;
    static constexpr const char *RulesDescription =
        "Location in world units and grid cells. X increases right; Y increases down. "
        "Cell coordinates are floor((worldPosition - gridOrigin) / cellSize). "
        "Location alone does not imply walkability or valid world bounds.";
    bool operator==(const Position &other) const;

  private:
    Vector2 worldPosition_{};
    CellPosition cellCoordinates_{};
    float cellSize_;
    Vector2 gridOrigin_;
    CellPosition toCell(Vector2 position) const;
};
} // namespace scene
