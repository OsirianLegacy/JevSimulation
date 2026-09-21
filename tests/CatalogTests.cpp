#include "MapStorage.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

void check(bool good,const char* message) { if(!good) throw std::runtime_error(message); }
template<class F> void rejects(F f) { bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"Invalid catalog data accepted."); }
void put32(std::string& data,std::size_t at,std::uint32_t n) { for(int i=0;i<4;++i)data.at(at+i)=static_cast<char>(n>>(8*i)); }
int main(int argc,char** argv) {
    try {
        check(argc==2,"Output path required.");
        const std::filesystem::path file(argv[1]);
        const std::vector<TilesetDescriptor> sheets{{"test.png",8,8}};
        SceneWorld grid(3,1); const TileRef tile{0,0,0};
        grid.createItemDefinition({"log","Wood log"}); grid.createItemDefinition({"ore","Iron ore"});
        grid.createObjectType({"chest","Chest",ObjectType::Container});
        grid.createObjectType({"gate","Gate",ObjectType::Door});
        rejects([&]{grid.createItemDefinition({"log","Duplicate"});});
        rejects([&]{grid.createItemDefinition({" ","Blank ID"});});
        rejects([&]{grid.createObjectType({"bad","Invalid",static_cast<ObjectType>(5)});});
        rejects([&]{grid.createObjectType({"gate","Duplicate",ObjectType::Door});});
        grid.paint({1,0},GridLayer::Ground,tile);
        const auto chest=grid.placeObject({0,0},tile,std::string("chest"));
        const auto gate=grid.placeObject({1,0},tile,std::string("gate"));
        grid.addItem(chest,{"ore","Iron ore",12});
        grid.setObjectOpen(gate,true);
        check(grid.isWalkable({1,0}) && !grid.blocksSight({1,0}),"Named Door inherits open-state rules.");
        check(grid.placeObject({0,0},tile,std::string("gate"))==chest && grid.findObject(chest)->typeDefinitionId()=="chest","Occupied placement keeps original named type.");
        rejects([&]{grid.placeObject({2,0},tile,std::string("missing"));});
        rejects([&]{grid.bindObjectType(chest,"gate");});
        grid.moveObject({0,0},{2,0});
        saveMap(file,grid,sheets);
        auto loaded=loadMap(file,sheets);
        check(loaded.itemDefinitions()==grid.itemDefinitions() && loaded.objectTypes()==grid.objectTypes(),"Catalogs round trip.");
        check(loaded.findObject(chest)->typeDefinitionId()=="chest" && loaded.findObject(gate)->typeDefinitionId()=="gate" && loaded.findObject(gate)->isOpen(),"Named object references round trip.");
        check(loaded.findObject(chest)->contents()==grid.findObject(chest)->contents(),"Catalog item stack round trip.");
        std::ifstream in(file,std::ios::binary);std::string data{std::istreambuf_iterator<char>(in),{}};in.close();
        const auto write=[&](const std::string& bytes){std::ofstream out(file,std::ios::binary|std::ios::trunc);out.write(bytes.data(),bytes.size());};
        const auto identities=grid.guidState();
        // Strip v4 identities plus v5-v7 empty pools/creatures and definition extension.
        std::size_t metadata=16+identities.used.size()*16+20+grid.creatureCatalog().json().dump().size();
        for(const auto& [key,id]:identities.items) metadata+=4+key.size()+16;
        for(const auto& [key,id]:identities.types) metadata+=4+key.size()+16;
        for(const auto& [owner,items]:identities.contents) metadata+=20+items.size()*16;
        auto version3=data.substr(0,data.size()-metadata);put32(version3,8,3);write(version3);
        const auto migrated=loadMap(file,sheets);
        check(!migrated.itemDefinitions().at("log").guid.empty() && !migrated.objectTypes().at("chest").guid.empty() &&
              !migrated.findObject(chest)->contents()[0].guid.empty() && migrated.findObject(chest)->typeDefinitionId()=="chest",
              "Version 3 migration assigns identities while preserving named types.");
        for(std::size_t n=0;n<data.size();++n) {
            write(data.substr(0,n));rejects([&]{loaded=loadMap(file,sheets);});
            check(loaded.findObject(chest)->typeDefinitionId()=="chest","Failed load preserves catalogs and references.");
        }
        auto corrupt=data; corrupt.replace(corrupt.rfind("gate"),4,"nope");write(corrupt);rejects([&]{loadMap(file,sheets);});
        corrupt=data;put32(corrupt,corrupt.find("Chest")+5,9);write(corrupt);rejects([&]{loadMap(file,sheets);});
        corrupt=data;corrupt.replace(corrupt.rfind(std::string("\3\0\0\0ore",7))+4,3,"log");write(corrupt);rejects([&]{loadMap(file,sheets);});
        grid.clear();check(grid.objectTypes().size()==2 && grid.itemDefinitions().size()==2,"Clearing cells retains reusable catalogs.");
        saveMap(file,SceneWorld(1,1),{});
        std::ifstream emptyFile(file,std::ios::binary);std::string empty{std::istreambuf_iterator<char>(emptyFile),{}};emptyFile.close();
        auto version2=empty.substr(0,44);put32(version2,8,2);write(version2);
        check(loadMap(file,{}).objectTypes().empty(),"Version 2 loads with empty catalogs.");
        auto version1=empty.substr(0,40);put32(version1,8,1);write(version1);
        check(loadMap(file,{}).itemDefinitions().empty(),"Version 1 loads with empty catalogs.");
        std::cout<<"Catalog persistence tests passed.\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
