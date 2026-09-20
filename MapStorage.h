#pragma once
#include "SceneWorld.h"
#include <filesystem>
#include <string>

struct TilesetDescriptor {
    std::string name;
    int columns = 0, rows = 0;
};

// Versioned sparse maps; texture references are stored by filename, not runtime ID.
void saveMap(const std::filesystem::path& file, const SceneWorld& grid,
             const std::vector<TilesetDescriptor>& tilesets);
// Validates into a new grid. Callers replace the live map only after success.
SceneWorld loadMap(const std::filesystem::path& file, const std::vector<TilesetDescriptor>& tilesets);
