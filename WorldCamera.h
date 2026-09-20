#pragma once
#include <raylib.h>

class WorldCamera {
public:
    explicit WorldCamera(Rectangle worldBounds);
    void resize(int width, int height);
    void pan(Vector2 direction, float deltaTime);
    void update(float deltaTime, bool allowZoom = true); // WASD, resize, mouse-wheel zoom.
    void setZoom(float zoom);
    const Camera2D& camera() const { return camera_; }
    Rectangle visibleBounds() const;

private:
    Camera2D camera_{};
    Rectangle world_;
    void clampToWorld();
};
