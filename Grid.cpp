#include "Grid.h"
#include "TilesetLibrary.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

Grid::Grid(int width, int height, float cellSize, Vector2 origin)
    : width_(width), height_(height), cellSize_(cellSize), origin_(origin) {
    if (width <= 0 || height <= 0 || !std::isfinite(cellSize) || cellSize <= 0 ||
        !std::isfinite(origin.x) || !std::isfinite(origin.y) ||
        !std::isfinite(origin.x + width * cellSize) ||
        !std::isfinite(origin.y + height * cellSize)) {
        throw std::invalid_argument("Grid dimensions and cell size must be positive and finite.");
    }
    const auto size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (size / static_cast<std::size_t>(width) != static_cast<std::size_t>(height)) {
        throw std::length_error("Grid cell count overflow.");
    }
    cells_.resize(size);
}

bool Grid::contains(CellPosition c) const {
    return c.x >= 0 && c.y >= 0 && c.x < width_ && c.y < height_;
}

std::size_t Grid::index(CellPosition c) const {
    if (!contains(c)) throw std::out_of_range("Cell outside grid.");
    return static_cast<std::size_t>(c.y) * width_ + c.x;
}

CellPosition Grid::coordinates(std::size_t i) const {
    if (i >= count()) throw std::out_of_range("Grid index out of range.");
    return {static_cast<int>(i % width_), static_cast<int>(i / width_)};
}

const GridCell& Grid::at(CellPosition c) const { return cells_[index(c)]; }
const GridCell* Grid::tryGet(CellPosition c) const { return contains(c) ? &at(c) : nullptr; }
std::optional<CellPosition> Grid::worldToCell(Vector2 world) const {
    const double x = (static_cast<double>(world.x) - origin_.x) / cellSize_;
    const double y = (static_cast<double>(world.y) - origin_.y) / cellSize_;
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= width_ || y >= height_) return {};
    return CellPosition{static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))};
}

Vector2 Grid::cellToWorld(CellPosition c) const {
    if (!contains(c)) throw std::out_of_range("Cell outside grid.");
    return {origin_.x + c.x * cellSize_, origin_.y + c.y * cellSize_};
}
Vector2 Grid::cellCenter(CellPosition c) const {
    const auto corner = cellToWorld(c);
    return {corner.x + cellSize_ / 2, corner.y + cellSize_ / 2};
}
Rectangle Grid::cellBounds(CellPosition c) const {
    const auto corner = cellToWorld(c);
    return {corner.x, corner.y, cellSize_, cellSize_};
}
Rectangle Grid::worldBounds() const { return {origin_.x, origin_.y, width_ * cellSize_, height_ * cellSize_}; }

CellRange Grid::visibleRange(Rectangle view) const {
    if (!std::isfinite(view.x) || !std::isfinite(view.y) || !std::isfinite(view.width) ||
        !std::isfinite(view.height) || view.width <= 0 || view.height <= 0) return {};
    const double left = std::max(0.0, (static_cast<double>(view.x) - origin_.x) / cellSize_);
    const double top = std::max(0.0, (static_cast<double>(view.y) - origin_.y) / cellSize_);
    const double right = std::min(static_cast<double>(width_),
        (static_cast<double>(view.x) + view.width - origin_.x) / cellSize_);
    const double bottom = std::min(static_cast<double>(height_),
        (static_cast<double>(view.y) + view.height - origin_.y) / cellSize_);
    if (left >= right || top >= bottom) return {};
    return {static_cast<int>(std::floor(left)), static_cast<int>(std::floor(top)),
            static_cast<int>(std::ceil(right)), static_cast<int>(std::ceil(bottom))};
}

void Grid::draw(Rectangle view, const TilesetLibrary* tilesets) const {
    const auto range = visibleRange(view);
    if (range.empty()) return;
    const float left = origin_.x + range.minX * cellSize_;
    const float top = origin_.y + range.minY * cellSize_;
    const float right = origin_.x + range.maxX * cellSize_;
    const float bottom = origin_.y + range.maxY * cellSize_;
    DrawRectangleRec({left, top, right - left, bottom - top}, Color{28, 34, 42, 255});
    for (int x = range.minX; x <= range.maxX; ++x) {
        const float worldX = origin_.x + x * cellSize_;
        DrawLineV({worldX, top}, {worldX, bottom}, x % 10 == 0 ? Color{81, 94, 109, 255} : Color{45, 54, 65, 255});
    }
    for (int y = range.minY; y <= range.maxY; ++y) {
        const float worldY = origin_.y + y * cellSize_;
        DrawLineV({left, worldY}, {right, worldY}, y % 10 == 0 ? Color{81, 94, 109, 255} : Color{45, 54, 65, 255});
    }
    // Grid lines remain visible on unpainted cells, underneath pixel-art tiles.
    if (tilesets) {
        for (std::size_t layer = 0; layer < static_cast<std::size_t>(GridLayer::Count); ++layer) {
            for (int y = range.minY; y < range.maxY; ++y) {
                for (int x = range.minX; x < range.maxX; ++x) {
                    tilesets->draw(at({x, y}).layers[layer], cellBounds({x, y}));
                }
            }
        }
    }
    DrawRectangleLinesEx(worldBounds(), 2, Color{130, 153, 170, 255});
}
