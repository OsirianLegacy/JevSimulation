#include "MapStorage.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>

void check(bool good, const char* message) { if (!good) throw std::runtime_error(message); }
template<class F> void rejects(F function) {
    bool failed = false;
    try { function(); } catch (const std::exception&) { failed = true; }
    check(failed, "Invalid map unexpectedly accepted.");
}
std::string bytes(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void write(const std::filesystem::path& file, const std::string& data) {
    std::ofstream out(file, std::ios::binary); out.write(data.data(), data.size());
}
void put32(std::string& data, std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) data.at(at + i) = static_cast<char>((value >> (8 * i)) & 255);
}

int main(int argc, char* argv[]) {
    try {
        check(argc == 2, "Test fixture directory required.");
        const auto directory = std::filesystem::path(argv[1]) / std::to_string(std::random_device{}());
        std::filesystem::create_directories(directory);
        const auto file = directory / "world.jevmap", bad = directory / "bad.jevmap";
        const std::vector<TilesetDescriptor> catalog{{"Overworld.png", 24, 93}, {"Walls.png", 52, 85}, {"Unused.png", 1, 1}};
        SceneWorld grid(1000, 1000, 8, {-8, 16});
        grid.paint({1, 1}, GridLayer::Ground, {0, 2, 3});
        grid.paint({1, 1}, GridLayer::Walls, {1, 4, 5});
        grid.paint({2, 2}, GridLayer::Objects, {1, 6, 7});
        grid.paint({3, 3}, GridLayer::Entities, {0, 8, 9});
        grid.paint({999, 999}, GridLayer::Ground, {0, 23, 92});
        saveMap(file, grid, catalog);
        const auto original = bytes(file);
        check(original.size() < 1024, "Sparse map must not serialize a million empty cells.");
        auto loaded = loadMap(file, catalog);
        check(loaded.width() == 1000 && loaded.height() == 1000 && loaded.cellSize() == 8 &&
              loaded.worldBounds().x == -8 && loaded.worldBounds().y == 16, "Geometry round trip.");
        for (std::size_t i = 0; i < grid.count(); ++i)
            check(grid.at(grid.coordinates(i)) == loaded.at(loaded.coordinates(i)), "All-layer map round trip.");
        check(!loaded.isWalkable({0, 0}) && loaded.isWalkable({999, 999}), "Empty/ground movement restored.");
        check(loaded.blocksSight({1, 1}) && loaded.blocksSight({3, 3}) && !loaded.blocksSight({2, 2}), "LOS layer rules restored.");

        const std::vector<TilesetDescriptor> reordered{{"New.png", 1, 1}, {"Walls.png", 52, 85}, {"Overworld.png", 24, 93}};
        auto remapped = loadMap(file, reordered);
        check(remapped.at({1, 1}).tile(GridLayer::Ground) == TileRef{2, 2, 3}, "Filename remapping after catalog changes.");
        check(remapped.at({1, 1}).tile(GridLayer::Walls) == TileRef{1, 4, 5}, "Independent per-layer remapping.");
        rejects([&] { loadMap(file, {{"Walls.png", 52, 85}}); });
        rejects([&] { loadMap(file, {{"Overworld.png", 1, 1}, {"Walls.png", 52, 85}}); });
        rejects([&] { loadMap(directory / "missing.map", catalog); });

        // Every truncated prefix must fail without assigning to the active grid.
        for (std::size_t length = 0; length < original.size(); ++length) {
            write(bad, original.substr(0, length));
            rejects([&] { loaded = loadMap(bad, catalog); });
            check(loaded.at({999, 999}) == grid.at({999, 999}), "Failed load replaced the live map.");
        }
        const auto corrupt = [&](std::size_t offset, std::uint32_t value) {
            auto data = original; put32(data, offset, value); write(bad, data);
            rejects([&] { loadMap(bad, catalog); });
        };
        corrupt(0, 0); corrupt(8, 99); corrupt(12, 0); corrupt(16, 2'000'000);
        corrupt(20, 0); corrupt(24, 0x7fc00000); corrupt(32, 5000); corrupt(36, 4'000'001); corrupt(40, 5000);
        const std::size_t recordsAt = 40 + 4 + std::string("Overworld.png").size() + 4 + std::string("Walls.png").size();
        corrupt(recordsAt, 1'000'000); corrupt(recordsAt + 4, 4); corrupt(recordsAt + 8, 2);
        corrupt(recordsAt + 12, 24); corrupt(recordsAt + 16, 93);
        auto duplicate = original + original.substr(recordsAt, 20); put32(duplicate, 36, 6);
        write(bad, duplicate); rejects([&] { loadMap(bad, catalog); });
        write(bad, original + "garbage"); rejects([&] { loadMap(bad, catalog); });

        grid.paint({0, 0}, GridLayer::Ground, {99, 0, 0});
        rejects([&] { saveMap(file, grid, catalog); });
        check(bytes(file) == original, "Failed save damaged previous file.");
        grid.erase({0, 0}, GridLayer::Ground);
        grid.erase({1, 1}, GridLayer::Walls);
        saveMap(file, grid, catalog); // Replace an existing file, including on Windows.
        check(loadMap(file, catalog).isWalkable({1, 1}), "Overwrite save did not replace map.");
        grid.clear(); saveMap(file, grid, catalog);
        check(bytes(file).size() == 72+grid.guidState().used.size()*16 && !loadMap(file, {}).isWalkable({0, 0}), "Cleared map preserves retired GUID history.");
        const auto blocked = directory / "directory-not-file";
        std::filesystem::create_directory(blocked);
        rejects([&] { saveMap(blocked, grid, catalog); });
        for (const auto& entry : std::filesystem::directory_iterator(directory))
            check(entry.path().filename().string().find(".tmp-") == std::string::npos, "Failed save left temporary file.");
        std::cout << "Map persistence tests passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
