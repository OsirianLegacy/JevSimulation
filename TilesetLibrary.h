#pragma once
#include "Grid.h"
#include "MapStorage.h"
#include <filesystem>
#include <string>
#include <vector>

struct Tileset {
    std::string name;
    Texture2D texture{};
    int columns = 0, rows = 0;
};

class TilesetLibrary {
public:
    static constexpr int tileSize = 8;
    explicit TilesetLibrary(const std::filesystem::path& directory);
    ~TilesetLibrary();
    TilesetLibrary(const TilesetLibrary&) = delete;
    TilesetLibrary& operator=(const TilesetLibrary&) = delete;
    int count() const { return static_cast<int>(tilesets_.size()); }
    std::filesystem::path assetsDirectory() const {return assetsDirectory_;}
    const Tileset* get(int id) const;
    bool valid(TileRef tile) const;
    std::vector<TilesetDescriptor> catalog() const;
    void draw(TileRef tile, Rectangle destination, Color tint = WHITE) const;

private:
    std::filesystem::path assetsDirectory_;
    std::vector<Tileset> tilesets_;
};
