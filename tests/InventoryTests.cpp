#include "MapStorage.h"
#include "PlaySession.h"
#include "EntityPresentation.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F action) {
    try { action(); } catch (const std::exception &) { return; }
    throw std::runtime_error("Invalid inventory operation accepted");
}
int main(int argc, char **argv) {
    try {
        SceneWorld world(4,1);
        for(int x=0;x<4;++x) world.paint({x,0},GridLayer::Ground,{0,0,0});
        world.createItemDefinition({"apple","Apple"});
        const auto owner=world.spawnCreature({0,0});
        const auto other=world.spawnCreature({1,0});
        const auto chest=world.placeObject({3,0},{0,0,0},ObjectType::Container);
        world.setHistoryEnabled(true);
        check(world.inventory(owner)->empty() && world.findCreature(owner)->inventory().empty(),"Creatures start empty");
        check(!world.inventory(Guid{}),"Missing inventory returns nullopt");
        const auto fields=world.componentNames(owner);
        check(std::find(fields.begin(),fields.end(),"Inventory")!=fields.end(),"Inventory registered on creatures");
        world.addItem(owner,{"apple","Apple",3});
        const auto stack=world.inventory(owner)->at(0).guid;
        check(!stack.empty() && world.hasUsedGuid(stack),"Stack gets persistent identity");
        check(world.undo() && world.inventory(owner)->empty(),"Undo add");
        check(world.redo() && world.inventory(owner)->at(0).guid==stack,"Redo preserves stack identity");
        world.setResource(stack,scene::Resource::Health,scene::ResourcePool(10,7));
        const auto snapshot=*world.findCreature(owner);
        world.updateItem(owner,0,{"apple","Fresh apple",5});
        check(world.inventory(owner)->at(0).guid==stack && snapshot.inventory()[0].quantity==3 &&
              world.resource(stack,"health")->current()==7,"Update preserves identity, resources and snapshot isolation");
        check(world.undo() && world.resource(stack,"health")->current()==7 &&
              world.inventory(owner)->at(0).quantity==3,"Undo update preserves child resources");
        world.redo();
        world.addItem(owner,world.inventory(owner)->at(0));
        check(world.inventory(owner)->size()==2 && world.inventory(owner)->at(1).guid!=stack,"Copies become distinct stacks");
        world.addItem(other,{"water","Water",1});
        const auto foreign=world.inventory(other)->at(0);
        const auto before=*world.inventory(owner);
        rejects([&]{world.updateInventory(owner,{foreign});});
        rejects([&]{world.updateInventory(owner,{before[0],before[0]});});
        rejects([&]{world.updateItem(owner,0,{"apple","Apple",0});});
        rejects([&]{world.updateItem(owner,0,foreign);});
        rejects([&]{world.removeItem(owner,99);});
        rejects([&]{world.addItem(owner,{"","Invalid",1});});
        rejects([&]{world.updateInventory(owner,std::vector<Item>(scene::Inventory::maxStacks+1,{"a","A",1}));});
        check(*world.inventory(owner)==before,"Rejected edits are atomic");
        check(scene::Inventory::maxStacks==6,"Unit inventory has six slots");
        world.updateInventory(owner,std::vector<Item>(6,{"apple","Apple",1}));
        const auto full=*world.inventory(owner);
        rejects([&]{world.addItem(owner,{"wood","Wood",1});});
        check(*world.inventory(owner)==full,"Seventh stack rejected without changing inventory");
        world.undo();
        check(*world.inventory(owner)==before,"Undo full inventory preserves original stacks");
        auto reordered=before;std::reverse(reordered.begin(),reordered.end());
        world.updateInventory(owner,reordered);
        check(*world.inventory(owner)==reordered,"Stack order retained");
        world.undo();
        check(world.moveCreature(owner,{2,0}),"Move creature");
        world.undo();
        check(*world.inventory(owner)==before && world.resource(stack,"health")->current()==7,"Move undo preserves inventory");
        world.removeItem(owner,0);
        check(!world.resources(stack),"Removal deletes child component state");
        world.undo();
        check(world.resource(stack,"health")->current()==7,"Removal undo restores child state");
        const auto count=world.persistentEntityCount();
        world.removeCreature(owner);
        check(!world.inventory(owner) && !world.resources(stack) && world.persistentEntityCount()==count-3,"Owner deletion cleans up stacks");
        world.undo();
        check(*world.inventory(owner)==before && world.resource(stack,"health")->current()==7,"Owner deletion undo restores all stacks");
        world.clearHistory();world.markSaved();
        world.updateInventory(owner,before);
        check(!world.canUndo() && !world.dirty(),"No-op inventory creates no history");
        world.beginEdit();world.removeItem(owner,0);world.cancelEdit();
        check(*world.inventory(owner)==before && world.resource(stack,"health")->current()==7,"Cancelled edit restores stacks");
        world.addItem(chest,{"wood","Wood",2});
        check(world.inventory(chest)==std::optional(world.findObject(chest)->contents()),"Existing object inventory stays compatible");
        auto data=EntityPresentation::inspectionData(*world.findCreature(owner),nullptr,5);
        check(data["inventory"]["maxStacks"]==6 && data["inventory"]["items"].size()==2 && data["inventory"]["items"][0]["quantity"]==5,"Inspection exposes inventory");
        PlaySession play;play.start(world);play.world().removeItem(owner,0);
        check(world.inventory(owner)->size()==2 && play.world().inventory(owner)->size()==1,"Play inventory is independent");
        play.stop(world);
        auto doc=world.document();
        auto restored=SceneWorld::fromDocument(doc);
        check(restored.inventory(owner)==world.inventory(owner) && restored.resources(stack)==world.resources(stack),"Document round trip");
        auto bad=doc;bad.creatures[0].inventory.push_back(foreign);
        rejects([&]{SceneWorld::fromDocument(bad);});
        bad=doc;bad.creatures[0].inventory.push_back({"a","A",1,GenerateUniqueGuid()});
        rejects([&]{SceneWorld::fromDocument(bad);});
        if(argc>1) {
            saveMap(argv[1],world,{{"test.png",8,8}});
            auto loaded=loadMap(argv[1],{{"test.png",8,8}});
            check(loaded.inventory(owner)==world.inventory(owner) && loaded.resources(stack)==world.resources(stack),"Map round trip");
            SceneWorld legacy(1,1);legacy.paint({0,0},GridLayer::Ground,{0,0,0});
            const auto old=legacy.spawnCreature({0,0});
            const auto path=std::string(argv[1])+".v7";
            saveMap(path,legacy,{{"test.png",8,8}});
            // v7 ends before v8's empty per-creature inventory list.
            std::size_t nameBytes=4;for(const auto &[id,name]:legacy.document().humanNames)nameBytes+=24+name.first.size()+name.last.size();
            std::filesystem::resize_file(path,std::filesystem::file_size(path)-nameBytes-8-4*legacy.creatureIds().size());
            {std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);file.seekp(8);file.put(7);}
            auto migrated=loadMap(path,{{"test.png",8,8}});
            check(migrated.inventory(old)->empty(),"Version 7 creatures migrate with empty inventory");
            std::filesystem::resize_file(argv[1],std::filesystem::file_size(argv[1])-1);
            rejects([&]{loadMap(argv[1],{{"test.png",8,8}});});
        }
        std::cout<<"Entity inventory tests passed.\n";
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
