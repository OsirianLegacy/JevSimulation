#include "Decision.h"
#include "MapStorage.h"
#include "PlaySession.h"
#include "EntityPresentation.h"
#include <algorithm>
#include <fstream>
#include <iostream>
using namespace scene;
using namespace ai;
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("Invalid combat data accepted");}
void ground(SceneWorld &w){GridCell c;c.tile(GridLayer::Ground)={0,0,0};w.fill(c);}
Execution order(Guid actor,Guid target){Execution e;e.goal={"combat","attack","player","pending",actor,{{"target",target.toString()}},nullptr};return e;}
int main(int argc,char **argv){try{
    SceneWorld w(12,12);ground(w);w.setHistoryEnabled(true);
    auto catalog=w.creatureCatalog();catalog.species.at(0).defaultDisposition=Disposition::Friendly;
    auto wolf=catalog.species.at(0);wolf.name="Wolf";wolf.defaultDisposition=Disposition::Hostile;wolf.carcassHarvest="carcass";
    auto deer=wolf;deer.name="Deer";deer.defaultDisposition=Disposition::Neutral;
    catalog.species[1]=wolf;catalog.species[2]=deer;w.setCreatureCatalog(catalog);
    check(CreatureCatalog::fromJson(catalog.json())==catalog,"Species dispositions round trip");
    auto legacyCatalog=catalog.json();for(auto &species:legacyCatalog["species"])species.erase("defaultDisposition");
    check(CreatureCatalog::fromJson(legacyCatalog).species.at(1).defaultDisposition==Disposition::Neutral,"Legacy species default neutral");
    auto invalid=catalog.json();invalid["species"][0]["defaultDisposition"]="angry";
    rejects([&]{CreatureCatalog::fromJson(invalid);});
    const auto player=w.spawnCreature({1,1},"player",ControlOwnership::Player);
    const auto enemy=w.spawnCreature({3,1},{static_cast<Species>(1),SubSpecies::Default},{},ControlOwnership::AI);
    const auto animal=w.spawnCreature({1,4},{static_cast<Species>(2),SubSpecies::Default},{},ControlOwnership::AI);
    check(w.findCreature(player)->disposition()==Disposition::Friendly && w.findCreature(enemy)->disposition()==Disposition::Hostile &&
          w.findCreature(animal)->disposition()==Disposition::Neutral,"Each spawn inherits its species default");
    check(w.isEnemy(player,enemy) && w.isEnemy(enemy,animal) && !w.isEnemy(player,animal),"Disposition enemy relationships");
    w.setCreatureDisposition(animal,Disposition::Friendly);
    check(w.undo() && w.findCreature(animal)->disposition()==Disposition::Neutral,"Disposition undo");
    check(w.redo() && w.findCreature(animal)->disposition()==Disposition::Friendly,"Disposition redo");
    w.setCreatureDisposition(animal,Disposition::Neutral);
    rejects([&]{w.setCreatureDisposition(animal,static_cast<Disposition>(99));});
    auto doc=w.document();doc.creatures[0].disposition=static_cast<Disposition>(99);
    rejects([&]{SceneWorld::fromDocument(doc);});
    catalog.species.at(1).defaultDisposition=Disposition::Neutral;w.setCreatureCatalog(catalog);
    check(w.findCreature(enemy)->disposition()==Disposition::Hostile,"Default changes preserve existing entity state");
    w.undo();
    check(EntityPresentation::inspectionData(*w.findCreature(enemy),nullptr,8)["disposition"]=="hostile","Inspector includes disposition");
    if(argc>1){
        saveMap(argv[1],w,{{"test.png",8,8}});auto loaded=loadMap(argv[1],{{"test.png",8,8}});
        check(loaded.findCreature(enemy)->disposition()==Disposition::Hostile,"Map preserves individual disposition");
        std::filesystem::resize_file(argv[1],std::filesystem::file_size(argv[1])-4*w.creatureIds().size());
        {std::fstream file(argv[1],std::ios::binary|std::ios::in|std::ios::out);file.seekp(8);file.put(10);}
        auto old=loadMap(argv[1],{{"test.png",8,8}});
        check(old.findCreature(enemy)->disposition()==Disposition::Hostile,"Version 10 derives species defaults");
    }
    PlaySession play;play.start(w);play.world().setCreatureDisposition(enemy,Disposition::Neutral);
    check(w.findCreature(enemy)->disposition()==Disposition::Hostile,"Play changes do not leak");play.stop(w);
    check(!w.canAttack(player,player).allowed && !w.canAttack(player,enemy).allowed,"Self and distant attacks rejected");
    ActionRegistry actions;Config config;config.provider="fallback-only";config.moveStepSeconds=0.1;
    auto attack=order(player,enemy);actions.tick(w,attack,0.1,config);
    check(w.findCreature(player)->position().cellCoordinates()==CellPosition{2,1},"Attack approaches a cardinal neighbor");
    actions.tick(w,attack,0.5,config);check(w.resource(enemy,Resource::Health)->full(),"Attack waits for cooldown");
    actions.tick(w,attack,0.5,config);check(w.resource(enemy,Resource::Health)->current()==90,"Attack deals ten damage");
    check(w.undo() && w.resource(enemy,Resource::Health)->full(),"Damage is undoable");w.redo();
    w.moveCreature(enemy,{4,1});actions.tick(w,attack,0.1,config);
    check(w.findCreature(player)->position().cellCoordinates()==CellPosition{3,1},"Attack follows a moving target");
    for(int i=0;i<9;++i)actions.tick(w,attack,1,config);
    check(attack.goal.status=="succeeded" && w.resource(enemy,Resource::Health)->depleted(),"Attack repeats until defeated");
    actions.tick(w,attack,1,config);check(w.resource(enemy,Resource::Health)->current()==0,"Completed attack cannot damage twice");
    w.tick();check(!w.findCreature(enemy) && w.objectAt({4,1}) && w.harvestable(w.objectAt({4,1})->id()),"Combat death creates configured carcass");
    w.setCreatureDisposition(animal,Disposition::Hostile);
    auto removed=order(player,animal);w.removeCreature(animal);actions.tick(w,removed,1,config);
    check(removed.goal.result["reason"]=="target_removed","Deleted targets fail cleanly");
    SceneWorld sight(8,8);ground(sight);
    const auto guard=sight.spawnCreature({1,1},"guard",ControlOwnership::AI);
    const auto threat=sight.spawnCreature({3,1},"wolf",ControlOwnership::AI);
    const auto neutral=sight.spawnCreature({1,3},"deer",ControlOwnership::AI);
    sight.setCreatureDisposition(guard,Disposition::Friendly);sight.setCreatureDisposition(threat,Disposition::Hostile);
    check(sight.hasLineOfSight({1,1},{3,1},true) && !sight.hasLineOfSight({1,1},{3,1}),"Entity perception allows occupied endpoints only");
    sight.paint({2,1},GridLayer::Walls,{0,0,0});
    auto visible=sight.nearbyCreatures(guard,8);
    check(std::find(visible.begin(),visible.end(),threat)==visible.end(),"Walls hide entities");
    sight.erase({2,1},GridLayer::Walls);
    DecisionContextBuilder builder;std::mt19937 rng(5);
    auto context=builder.build(sight,guard,nullptr,"s","r",0,config,actions,rng);
    check(context["nearbyEntities"].size()==2 && context["self"]["identity"]["disposition"]=="friendly","Jev sees self and nearby dispositions");
    auto candidate=context["candidates"][0];
    check(candidate["action"]=="attack" && candidate["parameters"]["target"]==threat.toString(),"Jev receives validated attack candidate");
    check(context["nearbyEntities"][0].contains("health") && context["nearbyEntities"][0].contains("cell"),"Jev sees nearby health and position");
    config.payloadBytes=2048;context=builder.build(sight,guard,nullptr,"s","r",0,config,actions,rng);
    check(context.dump().size()<=2048 && context["candidates"].back()["action"]=="wait","Perception obeys payload cap");config.payloadBytes=8192;
    {
        DecisionSystem local(sight,config);local.update(0.01,0.01);
        check(local.current(guard) && local.current(guard)->action=="attack","Friendly local AI targets hostile entities");
        check(local.current(threat) && local.current(threat)->action=="attack","Hostile local AI targets non-hostile entities");
        check(local.current(neutral) && local.current(neutral)->action!="attack","Neutral local AI stays passive");
    }
    {
        Config remote=config;remote.provider="fake";DecisionSystem jev(sight,remote);
        for(int i=0;i<10 && (!jev.current(guard) || jev.current(guard)->source!="jev");++i)jev.update(0.01,i*0.2);
        check(jev.current(guard) && jev.current(guard)->action=="attack" && jev.current(guard)->source=="jev","Jev selection enters shared attack executor");
    }
    SceneWorld controls(5,5);ground(controls);
    const auto a=controls.spawnCreature({1,1},"a",ControlOwnership::Player);
    const auto b=controls.spawnCreature({2,1},"b",ControlOwnership::Player);
    DecisionSystem orders(controls,config);check(orders.assign(a,"attack",{{"target",b.toString()}},"player"),"Explicit orders can attack neutral entities");
    orders.update(2,2,true);check(controls.resource(b,Resource::Health)->full(),"Pause freezes attacks");
    orders.step(1);check(controls.resource(b,Resource::Health)->current()==90,"Single step executes attacks");
    orders.cancel(a);orders.step(1);check(controls.resource(b,Resource::Health)->current()==90,"Cancel stops attacks");
    rejects([&]{controls.nearbyCreatures(a,129);});
    auto dead=order(a,b);controls.adjustResource(a,Resource::Health,-100);actions.tick(controls,dead,1,config);
    check(dead.goal.status=="cancelled" && dead.goal.result["reason"]=="actor_dead","Dead attackers cannot act");
    std::cout<<"Combat, disposition and perception tests passed.\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
