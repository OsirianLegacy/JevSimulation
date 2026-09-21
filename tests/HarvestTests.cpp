#include "Decision.h"
#include "MapStorage.h"
#include "PlaySession.h"
#include <iostream>
#include <set>
using namespace scene;
void check(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F f) {try{f();}catch(const std::exception&){return;}throw std::runtime_error("Invalid harvest accepted");}
void ground(SceneWorld &w) {GridCell c;c.tile(GridLayer::Ground)={0,0,0};w.fill(c);}
std::vector<Guid> drops(const SceneWorld &w) {
    std::vector<Guid> result;
    for(auto id:w.objectsInChunk({0,0}))if(!w.harvestable(id) && w.findObject(id)->type()==ObjectType::Gatherable && !w.inventory(id)->empty())result.push_back(id);
    return result;
}
void clearDrops(SceneWorld &w) {for(auto id:drops(w))w.removeObjectById(id);}
ai::Execution order(Guid actor,Guid node) {
    ai::Execution e;e.goal={"test","harvest","player","pending",actor,{{"target",node.toString()}},nullptr};return e;
}
int main(int argc,char **argv) {
try {
    SceneWorld w(24,24);ground(w);
    auto catalog=w.creatureCatalog();
    auto &tree=catalog.harvestables.at("tree");tree.workSeconds=0.1f;tree.units=8;
    auto quick=tree;quick.units=1;catalog.harvestables["quick"]=quick;
    auto regrow=quick;regrow.regenerationSeconds=1;regrow.depletedTile={0,2,0};catalog.harvestables["regrow"]=regrow;
    auto tool=quick;tool.requiredTool="axe";catalog.harvestables["tool_tree"]=tool;
    auto remove=quick;remove.removeWhenDepleted=true;catalog.harvestables["remove_tree"]=remove;
    catalog.species.at(0).carcassHarvest="carcass";catalog.species.at(0).carcassTile={0,1,0};
    w.setCreatureCatalog(catalog);
    const auto actor=w.spawnCreature({9,10},"worker",ControlOwnership::Player);
    const auto rival=w.spawnCreature({10,9});
    const auto node=w.placeObject({10,10},{0,0,0},ObjectType::Gatherable);
    w.addItem(node,{"seed","Stored seed",1});w.setHarvestable(node,"tree");w.setHistoryEnabled(true);
    rejects([&]{w.setResource(node,Harvestable::Units,ResourcePool(3,1.5f));});
    rejects([&]{w.removeResource(node,Harvestable::Units);});
    check(w.completeHarvest(actor,node).allowed && drops(w).empty(),"No burst before 25% loss");
    check(w.resource(node,Resource::Health)->current()==87.5f,"Work damages health");
    check(w.completeHarvest(actor,node).allowed && drops(w).size()==2,"75% health emits first burst");
    auto first=drops(w);auto firstItem=w.inventory(first.front())->front();
    check(w.inventory(actor)->empty() && w.inventory(node)->size()==1,"Drops are on ground, stored loot stays separate");
    check(w.undo() && drops(w).empty() && w.resource(node,Resource::Health)->current()==87.5f,"Undo restores threshold and drops atomically");
    check(w.redo() && w.inventory(first.front())->front()==firstItem,"Redo preserves random quantity and stack identity");
    for(int i=0;i<6;++i)check(w.completeHarvest(actor,node).allowed,"Harvest remaining health");
    check(drops(w).size()==8 && w.resource(node,Resource::Health)->depleted(),"Exactly four bursts through zero health");
    std::set<std::pair<int,int>> occupied;
    for(auto id:drops(w)) {
        const auto cell=*w.objectPosition(id);const auto item=w.inventory(id)->front();
        check(occupied.insert({cell.x,cell.y}).second && !w.creatureAt(cell),"Unique unoccupied drop cells");
        check(w.at(cell).tile(GridLayer::Ground).present() && !w.at(cell).tile(GridLayer::Walls).present(),"Drop terrain walkable");
        check(std::abs(cell.x-10)<=4 && std::abs(cell.y-10)<=4,"Drops stay near source");
        check(item.quantity>=1 && item.quantity<=(item.definitionId=="wood"?2u:1u),"Random quantity in configured bounds");
    }
    check(!w.completeHarvest(rival,node).allowed && drops(w).size()==8,"Last unit cannot duplicate yields");
    if(argc>1) {
        saveMap(argv[1],w,{{"test.png",8,8}});auto loaded=loadMap(argv[1],{{"test.png",8,8}});
        check(drops(loaded).size()==8 && loaded.inventory(first.front())==w.inventory(first.front()),"Drop persistence preserves stacks");
        check(loaded.resource(node,Resource::Health)->depleted() && !loaded.completeHarvest(actor,node).allowed,"Load cannot repeat thresholds");
    }
    clearDrops(w);w.setHarvestable(node,"quick");
    w.updateInventory(actor,std::vector<Item>(Inventory::maxStacks,{"filler","Filler",1}));
    check(w.completeHarvest(actor,node).allowed && drops(w).size()==8,"Full actor can harvest into world");
    clearDrops(w);w.updateInventory(actor,{});w.setHarvestable(node,"tool_tree");
    check(w.completeHarvest(actor,node).reason=="missing_tool","Tool required");
    w.addItem(actor,{"axe","Axe",1});const auto axe=w.inventory(actor)->front().guid;
    check(w.completeHarvest(actor,node).allowed && w.inventory(actor)->front().guid==axe,"Tool is retained");
    clearDrops(w);w.updateInventory(actor,{});w.setHarvestable(node,"tree");
    ai::ActionRegistry registry;ai::Config config;config.provider="fallback-only";
    {
        ai::DecisionSystem decisions(w,config);
        check(decisions.assign(actor,"harvest",{{"target",node.toString()}},"player"),"Harvest command accepted");
        decisions.update(0.05,0.05);decisions.cancel(actor,"interrupted");decisions.update(0.05,0.1);
        check(w.resource(node,Resource::Health)->full(),"Cancellation discards unfinished work");
        decisions.assign(actor,"harvest",{{"target",node.toString()}},"player");decisions.update(1,1,true);
        check(w.resource(node,Resource::Health)->full(),"Pause freezes harvesting");
        decisions.step(0.11);check(w.resource(node,Resource::Health)->current()==87.5f,"Step advances one work interval");
    }
    auto ongoing=order(actor,node);
    for(int i=0;i<7;++i)registry.tick(w,ongoing,0.11,config);
    check(ongoing.goal.status=="succeeded" && w.resource(node,Resource::Health)->depleted(),"One order keeps harvesting to depletion");
    clearDrops(w);w.setHarvestable(node,"quick");
    auto a=order(actor,node),b=order(rival,node);
    registry.tick(w,a,0.11,config);registry.tick(w,b,0.11,config);
    check(a.goal.status=="succeeded" && b.goal.result["reason"]=="depleted" && drops(w).size()==8,"Competing harvesters cannot duplicate final burst");
    clearDrops(w);w.setHarvestable(node,"regrow");check(w.completeHarvest(actor,node).allowed,"Deplete regenerating node");
    check(w.at({10,10}).tile(GridLayer::Objects)==regrow.depletedTile,"Depleted artwork applied");
    clearDrops(w);for(int i=0;i<30;++i)w.tick();
    PlaySession play;play.start(w);play.pause(true);play.advance(10);
    check(play.world().harvestable(node)==w.harvestable(node),"Paused timer frozen");play.stop(w);
    if(argc>1){saveMap(argv[1],w,{{"test.png",8,8}});auto loaded=loadMap(argv[1],{{"test.png",8,8}});
        for(int i=0;i<30;++i)loaded.tick();check(loaded.resource(node,Resource::Health)->full(),"Loaded regeneration restores health");}
    for(int i=0;i<30;++i)w.tick();check(w.resource(node,Resource::Health)->full(),"Simulation time regenerates health");
    const auto removable=w.placeObject({8,10},{0,0,0},ObjectType::Gatherable);w.setHarvestable(removable,"remove_tree");
    check(w.completeHarvest(actor,removable).allowed && !w.findObject(removable),"Exhausted empty node removed");
    check(w.undo() && w.harvestable(removable) && w.resource(removable,Resource::Health)->full(),"Removal undo restores source");
    clearDrops(w);
    // Pickup arbitration: cardinal only, player priority, full inventories, preserved IDs and undo.
    SceneWorld pickup(12,12);ground(pickup);pickup.setHistoryEnabled(true);
    const auto player=pickup.spawnCreature({5,4},"player",ControlOwnership::Player);
    const auto ai=pickup.spawnCreature({4,5});
    auto loose=pickup.placeObject({5,5},{0,0,0},ObjectType::Gatherable);pickup.addItem(loose,{"wood","Wood",2});
    const auto stack=pickup.inventory(loose)->front();pickup.setResource(stack.guid,"durability",ResourcePool(10,7));pickup.clearHistory();
    pickup.tick();check(!pickup.findObject(loose) && pickup.inventory(player)->front()==stack && pickup.inventory(ai)->empty(),"Player wins over AI");
    check(pickup.resource(stack.guid,"durability")->current()==7,"Pickup preserves stack resources");
    check(pickup.undo() && pickup.inventory(player)->empty() && pickup.inventory(loose)->front()==stack,"Pickup undo returns stack once");
    check(pickup.redo() && !pickup.findObject(loose) && pickup.inventory(player)->size()==1,"Pickup redo has no duplicate");
    pickup.updateInventory(player,std::vector<Item>(Inventory::maxStacks,{"filler","Filler",1}));
    loose=pickup.placeObject({5,5},{0,0,0},ObjectType::Gatherable);pickup.addItem(loose,{"stone","Stone",1});pickup.tick();
    check(!pickup.findObject(loose) && pickup.inventory(ai)->size()==1,"Full player allows AI pickup");
    pickup.updateInventory(ai,std::vector<Item>(Inventory::maxStacks,{"filler","Filler",1}));
    loose=pickup.placeObject({5,5},{0,0,0},ObjectType::Gatherable);pickup.addItem(loose,{"ore","Ore",1});pickup.tick();
    check(pickup.findObject(loose).has_value(),"No capacity leaves drop on ground");
    pickup.removeCreature(player);pickup.removeCreature(ai);
    const auto diagonal=pickup.spawnCreature({4,4});pickup.tick();check(pickup.findObject(loose).has_value(),"Diagonal does not collect");
    pickup.moveCreature(diagonal,{4,5});pickup.tick();check(!pickup.findObject(loose),"Entering cardinal cell collects");
    const auto other=pickup.spawnCreature({5,4});
    pickup.setHistoryEnabled(false);
    for(int i=0;i<64;++i){auto drop=pickup.placeObject({5,5},{0,0,0},ObjectType::Gatherable);pickup.addItem(drop,{"wood","Wood",1});pickup.tick();}
    check(pickup.inventory(other)->size()>0 && pickup.inventory(diagonal)->size()>1,"Equal priority contenders both win random pickups");
    // Gatherable stacks fill to ten, including when every inventory slot is occupied.
    SceneWorld stacking(4,4);ground(stacking);stacking.setHistoryEnabled(true);
    const auto collector=stacking.spawnCreature({1,0});
    stacking.addItem(collector,{"wood","Wood",9});
    const auto original=stacking.inventory(collector)->front().guid;
    auto drop=stacking.placeObject({1,1},{0,0,0},ObjectType::Gatherable);
    stacking.addItem(drop,{"wood","Wood",12});
    const auto sourceStack=stacking.inventory(drop)->front().guid;
    stacking.clearHistory();stacking.tick();
    auto packed=*stacking.inventory(collector);
    check(!stacking.findObject(drop) && packed.size()==3 && packed[0].quantity==10 &&
          packed[1].quantity==10 && packed[2].quantity==1,"Overflow splits into stacks of ten");
    check(packed[0].guid==original && packed[1].guid==sourceStack,"Existing and transferred identities survive splitting");
    check(stacking.undo() && stacking.inventory(collector)->front().quantity==9 &&
          stacking.inventory(drop)->front().quantity==12,"Undo restores merged and split quantities");
    check(stacking.redo() && *stacking.inventory(collector)==packed,"Redo preserves split stack identities");
    if(argc>1){saveMap(argv[1],stacking,{{"test.png",8,8}});auto loaded=loadMap(argv[1],{{"test.png",8,8}});
        check(*loaded.inventory(collector)==packed,"Merged and split stacks survive save/load");}
    auto fullStacks=std::vector<Item>(Inventory::maxStacks,{"stone","Stone",10});
    fullStacks[0]={"wood","Wood",9};stacking.updateInventory(collector,fullStacks);
    drop=stacking.placeObject({1,1},{0,0,0},ObjectType::Gatherable);stacking.addItem(drop,{"wood","Wood",2});
    stacking.tick();check(stacking.findObject(drop) && stacking.inventory(collector)->front().quantity==9,
                         "Insufficient total room leaves entire pickup unchanged");
    stacking.updateItem(drop,0,{"wood","Wood",1});stacking.tick();
    check(!stacking.findObject(drop) && stacking.inventory(collector)->size()==6 &&
          stacking.inventory(collector)->front().quantity==10,"Matching pickup fits a full six-slot inventory");
    stacking.updateItem(collector,0,{"wood","Wood",9});
    drop=stacking.placeObject({1,1},{0,0,0},ObjectType::Gatherable);stacking.addItem(drop,{"wood","Wood",1});
    stacking.setResource(stacking.inventory(drop)->front().guid,"durability",ResourcePool(10,7));stacking.tick();
    check(stacking.findObject(drop).has_value(),"Different stack resources cannot be silently merged");
    stacking.updateInventory(collector,{});stacking.updateItem(drop,0,{"wood","Wood",21});
    stacking.clearHistory();stacking.tick();
    packed=*stacking.inventory(collector);
    check(packed.size()==3,"Resource-bearing drops also split at ten");
    for(const auto &item:packed)check(stacking.resource(item.guid,"durability")->current()==7,
                                    "Split stacks preserve resource state");
    check(stacking.undo() && stacking.inventory(drop)->front().quantity==21,"Undo restores resource-bearing source");
    check(stacking.redo() && *stacking.inventory(collector)==packed,"Redo restores resource-bearing split identities");
    for(const auto &item:packed)check(stacking.resource(item.guid,"durability")->current()==7,
                                    "Redo restores split resource state");
    // No valid drop cells: no resources consumed, no overwrite.
    SceneWorld blocked(3,3);ground(blocked);const auto worker=blocked.spawnCreature({0,1});
    const auto source=blocked.placeObject({1,1},{0,0,0},ObjectType::Gatherable);blocked.setHarvestable(source,"tree");
    for(int y=0;y<3;++y)for(int x=0;x<3;++x)if(!blocked.creatureAt({x,y}) && !blocked.objectAt({x,y}))blocked.paint({x,y},GridLayer::Walls,{0,0,0});
    check(blocked.completeHarvest(worker,source).allowed,"Work before threshold requires no drops");
    const auto before=blocked.resources(source);
    check(blocked.completeHarvest(worker,source).reason=="no_drop_space" && blocked.resources(source)==before,"Blocked burst preserves health and units");
    auto removed=order(worker,source);blocked.removeObjectById(source);registry.tick(blocked,removed,1,config);
    check(removed.goal.result["reason"]=="target_removed","Deletion during work fails cleanly");
    // Animal carried items remain distinct from butcher outputs.
    const auto animal=w.spawnCreature({20,20},"animal");w.addItem(animal,{"collar","Collar",1});
    const auto collar=w.inventory(animal)->front();w.adjustResource(animal,Resource::Health,-100);w.tick();
    const auto corpse=w.objectAt({20,20});check(corpse && w.harvestable(corpse->id()) && corpse->contents().front()==collar,"Death preserves carried stack");
    check(w.moveCreature(actor,{19,20}),"Position butcher");
    check(w.completeHarvest(actor,corpse->id()).allowed && drops(w).size()==12 && w.inventory(corpse->id())->front()==collar,"Butcher bursts do not duplicate carried loot");
    auto bad=w.document();bad.harvestables.at(corpse->id()).regenerationRemaining=-1;rejects([&]{SceneWorld::fromDocument(bad);});
    std::cout<<"Harvest drops, lifecycle and pickup tests passed.\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
