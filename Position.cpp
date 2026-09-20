#include "Position.h"
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
template <class T> std::string number(T value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{})
        throw std::runtime_error("Could not serialize position.");
    return {buffer, result.ptr};
}
template <class T> std::string coordinates(T value) {
    return "{\"x\":" + number(value.x) + ",\"y\":" + number(value.y) + "}";
}
} // namespace

scene::Position::Position(Vector2 position, float cellSize, Vector2 origin)
    : cellSize_(cellSize), gridOrigin_(origin) {
    if (!std::isfinite(cellSize) || cellSize <= 0 ||
        !std::isfinite(origin.x) || !std::isfinite(origin.y))
        throw std::invalid_argument("Position requires a positive finite cell size and finite origin.");
    setWorldPosition(position);
}

CellPosition scene::Position::toCell(Vector2 position) const {
    const auto coordinate = [&](float value, float origin) {
        const double cell = std::floor((static_cast<double>(value) - origin) / cellSize_);
        if (!std::isfinite(cell) || cell < std::numeric_limits<int>::min() ||
            cell > std::numeric_limits<int>::max())
            throw std::invalid_argument("Position must be finite with representable cell coordinates.");
        return static_cast<int>(cell);
    };
    return {coordinate(position.x, gridOrigin_.x), coordinate(position.y, gridOrigin_.y)};
}

void scene::Position::setWorldPosition(Vector2 position) {
    const auto cell = toCell(position); // Validate both axes before changing either.
    worldPosition_ = position;
    cellCoordinates_ = cell;
}

void scene::Position::setCellCoordinates(CellPosition cell) {
    const auto axis = [&](int value, float origin) {
        const double result = static_cast<double>(origin) + static_cast<double>(value) * cellSize_;
        if (std::abs(result) > std::numeric_limits<float>::max())
            throw std::invalid_argument("Cell world position exceeds float range.");
        return static_cast<float>(result);
    };
    const Vector2 position{axis(cell.x, gridOrigin_.x), axis(cell.y, gridOrigin_.y)};
    if (toCell(position) != cell)
        throw std::invalid_argument("Cell corner cannot be represented accurately in world coordinates.");
    setWorldPosition(position);
}

bool scene::Position::operator==(const Position &other) const {
    return worldPosition_.x == other.worldPosition_.x && worldPosition_.y == other.worldPosition_.y &&
           cellCoordinates_ == other.cellCoordinates_ && cellSize_ == other.cellSize_ &&
           gridOrigin_.x == other.gridOrigin_.x && gridOrigin_.y == other.gridOrigin_.y;
}

std::string scene::Position::fetchData() const {
    // RulesDescription is a fixed ASCII literal with no JSON escape characters.
    return std::string("{\"component\":\"Position\",\"rulesDescription\":\"") + RulesDescription +
           "\",\"worldPosition\":" + coordinates(worldPosition_) +
           ",\"cellCoordinates\":" + coordinates(cellCoordinates_) +
           ",\"cellSize\":" + number(cellSize_) + ",\"gridOrigin\":" + coordinates(gridOrigin_) + "}";
}
