#include "TilesetLibrary.h"
#include <algorithm>
#include <cctype>
#include <iostream>

TilesetLibrary::TilesetLibrary(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        auto extension = it->path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
        if (extension == ".png") files.push_back(it->path());
    }
    if (error) std::cerr << "[ERROR] Cannot read tileset directory: " << error.message() << '\n';
    std::sort(files.begin(), files.end());
    // The requested default sheet comes first; painted references never change on dropdown selection.
    const auto overworld = std::find_if(files.begin(), files.end(), [](const auto& p) { return p.filename() == "Overworld.png"; });
    if (overworld != files.end()) std::rotate(files.begin(), overworld, overworld + 1);
    tilesets_.reserve(files.size());
    for (const auto& file : files) {
        Tileset sheet;
        sheet.name = file.filename().string();
        sheet.texture = LoadTexture(file.string().c_str());
        if (!IsTextureValid(sheet.texture)) {
            std::cerr << "[ERROR] Unable to load tileset " << sheet.name << '\n';
            continue;
        }
        if (sheet.texture.width % tileSize || sheet.texture.height % tileSize) {
            UnloadTexture(sheet.texture);
            std::cerr << "[ERROR] Tileset is not aligned to 8x8 tiles: " << sheet.name << '\n';
            continue;
        }
        SetTextureFilter(sheet.texture, TEXTURE_FILTER_POINT);
        sheet.columns = sheet.texture.width / tileSize;
        sheet.rows = sheet.texture.height / tileSize;
        tilesets_.push_back(std::move(sheet));
    }
}

TilesetLibrary::~TilesetLibrary() { for (const auto& sheet : tilesets_) UnloadTexture(sheet.texture); }
std::vector<TilesetDescriptor> TilesetLibrary::catalog() const {
    std::vector<TilesetDescriptor> result;
    for (const auto& sheet : tilesets_) result.push_back({sheet.name, sheet.columns, sheet.rows});
    return result;
}
const Tileset* TilesetLibrary::get(int id) const {
    return id >= 0 && id < count() ? &tilesets_[id] : nullptr;
}
bool TilesetLibrary::valid(TileRef tile) const {
    const auto* sheet = get(tile.tileset);
    return sheet && tile.column >= 0 && tile.row >= 0 && tile.column < sheet->columns && tile.row < sheet->rows;
}
void TilesetLibrary::draw(TileRef tile, Rectangle destination, Color tint) const {
    if (!valid(tile)) return;
    DrawTexturePro(get(tile.tileset)->texture,
                   {static_cast<float>(tile.column * tileSize), static_cast<float>(tile.row * tileSize), tileSize, tileSize},
                   destination, {}, 0, tint);
}
