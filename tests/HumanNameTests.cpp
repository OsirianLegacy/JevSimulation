#include "SceneWorld.h"
#include "MapStorage.h"
#include "PlaySession.h"
#include "EntityPresentation.h"
#include <fstream>
#include <iostream>
using namespace scene;
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("Invalid names accepted");}
int main(int argc,char **argv){try{
    SceneWorld world(40,4);GridCell floor;floor.tile(GridLayer::Ground)={0,0,0};world.fill(floor);
    std::unordered_set<std::string> names;
    std::vector<Guid> ids;
    for(int x=0;x<32;++x){const auto id=world.spawnCreature({x,0},SpeciesComponent{},VocationComponent{},x%2?ControlOwnership::AI:ControlOwnership::Player);
        ids.push_back(id);const auto actor=world.findCreature(id);check(actor->humanName().has_value(),"Humans receive names");
        actor->humanName()->validate();check(names.insert(actor->displayName()).second,"Human full names unique");}
    const auto id=ids.front();const auto original=*world.findCreature(id)->humanName();
    check(EntityPresentation::inspectionData(*world.findCreature(id),nullptr,4)["name"]["full"]==original.full(),"Inspection exposes full name");
    world.setHistoryEnabled(true);world.removeCreature(id);check(world.undo() && world.findCreature(id)->humanName()==original,"Undo restores same name");
    check(world.redo() && !world.findCreature(id),"Redo removes creature");
    const auto replacement=world.spawnCreature({0,0});check(world.findCreature(replacement)->displayName()!=original.full(),"Deleted human name is reserved");
    auto catalog=world.creatureCatalog();catalog.species[1]=catalog.species.at(0);catalog.species[1].name="Wolf";world.setCreatureCatalog(catalog);
    const auto wolf=world.spawnCreature({0,1},{static_cast<Species>(1),SubSpecies::Default},{},ControlOwnership::AI);
    check(!world.findCreature(wolf)->humanName() && !world.document().humanNames.contains(wolf),"Nonhuman species do not receive names");
    world.setCreatureIdentity(wolf,{},{});const auto converted=world.findCreature(wolf)->humanName();check(converted.has_value(),"Converting to human assigns name");
    world.setCreatureIdentity(wolf,{static_cast<Species>(1),SubSpecies::Default},{});world.setCreatureIdentity(wolf,{},{});
    check(world.findCreature(wolf)->humanName()==converted,"Species changes preserve human identity");
    PlaySession play;play.start(world);check(play.world().findCreature(replacement)->humanName()==world.findCreature(replacement)->humanName(),"Play copy preserves names");play.stop(world);
    auto bad=world.document();bad.humanNames[ids[1]]=bad.humanNames.at(ids[2]);rejects([&]{SceneWorld::fromDocument(bad);});
    bad=world.document();bad.humanNames[Guid{}]={"Ada","Ward"};rejects([&]{SceneWorld::fromDocument(bad);});
    if(argc>1){
        saveMap(argv[1],world,{{"test.png",8,8}});auto loaded=loadMap(argv[1],{{"test.png",8,8}});
        check(loaded.document().humanNames==world.document().humanNames,"Save/load keeps active and retired names");
        const auto doc=world.document();std::uintmax_t tail=4+4*doc.creatures.size();
        for(const auto &[guid,name]:doc.humanNames)tail+=16+4+name.first.size()+4+name.last.size();
        std::filesystem::resize_file(argv[1],std::filesystem::file_size(argv[1])-tail);
        {std::fstream file(argv[1],std::ios::in|std::ios::out|std::ios::binary);file.seekp(8);const char version[4]{9,0,0,0};file.write(version,4);}
        auto legacy=loadMap(argv[1],{{"test.png",8,8}});std::unordered_set<std::string> migrated;
        for(auto guid:legacy.creatureIds())check(legacy.findCreature(guid)->humanName() && migrated.insert(legacy.findCreature(guid)->displayName()).second,"Old maps gain unique names");
        saveMap(argv[1],legacy,{{"test.png",8,8}});auto again=loadMap(argv[1],{{"test.png",8,8}});
        check(again.document().humanNames==legacy.document().humanNames,"Migrated names remain stable after save");
    }
    std::unordered_set<std::string> exhausted;
    for(int i=0;i<1100;++i){auto name=generateHumanName(exhausted);name.validate();check(exhausted.insert(name.full()).second,"Name pool exhaustion never duplicates or hangs");}
    std::cout<<"Human name tests passed.\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
