#include "MapStorage.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

void check(bool good,const char* message) { if(!good) throw std::runtime_error(message); }
template<class F> void rejects(F f) { bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"Invalid identity accepted."); }
int main(int argc,char** argv) {
    try {
        check(argc==2,"Test output path required.");
        GuidRegistry registry;
        Guid used, fresh; used.bytes[0]=1;fresh.bytes[0]=2;
        check(registry.reserve(used) && !registry.reserve(used),"Reservation tracks previously used IDs.");
        int attempt=0;
        const auto generated=registry.generate([&]{++attempt;return attempt==1 ? Guid{} : attempt==2 ? used : fresh;});
        check(generated==fresh && attempt==3 && registry.hasUsed(fresh),"Empty IDs and deterministic collisions are retried.");
        rejects([&]{registry.generate([&]{return used;});});
        rejects([&]{registry.reserve({});});
        std::vector<Guid> first,second;
        std::thread a([&]{for(int i=0;i<200;++i) first.push_back(GenerateUniqueGuid());});
        std::thread b([&]{for(int i=0;i<200;++i) second.push_back(GenerateUniqueGuid());});
        a.join();b.join();
        std::unordered_set<Guid,GuidHash> all(first.begin(),first.end());all.insert(second.begin(),second.end());
        check(all.size()==400,"Shared allocator is thread safe.");
        for(auto id:all) check(GlobalGuidRegistry().hasUsed(id) && (id.bytes[6]&0xf0)==0x40 && (id.bytes[8]&0xc0)==0x80,"Generated UUID format and reservation.");

        SceneWorld grid(4,1);const TileRef tile{0,0,0};
        grid.createItemDefinition({"wood","Wood"});grid.createObjectType({"chest","Chest",ObjectType::Container});
        const auto object=grid.placeObject({0,0},tile,std::string("chest"));
        const auto other=grid.placeObject({1,0},tile,ObjectType::Container);
        grid.addItem(object,{"wood","Wood",5});
        const auto original=grid.findObject(object)->contents()[0].guid;
        all={object,other,original,grid.itemDefinitions().at("wood").guid,grid.objectTypes().at("chest").guid};
        check(all.size()==5 && !all.contains(Guid{}),"All GUID systems share one unique namespace.");
        grid.updateItem(object,0,{"wood","Wood",10});
        check(grid.findObject(object)->contents()[0].guid==original,"Quantity edits preserve identity.");
        grid.addItem(other,grid.findObject(object)->contents()[0]);
        const auto copied=grid.findObject(other)->contents()[0].guid;
        check(copied!=original,"Adding a copy allocates a new stack identity.");
        auto duplicate=grid.findObject(object)->contents();duplicate.push_back(duplicate[0]);
        rejects([&]{grid.updateObject(object,false,duplicate);});
        rejects([&]{grid.updateObject(other,false,grid.findObject(object)->contents());});
        check(grid.findObject(other)->contents()[0].guid==copied,"Rejected assignment preserves prior contents.");
        grid.moveObject({0,0},{2,0});
        check(grid.objectAt({2,0})->contents()[0].guid==original,"Moving object preserves item GUIDs.");
        grid.removeItem(object,0);
        check(grid.hasUsedGuid(original) && GlobalGuidRegistry().hasUsed(original),"Deleted item GUID is retained.");
        rejects([&]{grid.restoreObject({3,0},tile,original,ObjectType::Door,false,{});});
        grid.addItem(object,{"wood","Wood",1});
        const auto replacement=grid.findObject(object)->contents()[0].guid;
        check(replacement!=original,"Replacement item does not recycle ID.");
        const auto file=std::filesystem::path(argv[1]);const std::vector<TilesetDescriptor> sheets{{"test.png",1,1}};
        saveMap(file,grid,sheets);auto loaded=loadMap(file,sheets);
        check(loaded.guidState().used==grid.guidState().used && loaded.hasUsedGuid(original),"History persists across save/load.");
        check(loaded.findObject(object)->contents()[0].guid==replacement && loaded.itemDefinitions()==grid.itemDefinitions() && loaded.objectTypes()==grid.objectTypes(),"All systems preserve IDs on load.");
        auto state=loaded.guidState();state.items.at("wood")=object;
        rejects([&]{loaded.restoreGuidState(state);});
        state=loaded.guidState();state.contents.at(other)[0]=state.contents.at(object)[0];
        rejects([&]{loaded.restoreGuidState(state);});
        state=loaded.guidState();state.used.erase(replacement);
        rejects([&]{loaded.restoreGuidState(state);});
        check(loaded.findObject(object)->contents()[0].guid==replacement,"Invalid metadata is atomic.");
        // Corrupt the persisted definition GUID to alias an object's GUID.
        std::ifstream in(file,std::ios::binary);std::string bytes{std::istreambuf_iterator<char>(in),{}};in.close();
        const auto marker=bytes.rfind(std::string("\4\0\0\0wood",8));
        check(marker!=std::string::npos,"Definition identity record found.");
        bytes.replace(marker+8,16,reinterpret_cast<const char*>(object.bytes.data()),16);
        {std::ofstream out(file,std::ios::binary|std::ios::trunc);out.write(bytes.data(),bytes.size());}
        rejects([&]{loaded=loadMap(file,sheets);});
        check(loaded.itemDefinitions().at("wood").guid==grid.itemDefinitions().at("wood").guid,"Malformed map does not replace live identities.");
        grid.clear();saveMap(file,grid,sheets);loaded=loadMap(file,sheets);
        check(loaded.hasUsedGuid(object) && loaded.hasUsedGuid(replacement),"Clearing cells preserves GUID tombstones.");
        SceneWorld separate(1,1);const auto newObject=separate.placeObject({0,0},tile,ObjectType::Door);
        check(!grid.hasUsedGuid(newObject),"Separate worlds use global allocation.");
        std::cout<<"GUID allocation and identity tests passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
