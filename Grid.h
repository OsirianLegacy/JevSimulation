#pragma once

#include <raylib.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include <unordered_map>
#include <map>
#include "Object.h"

struct CellPosition {
    int x = 0;
    int y = 0;
    bool operator==(const CellPosition&) const = default;
};

enum class GridLayer : std::size_t { Ground, Walls, Objects, Entities, Count };

struct TileRef {
    int tileset = -1; // Empty; otherwise index in the tileset library.
    int column = 0;
    int row = 0;
    bool present() const { return tileset >= 0; }
    bool operator==(const TileRef&) const = default;
};

struct GridCell {
    Guid objectId{};
    std::array<TileRef, static_cast<std::size_t>(GridLayer::Count)> layers{};
    TileRef& tile(GridLayer layer) { return layers.at(static_cast<std::size_t>(layer)); }
    const TileRef& tile(GridLayer layer) const { return layers.at(static_cast<std::size_t>(layer)); }
    // Object-dependent movement/vision queries belong to SceneWorld.
    bool layersBlockSight() const {
        return tile(GridLayer::Walls).present() || tile(GridLayer::Entities).present();
    }
    bool operator==(const GridCell&) const = default;
};

class TilesetLibrary;
// Half-open bounds: minimum included, maximum excluded.
struct CellRange {
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool empty() const { return minX >= maxX || minY >= maxY; }
};

class Grid {
public:
    explicit Grid(int width=1000,int height=1000,float cellSize=8,Vector2 origin={});
    int width() const { return width_; }
    int height() const { return height_; }
    float cellSize() const { return cellSize_; }
    std::size_t count() const { return cells_.size(); }
    bool contains(CellPosition cell) const;
    std::size_t index(CellPosition cell) const;
    CellPosition coordinates(std::size_t index) const;
    const GridCell& at(CellPosition cell) const;
    const GridCell* tryGet(CellPosition cell) const;
    std::optional<CellPosition> worldToCell(Vector2 world) const;
    Vector2 cellToWorld(CellPosition cell) const;
    Vector2 cellCenter(CellPosition cell) const;
    Rectangle cellBounds(CellPosition cell) const;
    Rectangle worldBounds() const;
    CellRange visibleRange(Rectangle view) const;
    void draw(Rectangle view,const TilesetLibrary* tilesets=nullptr) const;
private:
    friend class SceneWorld;
    friend class WorldDocument;
    int width_,height_;
    float cellSize_;
    Vector2 origin_;
    std::vector<GridCell> cells_;
};
