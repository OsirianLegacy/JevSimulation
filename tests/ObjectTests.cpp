#include "MapStorage.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <unordered_set>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
    bool failed = false; try { f(); } catch (const std::exception&) { failed = true; }
    check(failed, "Invalid object operation accepted.");
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Test path required.");
        const TileRef tile{0,0,0};
        SceneWorld grid(5,1);
        for (int x=0;x<5;++x) grid.paint({x,0},GridLayer::Ground,tile);
        const auto door = grid.placeObject({2,0},tile,ObjectType::Door);
        check(!door.empty() && door.toString().size()==36, "UUID representation.");
        check(grid.at({2,0}).objectId==door && !grid.isWalkable({2,0}) && grid.blocksSight({2,0}), "Closed door.");
        check(grid.aStar({0,0},{4,0}).empty() && grid.breadthFirstSearch({0,0},{4,0}).empty() && !grid.hasLineOfSight({0,0},{4,0}), "Closed door navigation.");
        grid.setObjectOpen(door,true);
        check(grid.aStar({0,0},{4,0}).size()==5 && grid.breadthFirstSearch({0,0},{4,0}).size()==5 && grid.hasLineOfSight({0,0},{4,0}), "Open door navigation.");
        for (const auto layer : {GridLayer::Walls,GridLayer::Entities}) {
            grid.paint({2,0},layer,tile);
            check(!grid.isWalkable({2,0}) && grid.blocksSight({2,0}), "Other blockers override open door.");
            grid.erase({2,0},layer);
        }
        grid.erase({2,0},GridLayer::Ground);
        check(!grid.isWalkable({2,0}) && !grid.blocksSight({2,0}), "Open door requires ground for movement.");
        grid.paint({2,0},GridLayer::Ground,tile);
        rejects([&]{grid.addItem(door,{"wood","Wood",1});});
        const auto chest=grid.placeObject({1,0},tile,ObjectType::Container);
        grid.addItem(chest,{"wood","Wood",10});
        grid.updateItem(chest,0,{"stone","Stone",3});
        grid.setObjectOpen(chest,true);
        check(grid.placeObject({1,0},{0,1,0},ObjectType::Door)==chest && grid.findObject(chest)->contents()[0].quantity==3, "Occupied painting preserves identity and contents.");
        check(grid.at({1,0}).tile(GridLayer::Objects)==tile, "Occupied painting preserves sprite.");
        rejects([&]{grid.updateObject(chest,false,{{" ","Bad",1}});});
        rejects([&]{grid.updateItem(chest,0,{"stone","Stone",0});});
        rejects([&]{grid.addItem(chest,{std::string(256,'x'),"Too long",1});});
        rejects([&]{grid.updateObject(chest,false,std::vector<Item>(1025,{"x","X",1}));});
        rejects([&]{grid.set({3,0},grid.at({1,0}));});
        rejects([&]{grid.fill(grid.at({1,0}));});
        check(grid.findObject(chest)->isOpen() && grid.findObject(chest)->contents()[0].quantity==3, "Rejected edit is atomic.");
        check(!grid.moveObject({1,0},{2,0}) && grid.moveObject({1,0},{3,0}), "Move rejects occupied destination.");
        check(grid.at({3,0}).objectId==chest && !grid.objectAt({1,0}), "Move preserves GUID.");
        const auto resource=grid.placeObject({0,0},tile,ObjectType::Gatherable);
        grid.addItem(resource,{"ore","Ore",5});
        rejects([&]{grid.setObjectOpen(resource,true);});
        check(!grid.findObject(resource)->isContainer() && grid.findObject(chest)->isContainer(), "Type determines container flag.");
        const std::vector<TilesetDescriptor> catalog{{"test.png",2,2}};
        const std::filesystem::path file(argv[1]);
        saveMap(file,grid,catalog);
        auto loaded=loadMap(file,catalog);
        check(loaded.objectCount()==3 && loaded.at({3,0}).objectId==chest && loaded.findObject(door)->isOpen() && loaded.findObject(chest)->contents()==grid.findObject(chest)->contents(), "Object round trip.");
        std::ifstream in(file,std::ios::binary);
        std::string original{std::istreambuf_iterator<char>(in),{}}; in.close();
        const auto write=[&](const std::string& data){std::ofstream out(file,std::ios::binary|std::ios::trunc);out.write(data.data(),data.size());};
        const auto put=[](std::string& data,std::size_t offset,std::uint32_t value){for(int i=0;i<4;++i)data.at(offset+i)=static_cast<char>(value>>(i*8));};
        const std::size_t version2End=40+4+8+8*20+4+3*36+18+22;
        auto version2=original.substr(0,version2End); put(version2,8,2); write(version2);
        const auto oldMap=loadMap(file,catalog);
        check(oldMap.findObject(chest)->contents()[0].quantity==3 && !oldMap.findObject(chest)->contents()[0].guid.empty() && oldMap.findObject(door)->isOpen(),"Version 2 preserves existing objects and assigns item GUIDs.");
        // Header + one filename + eight tile records, then object count.
        const std::size_t objectStart=40+4+8+8*20+4;
        for (const auto offset : {objectStart,objectStart+20,objectStart+24,objectStart+28,objectStart+32}) {
            auto corrupt=original; put(corrupt,offset,0xffffffff); write(corrupt);
            rejects([&]{loaded=loadMap(file,catalog);});
            check(loaded.findObject(chest).has_value(),"Failed load changed live objects.");
        }
        auto zero=original; zero.replace(objectStart+4,16,16,'\0'); write(zero); rejects([&]{loadMap(file,catalog);});
        const std::size_t secondObject = objectStart + 54; // Gatherable record plus its ore stack.
        auto duplicateGuid=original;
        duplicateGuid.replace(secondObject+4,16,original.substr(objectStart+4,16));
        write(duplicateGuid); rejects([&]{loadMap(file,catalog);});
        auto duplicateCell=original; put(duplicateCell,secondObject,0);
        write(duplicateCell); rejects([&]{loadMap(file,catalog);});
        for (const auto [offset,value] : {std::pair{objectStart+24,1u}, {objectStart+28,1u},
                                        {objectStart+36,256u}, {objectStart+50,0u}}) {
            auto corrupt=original; put(corrupt,offset,value); write(corrupt); rejects([&]{loadMap(file,catalog);});
        }
        // Version 1 ends immediately after tiles and has no identities.
        auto legacy=original.substr(0,objectStart-4); put(legacy,8,1); write(legacy);
        auto migrated=loadMap(file,catalog);
        check(migrated.objectCount()==3 && migrated.objectAt({2,0})->type()==ObjectType::Container && !migrated.blocksSight({2,0}),"Legacy object migration.");
        grid.removeItem(chest,0); check(grid.objectAt({3,0}) && grid.findObject(chest)->contents().empty(),"Empty contents retain object.");
        grid.fillRegion({2,0,4,1},{});
        check(!grid.findObject(door) && !grid.findObject(chest) && grid.objectCount()==1,"Region replacement removes objects.");
        grid.clear(); check(grid.objectCount()==0,"Clear removes registry.");
        SceneWorld many(100,10); std::unordered_set<Guid,GuidHash> ids;
        for(std::size_t i=0;i<many.count();++i) ids.insert(many.placeObject(many.coordinates(i),tile,ObjectType::Container));
        check(ids.size()==many.count(),"Unique placement GUIDs.");
        std::cout << "Object tests passed.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
