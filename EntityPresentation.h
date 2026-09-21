#pragma once
#include "Decision.h"
#include <filesystem>

// Presentation-only assets and inspection; no textures or UI state in the simulation.
class EntityPresentation {
public:
    explicit EntityPresentation(const std::filesystem::path& assets);
    ~EntityPresentation();
    EntityPresentation(const EntityPresentation&) = delete;
    EntityPresentation& operator=(const EntityPresentation&) = delete;
    void drawCreature(Vector2 center, float size, ControlOwnership control, double time) const;
    void drawCreature(const Entity& entity, const scene::CreatureCatalog& catalog, double time) const;
    void drawHover(Rectangle cell, double time) const;
    void drawTooltip(const SceneWorld& world, std::optional<CellPosition> cell, bool paused, Vector2 mouse);
    static ai::Json inspectionData(const Entity& entity, const ai::DecisionSystem* decisions, int radius);
private:
    Texture2D units_{}, animals_{}, monsters_{}, interface_{}, bars_{}, frames_{};
};
