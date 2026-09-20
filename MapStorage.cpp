#include "MapStorage.h"
#include "AtomicFile.h"
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr std::uint32_t maxCells = 1'000'000, maxSheets = 4096;
void require(bool good, const char* message) { if (!good) throw std::runtime_error(message); }
void write32(std::ostream& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.put(static_cast<char>((value >> shift) & 255));
}
std::uint32_t read32(std::istream& in) {
    std::uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const int byte = in.get();
        require(byte != std::char_traits<char>::eof(), "Map is truncated.");
        value |= static_cast<std::uint32_t>(byte) << shift;
    }
    return value;
}
void writeText(std::ostream& out, const std::string& text) {
    write32(out, static_cast<std::uint32_t>(text.size()));
    out.write(text.data(), text.size());
}
std::string readText(std::istream& in) {
    const auto length = read32(in);
    require(length && length <= Object::maxTextBytes, "Invalid item text length.");
    std::string text(length, '\0'); in.read(text.data(), length);
    require(static_cast<bool>(in), "Truncated item text.");
    return text;
}
void writeGuid(std::ostream& out,Guid id) { out.write(reinterpret_cast<const char*>(id.bytes.data()),16); }
Guid readGuid(std::istream& in) {
    Guid id; in.read(reinterpret_cast<char*>(id.bytes.data()),16);
    require(static_cast<bool>(in) && !id.empty(),"Empty or truncated GUID.");
    return id;
}
void validateCatalog(const std::vector<TilesetDescriptor>& sheets) {
    require(sheets.size() <= maxSheets, "Too many tilesets.");
    std::unordered_set<std::string> names;
    for (const auto& sheet : sheets) {
        require(!sheet.name.empty() && sheet.name.size() <= 255 && sheet.columns > 0 && sheet.rows > 0,
                "Invalid tileset catalog.");
        require(names.insert(sheet.name).second, "Duplicate tileset filename.");
    }
}
}

void saveDocument(const std::filesystem::path& file, const WorldDocument& grid, const std::vector<TilesetDescriptor>& sheets) {
    validateCatalog(sheets);
    require(grid.count() <= maxCells && grid.cellSize() == 8, "Maps must use 8x8 cells and at most one million cells.");
    std::vector<int> used(sheets.size(), -1);
    std::vector<int> ids;
    std::uint32_t records = 0;
    for (std::size_t i = 0; i < grid.count(); ++i) {
        for (const auto& tile : grid.at(grid.coordinates(i)).layers) {
            if (!tile.present()) continue;
            require(static_cast<std::size_t>(tile.tileset) < sheets.size(), "Map references an unknown tileset.");
            const auto& sheet = sheets[tile.tileset];
            require(tile.column >= 0 && tile.column < sheet.columns && tile.row >= 0 && tile.row < sheet.rows,
                    "Map contains a tile outside its tilesheet.");
            if (used[tile.tileset] < 0) { used[tile.tileset] = static_cast<int>(ids.size()); ids.push_back(tile.tileset); }
            ++records;
        }
    }
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path());
    auto temporary = file;
    std::random_device random;
    temporary += ".tmp-" + std::to_string(random()) + "-" + std::to_string(random());
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        require(static_cast<bool>(out), "Cannot create temporary map save.");
        out.write("JEVMAP01", 8);
        write32(out, grid.resources.empty() ? 4 : 5); // Version 5 adds optional resource pools.
        write32(out, grid.width()); write32(out, grid.height());
        write32(out, std::bit_cast<std::uint32_t>(grid.cellSize()));
        const auto bounds = grid.worldBounds();
        write32(out, std::bit_cast<std::uint32_t>(bounds.x));
        write32(out, std::bit_cast<std::uint32_t>(bounds.y));
        write32(out, static_cast<std::uint32_t>(ids.size()));
        write32(out, records);
        for (const int id : ids) {
            write32(out, static_cast<std::uint32_t>(sheets[id].name.size()));
            out.write(sheets[id].name.data(), static_cast<std::streamsize>(sheets[id].name.size()));
        }
        for (std::size_t i = 0; i < grid.count(); ++i) {
            const auto& cell = grid.at(grid.coordinates(i));
            for (std::uint32_t layer = 0; layer < 4; ++layer) {
                const auto& tile = cell.layers[layer];
                if (!tile.present()) continue;
                write32(out, static_cast<std::uint32_t>(i)); write32(out, layer);
                write32(out, used[tile.tileset]); write32(out, tile.column); write32(out, tile.row);
            }
        }
        write32(out, static_cast<std::uint32_t>(grid.objectCount()));
        for (std::size_t i = 0; i < grid.count(); ++i) {
            const auto object = grid.objectAt(grid.coordinates(i));
            if (!object) continue;
            Object::validate(object->type(), object->isOpen(), object->contents());
            write32(out, static_cast<std::uint32_t>(i));
            out.write(reinterpret_cast<const char*>(object->id().bytes.data()), 16);
            write32(out, static_cast<std::uint32_t>(object->type()));
            write32(out, object->isOpen()); write32(out, object->isContainer());
            write32(out, static_cast<std::uint32_t>(object->contents().size()));
            for (const auto& item : object->contents()) {
                writeText(out, item.definitionId); writeText(out, item.displayName); write32(out, item.quantity);
            }
        require(out.tellp() <= 85'000'000, "Map file is too large.");
        }
        write32(out,static_cast<std::uint32_t>(grid.itemDefinitions().size()));
        for (const auto& [id,item] : grid.itemDefinitions()) { writeText(out,id); writeText(out,item.displayName); }
        write32(out,static_cast<std::uint32_t>(grid.objectTypes().size()));
        for (const auto& [id,type] : grid.objectTypes()) {
            writeText(out,id); writeText(out,type.displayName); write32(out,static_cast<std::uint32_t>(type.behavior));
        }
        std::uint32_t bindings = 0;
        for (std::size_t i=0;i<grid.count();++i) {
            const auto object = grid.objectAt(grid.coordinates(i));
            if (object && !object->typeDefinitionId().empty()) ++bindings;
        }
        write32(out,bindings);
        for (std::size_t i=0;i<grid.count();++i) {
            const auto object = grid.objectAt(grid.coordinates(i));
            if (!object || object->typeDefinitionId().empty()) continue;
            out.write(reinterpret_cast<const char*>(object->id().bytes.data()),16);
            writeText(out,object->typeDefinitionId());
        }
        const auto state=grid.guidState();
        write32(out,static_cast<std::uint32_t>(state.items.size()));
        for(const auto& [key,id]:state.items) { writeText(out,key);writeGuid(out,id); }
        write32(out,static_cast<std::uint32_t>(state.types.size()));
        for(const auto& [key,id]:state.types) { writeText(out,key);writeGuid(out,id); }
        write32(out,static_cast<std::uint32_t>(state.contents.size()));
        for(const auto& [owner,items]:state.contents) {
            writeGuid(out,owner);write32(out,static_cast<std::uint32_t>(items.size()));
            for(auto id:items) writeGuid(out,id);
        }
        write32(out,static_cast<std::uint32_t>(state.used.size()));
        for(auto id:state.used) writeGuid(out,id);
            if(!grid.resources.empty()) {
            write32(out,static_cast<std::uint32_t>(grid.resources.size()));
            for(const auto& [owner,resource]:grid.resources){
                writeGuid(out,owner);write32(out,static_cast<std::uint32_t>(resource.pools().size()));
                for(const auto& [key,pool]:resource.pools()){
                    writeText(out,key);write32(out,std::bit_cast<std::uint32_t>(pool.maximum()));write32(out,std::bit_cast<std::uint32_t>(pool.current()));
                }
            }
        }
        require(out.tellp() <= 85'000'000,"Map file is too large.");
        out.flush(); require(static_cast<bool>(out), "Map write failed; previous save preserved.");
        out.close(); require(!out.fail(), "Map close failed; previous save preserved.");
        replaceFile(temporary, file);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

WorldDocument loadDocument(const std::filesystem::path& file, const std::vector<TilesetDescriptor>& sheets) {
    validateCatalog(sheets);
    require(std::filesystem::file_size(file) <= 85'000'000, "Map file is too large.");
    std::ifstream in(file, std::ios::binary);
    require(static_cast<bool>(in), "Cannot open map save.");
    char magic[8]{}; in.read(magic, 8);
    require(std::string(magic, 8) == "JEVMAP01", "Not a JevSimulation map.");
    const auto version = read32(in);
    require(version >= 1 && version <= 5, "Unsupported map version.");
    const auto width = read32(in), height = read32(in);
    const float size = std::bit_cast<float>(read32(in));
    const float x = std::bit_cast<float>(read32(in)), y = std::bit_cast<float>(read32(in));
    require(width && height && width <= maxCells && height <= maxCells &&
            static_cast<std::uint64_t>(width) * height <= maxCells, "Invalid map dimensions.");
    require(size == 8 && std::isfinite(x) && std::isfinite(y), "Invalid map geometry.");
    const auto sheetCount = read32(in), records = read32(in);
    require(sheetCount <= maxSheets && records <= static_cast<std::uint64_t>(width) * height * 4, "Invalid map record counts.");
    std::unordered_map<std::string, int> lookup;
    for (std::size_t i = 0; i < sheets.size(); ++i) lookup.emplace(sheets[i].name, static_cast<int>(i));
    std::vector<int> remap;
    std::unordered_set<std::string> names;
    for (std::uint32_t i = 0; i < sheetCount; ++i) {
        const auto length = read32(in);
        require(length > 0 && length <= 255, "Invalid tileset filename length.");
        std::string name(length, '\0'); in.read(name.data(), length);
        require(static_cast<bool>(in), "Map tileset table is truncated.");
        require(names.insert(name).second, "Duplicate map tileset filename.");
        const auto found = lookup.find(name);
        if (found == lookup.end()) throw std::runtime_error("Missing tileset: " + name);
        remap.push_back(found->second);
    }
    WorldDocument loaded(static_cast<int>(width), static_cast<int>(height), size, {x, y});
    std::unordered_map<std::uint32_t, TileRef> objectTiles;
    for (std::uint32_t i = 0; i < records; ++i) {
        const auto cell = read32(in), layer = read32(in), sheet = read32(in), column = read32(in), row = read32(in);
        require(cell < loaded.count() && layer < 4 && sheet < remap.size(), "Invalid map tile reference.");
        const int id = remap[sheet];
        require(column < static_cast<unsigned>(sheets[id].columns) && row < static_cast<unsigned>(sheets[id].rows),
                "Saved tile no longer fits its tilesheet.");
        const auto position = loaded.coordinates(cell);
        const auto targetLayer = static_cast<GridLayer>(layer);
        const TileRef tile{id, static_cast<int>(column), static_cast<int>(row)};
        if (targetLayer == GridLayer::Objects) {
            require(objectTiles.emplace(cell, tile).second, "Duplicate object tile record.");
        } else {
            require(!loaded.at(position).tile(targetLayer).present(), "Duplicate map cell/layer record.");
            loaded.paint(position, targetLayer, tile);
        }
    }
    if (version == 1) {
        for (const auto& [cell, tile] : objectTiles)
            loaded.placeObject(loaded.coordinates(cell), tile, ObjectType::Container);
    } else {
        const auto count = read32(in);
        require(count == objectTiles.size(), "Object instances do not match object tiles.");
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto cell = read32(in);
            Guid id; in.read(reinterpret_cast<char*>(id.bytes.data()), 16);
            require(static_cast<bool>(in), "Truncated object GUID.");
            const auto type = read32(in), open = read32(in), container = read32(in), stacks = read32(in);
            require(type <= static_cast<unsigned>(ObjectType::Gatherable) && open <= 1 &&
                    container == (type == static_cast<unsigned>(ObjectType::Container)) &&
                    stacks <= Object::maxStacks, "Invalid saved object properties.");
            const auto tile = objectTiles.find(cell);
            require(tile != objectTiles.end(), "Object has no matching cell tile.");
            std::vector<Item> items;
            for (std::uint32_t j = 0; j < stacks; ++j) {
                auto definition = readText(in); auto name = readText(in); const auto quantity = read32(in);
                items.push_back({std::move(definition), std::move(name), quantity});
            }
            loaded.restoreObject(loaded.coordinates(cell), tile->second, id, static_cast<ObjectType>(type),
                                 open != 0, std::move(items));
            objectTiles.erase(tile);
        }
    }
    if (version >= 3) {
        const auto items = read32(in);
        require(items <= SceneWorld::maxDefinitions,"Too many item definitions.");
        for (std::uint32_t i=0;i<items;++i) {
            auto id=readText(in); auto name=readText(in);
            loaded.createItemDefinition({std::move(id),std::move(name)});
        }
        const auto types = read32(in);
        require(types <= SceneWorld::maxDefinitions,"Too many object type definitions.");
        for (std::uint32_t i=0;i<types;++i) {
            auto id=readText(in); auto name=readText(in); const auto behavior=read32(in);
            loaded.createObjectType({std::move(id),std::move(name),static_cast<ObjectType>(behavior)});
        }
        const auto bindings = read32(in);
        require(bindings <= loaded.objectCount(),"Too many object type bindings.");
        for (std::uint32_t i=0;i<bindings;++i) {
            Guid id; in.read(reinterpret_cast<char*>(id.bytes.data()),16);
            require(static_cast<bool>(in),"Truncated type binding.");
            loaded.bindObjectType(id,readText(in));
        }
    }
    if(version>=4) {
        GuidState state;
        const auto items=read32(in);
        require(items==loaded.itemDefinitions().size(),"Item identity count mismatch.");
        for(std::uint32_t i=0;i<items;++i) {
            auto key=readText(in);auto id=readGuid(in);
            require(state.items.emplace(std::move(key),id).second,"Duplicate item identity record.");
        }
        const auto types=read32(in);
        require(types==loaded.objectTypes().size(),"Type identity count mismatch.");
        for(std::uint32_t i=0;i<types;++i) {
            auto key=readText(in);auto id=readGuid(in);
            require(state.types.emplace(std::move(key),id).second,"Duplicate type identity record.");
        }
        const auto owners=read32(in);
        require(owners==loaded.objectCount(),"Contents identity count mismatch.");
        for(std::uint32_t i=0;i<owners;++i) {
            const auto owner=readGuid(in);const auto count=read32(in);
            const auto object=loaded.findObject(owner);
            require(object && count==object->contents().size(),"Invalid item identity owner or count.");
            auto [found,inserted]=state.contents.emplace(owner,std::vector<Guid>{});
            require(inserted,"Duplicate contents identity record.");
            for(std::uint32_t j=0;j<count;++j) found->second.push_back(readGuid(in));
        }
        const auto history=read32(in);
        require(history<=SceneWorld::maxGuidHistory,"Too many used GUIDs.");
        for(std::uint32_t i=0;i<history;++i)
            require(state.used.insert(readGuid(in)).second,"Duplicate GUID history record.");
        loaded.restoreGuidState(std::move(state));
    }
    if(version>=5){
        const auto count=read32(in);require(count<=SceneWorld::maxGuidHistory,"Too many resource owners.");
        for(std::uint32_t i=0;i<count;++i){
            const auto owner=readGuid(in);const auto pools=read32(in);
            require(pools>0 && pools<=scene::Resource::maxPools,"Invalid resource pool count.");
            scene::Resource resource;
            for(std::uint32_t j=0;j<pools;++j){
                const auto key=readText(in);
                const float maximum=std::bit_cast<float>(read32(in)),current=std::bit_cast<float>(read32(in));
                require(!resource.find(key),"Duplicate resource key.");resource.set(key,scene::ResourcePool(maximum,current));
            }
            require(loaded.resources.emplace(owner,std::move(resource)).second,"Duplicate resource owner.");
        }
    }
    require(in.peek() == std::char_traits<char>::eof() && !in.bad(), "Unexpected data after map records.");
    return loaded;
}

void saveMap(const std::filesystem::path& file,const SceneWorld& world,const std::vector<TilesetDescriptor>& sheets) {
    saveDocument(file,world.document(),sheets);
}
SceneWorld loadMap(const std::filesystem::path& file,const std::vector<TilesetDescriptor>& sheets) {
    return SceneWorld::fromDocument(loadDocument(file,sheets));
}
