#pragma once
#include "Grid.h"
inline GridCell groundCell() {
    GridCell cell;
    cell.tile(GridLayer::Ground) = {0, 0, 0};
    return cell;
}
inline GridCell wallCell() {
    auto cell = groundCell();
    cell.tile(GridLayer::Walls) = {0, 1, 0};
    return cell;
}
