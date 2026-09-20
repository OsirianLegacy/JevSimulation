#include "WorldCamera.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

WorldCamera::WorldCamera(Rectangle worldBounds) : world_(worldBounds) {
    if (!std::isfinite(world_.x) || !std::isfinite(world_.y) ||
        !std::isfinite(world_.width) || !std::isfinite(world_.height) ||
        world_.width <= 0 || world_.height <= 0) throw std::invalid_argument("Invalid camera world bounds.");
    camera_.target = {world_.x + world_.width / 2, world_.y + world_.height / 2};
    camera_.zoom = 1;
}

void WorldCamera::resize(int width, int height) {
    camera_.offset = {std::max(1, width) / 2.0f, std::max(1, height) / 2.0f};
    clampToWorld();
}

void WorldCamera::pan(Vector2 direction, float deltaTime) {
    if (!std::isfinite(deltaTime) || deltaTime <= 0 ||
        !std::isfinite(direction.x) || !std::isfinite(direction.y)) return;
    const float length = std::hypot(direction.x, direction.y);
    if (length > 0) {
        const float distance = 600.0f * deltaTime / std::max(1.0f, length);
        camera_.target.x += direction.x * distance;
        camera_.target.y += direction.y * distance;
        clampToWorld();
    }
}

void WorldCamera::setZoom(float zoom) {
    if (!std::isfinite(zoom)) return;
    camera_.zoom = std::clamp(zoom, 0.5f, 8.0f);
    clampToWorld();
}

void WorldCamera::update(float deltaTime, bool allowZoom) {
    resize(GetScreenWidth(), GetScreenHeight());
    if (!IsWindowFocused()) return;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) return;
    if (allowZoom && GetMouseWheelMove() != 0) {
        const auto before = GetScreenToWorld2D(GetMousePosition(), camera_);
        setZoom(camera_.zoom * std::pow(1.2f, GetMouseWheelMove()));
        const auto after = GetScreenToWorld2D(GetMousePosition(), camera_);
        camera_.target.x += before.x - after.x;
        camera_.target.y += before.y - after.y;
        clampToWorld();
    }
    const Vector2 direction{static_cast<float>(IsKeyDown(KEY_D) - IsKeyDown(KEY_A)),
                            static_cast<float>(IsKeyDown(KEY_S) - IsKeyDown(KEY_W))};
    pan(direction, std::min(deltaTime, 0.1f)); // Avoid jumps after pauses/debug breaks.
}

void WorldCamera::clampToWorld() {
    const auto clampAxis = [](float target, float origin, float size, float halfView) {
        return size <= halfView * 2 ? origin + size / 2 :
            std::clamp(target, origin + halfView, origin + size - halfView);
    };
    camera_.target.x = clampAxis(camera_.target.x, world_.x, world_.width, camera_.offset.x / camera_.zoom);
    camera_.target.y = clampAxis(camera_.target.y, world_.y, world_.height, camera_.offset.y / camera_.zoom);
}

Rectangle WorldCamera::visibleBounds() const {
    return {camera_.target.x - camera_.offset.x / camera_.zoom,
            camera_.target.y - camera_.offset.y / camera_.zoom,
            2 * camera_.offset.x / camera_.zoom, 2 * camera_.offset.y / camera_.zoom};
}
