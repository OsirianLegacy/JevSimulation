#include "Decision.h"
#include "MapStorage.h"
#include "PlaySession.h"
#include <iostream>
#include <set>
#include <fstream>
using namespace ai;
void check(bool condition,const char *message) { if (!condition) throw std::runtime_error(message); }
template<class F> void rejects(F f) { bool caught=false; try { f(); } catch(const std::exception&) { caught=true; } check(caught,"Invalid operation accepted"); }
void ground(SceneWorld &w) { GridCell cell; cell.tile(GridLayer::Ground)={0,0,0}; w.fill(cell); }
class Controlled : public Transport {
  public:
    Json request; std::optional<Json> response; int submissions=0, cancels=0; bool pending=false;
    bool submit(const Json &r) override { check(!pending,"Two requests in flight"); request=r; pending=true; ++submissions; return true; }
    std::optional<Json> poll() override { auto r=response; if(r) { response.reset(); pending=false; } return r; }
    void cancel() override { pending=false; response.reset(); ++cancels; }
    void answer(bool stale=false) { response={{"version",1},{"session",stale?Json("old"):request.at("session")},
        {"request",request.at("request")},{"revision",request.at("revision")},{"candidate",request.at("candidates").back().at("id")}}; }
};
int main(int argc,char **argv) {
 try {
    {
        SceneWorld speciesWorld(8,8);ground(speciesWorld);speciesWorld.setHistoryEnabled(true);
        auto catalog=speciesWorld.creatureCatalog();
        catalog.species.at(0).resources["thirst"]=100;
        scene::SpeciesDefinition zombie;zombie.name="Zombie";zombie.sheet="Monsters.png";zombie.subSpecies.emplace(1,"Walker");
        // Undead explicitly opt out of the living species' default needs.
        zombie.resources={{scene::Resource::Health,100}};
        catalog.species.emplace(1,zombie);catalog.vocations.emplace(1,scene::VocationDefinition{"Guard",{{0,{}}}});
        speciesWorld.setCreatureCatalog(catalog);
        check(speciesWorld.canUndo(),"Catalog edits enter undo history");speciesWorld.undo();check(speciesWorld.creatureCatalog().species.size()==1,"Undo catalog");speciesWorld.redo();
        const auto human=speciesWorld.spawnCreature({0,0},scene::SpeciesComponent{},scene::VocationComponent{1},ControlOwnership::Player);
        const scene::SpeciesComponent undead{static_cast<scene::Species>(1),static_cast<scene::SubSpecies>(1)};
        const auto z=speciesWorld.spawnCreature({1,0},undead,{},ControlOwnership::AI);
        check(speciesWorld.findCreature(human)->resources().find("thirst") && !speciesWorld.findCreature(z)->resources().find("thirst"),"Species controls resources");
        rejects([&]{speciesWorld.setCreatureIdentity(z,undead,{1});});
        auto invalid=catalog;invalid.vocations.at(1).species.clear();rejects([&]{speciesWorld.setCreatureCatalog(invalid);});
        speciesWorld.setCreatureIdentity(human,undead,{});check(!speciesWorld.findCreature(human)->resources().find("thirst"),"Reassignment removes irrelevant resources");
        speciesWorld.undo();check(speciesWorld.findCreature(human)->vocation().id==1 && speciesWorld.findCreature(human)->resources().find("thirst"),"Undo restores components");
        auto changed=catalog;changed.species.at(0).resources["thirst"]=50;speciesWorld.setCreatureCatalog(changed);
        check(speciesWorld.findCreature(human)->resources().find("thirst")->maximum()==50,"Catalog resource changes reconcile instances");
        const auto document=speciesWorld.document();auto cloned=SceneWorld::fromDocument(document);
        check(cloned.findCreature(z)==speciesWorld.findCreature(z) && cloned.creatureCatalog()==changed,"Species and vocation survive cloning");
        if(argc>1){speciesWorld.placeObject({3,3},{0,0,0},ObjectType::Container);
            const std::string path=std::string(argv[1])+".species";saveMap(path,speciesWorld,{{"test.png",8,8}});
            auto loaded=loadMap(path,{{"test.png",8,8}});check(loaded.findCreature(z)==speciesWorld.findCreature(z) && loaded.creatureCatalog()==changed && loaded.objectCount()==1,"Mixed object/creature map roundtrip");
            std::ifstream input(path,std::ios::binary);std::string legacy{std::istreambuf_iterator<char>(input),{}};input.close();
            // Strip v7 bindings and v8's empty inventory count for each creature.
            std::size_t nameBytes=4;for(const auto &[id,name]:speciesWorld.document().humanNames)nameBytes+=24+name.first.size()+name.last.size();
            legacy.resize(legacy.size()-nameBytes-8-changed.json().dump().size()-speciesWorld.creatureIds().size()*20);legacy[8]=6;
            std::ofstream output(path+".v6",std::ios::binary);output.write(legacy.data(),legacy.size());output.close();
            auto old=loadMap(path+".v6",{{"test.png",8,8}});check(old.findCreature(z)->species().species==scene::Species::Human && old.findCreature(human)->vocation().id==0,"Version 6 receives default components");}
        check(scene::CreatureCatalog::fromJson(changed.json())==changed,"Definition JSON roundtrip");
    }
    SceneWorld w(96,32); ground(w); w.setHistoryEnabled(true);
    auto player=w.spawnCreature({0,0},"player",ControlOwnership::Player);
    auto actor=w.spawnCreature({31,1},"wolf",ControlOwnership::AI,scene::ResourcePool(80,60));
    check(w.chunkOf({31,31})==CellPosition{0,0} && w.chunkOf({32,0})==CellPosition{1,0},"Chunk boundary");
    check(w.creaturesInChunk({0,0}).size()==2 && !w.isWalkable({31,1}),"Indexed occupancy");
    auto original=w.findCreature(actor);
    check(w.moveCreature(actor,{32,1}),"Move across chunk");
    check(w.creaturesInChunk({1,0}).size()==1 && !w.creatureAt({31,1}),"Chunk move removes old entry");
    check(w.undo() && w.findCreature(actor)==original,"Move undo");
    check(w.redo() && w.findCreature(actor)->position().cellCoordinates()==CellPosition{32,1},"Move redo");
    check(!w.moveCreature(actor,{0,0}),"Occupied destination blocked");
    rejects([&]{w.removeResource(actor,"health");});
    auto object=w.placeObject({63,2},{0,0,0},ObjectType::Container);
    check(w.objectsInChunk({1,0}).size()==1,"Object index");
    check(w.moveObjectById(object,{64,2}) && w.objectsInChunk({1,0}).empty(),"Object index move");
    w.undo(); check(w.objectsInChunk({1,0}).size()==1 && w.objectsInChunk({2,0}).empty(),"Object index undo");
    w.removeObjectById(object); check(w.objectsInChunk({1,0}).empty(),"Object index remove");
    w.removeCreature(actor); check(!w.findCreature(actor),"Creature remove");
    check(w.undo() && w.resource(actor,"health")->current()==60,"Creature removal undo restores health");
    PlaySession play; play.start(w); play.world().moveCreature(actor,{33,1});
    check(w.findCreature(actor)->position().cellCoordinates()==CellPosition{32,1},"Play clone isolation"); play.stop(w);
    if(argc>1) { saveMap(argv[1],w,{{"test.png",8,8}}); auto loaded=loadMap(argv[1],{{"test.png",8,8}});
        check(loaded.findCreature(actor)==w.findCreature(actor) && loaded.findCreature(player)->control()==ControlOwnership::Player,"Creature save roundtrip");
        check(loaded.creaturesInChunk({1,0}).size()==1,"Load rebuilds chunks"); }
    auto bad=w.document(); bad.creatures.push_back(bad.creatures.front()); rejects([&]{ SceneWorld::fromDocument(bad); });
    bad=w.document(); bad.resources.at(actor).remove("health"); rejects([&]{ SceneWorld::fromDocument(bad); });
    w.setHistoryEnabled(false);
    Config c; c.provider="fallback-only"; c.moveStepSeconds=0.01; c.waitSeconds=0.05;
    DecisionSystem local(w,c);
    check(local.assign(actor,"move",{{"destination",{{"x",35},{"y",1}}}}),"Assign move");
    // Exercise the registry directly to observe terminal outcomes before the scheduler starts another goal.
    Execution move; move.goal={"g","move","fallback","pending",actor,{{"destination",{{"x",35},{"y",1}}}},nullptr};
    ActionRegistry registry;
    for(int i=0;i<10 && move.goal.status!="succeeded";++i) registry.tick(w,move,0.01,c);
    check(move.goal.status=="succeeded" && w.findCreature(actor)->position().cellCoordinates()==CellPosition{35,1},"Move completes");
    Execution blocked; blocked.goal={"b","move","jev","pending",actor,{{"destination",{{"x",36},{"y",1}}}},nullptr};
    w.paint({36,1},GridLayer::Walls,{0,0,0});
    for(int i=0;i<3;++i) registry.tick(w,blocked,0.01,c);
    check(blocked.goal.status=="failed" && blocked.goal.result.at("reason")=="unreachable" && blocked.reroutes==3,"Bounded reroutes then failure");
    Execution wait; wait.goal={"wait","wait","jev","pending",actor,{{"seconds",0.02}},nullptr};
    registry.tick(w,wait,0.01,c); check(wait.goal.status=="running","Wait not premature");
    registry.tick(w,wait,0.01,c); check(wait.goal.status=="succeeded","Wait completion");
    check(Json::parse(wait.goal.json().dump())==wait.goal.json(),"Goal JSON roundtrip");
    local.cancel(actor); check(local.lastOutcome(actor).at("status")=="cancelled","Explicit cancellation");
    DecisionContextBuilder builder; std::mt19937 random(3); c.payloadBytes=2048;
    auto context=builder.build(w,actor,blocked.goal.json(),"s","r",1,c,registry,random);
    check(context.dump().size()<=2048 && context.at("candidates").size()<=4 && context.at("candidates").back().at("action")=="wait","Bounded context with wait");
    builder.add("large","Required extension",[](const SceneWorld&,Guid){ return Json(std::string(9000,'x')); });
    rejects([&]{builder.build(w,actor,nullptr,"s","r",1,c,registry,random);});
    c.payloadBytes=8192; c.provider="proxy";
    auto controlled=std::make_unique<Controlled>(); auto *remote=controlled.get();
    DecisionSystem connected(w,c,std::move(controlled));
    connected.update(0.02,0.02); check(connected.inFlight() && remote->submissions==1,"Single remote dispatch");
    auto sent=remote->request;
    connected.update(0.02,0.04); check(remote->submissions==1,"No duplicate while waiting");
    remote->answer(); connected.update(0.01,0.05);
    check(connected.current(actor)->source=="jev","Valid response accepted");
    connected.update(0.02,0.07,true); check(connected.current(actor)->status=="pending","Pause stops executor");
    connected.step(0.01); check(remote->submissions==1,"Paused step cannot dispatch requests");
    connected.update(0.06,0.13); check(connected.lastOutcome(actor).at("status")=="succeeded","Outcome retained for next decision");
    connected.invalidate(); connected.update(0.01,1); remote->answer(true); connected.update(0.01,1.01);
    check(connected.metrics()["bands"][1]["staleResponses"]==1,"Stale response rejected");
    check(connected.current(actor)->source=="fallback","Stale response falls back");
    connected.invalidate(); connected.update(0.01,2); connected.update(0.01,4.1);
    check(!connected.inFlight() && connected.current(actor)->source=="fallback","Timeout fallback");
    const int attempts=remote->submissions; connected.update(0.01,4.2); check(remote->submissions==attempts,"Backoff suppresses immediate retry");
    auto oldSession=connected.session(); connected.invalidate(); check(connected.session()!=oldSession && !connected.inFlight(),"Session reset");
    connected.update(0.01,5); connected.update(0.01,5.1,true); check(!connected.inFlight(),"Pause invalidates request");
    // A response from a superseded request must not cancel the newer goal.
    connected.invalidate(); connected.update(0.01,7);
    check(connected.assign(actor,"wait",{{"seconds",10}},"fallback"),"Replace pending decision");
    const auto replacement=connected.current(actor)->id;
    remote->answer(); connected.update(0.01,7.01);
    check(connected.current(actor)->id==replacement,"Stale response preserves newer goal");
    // Next dispatch carries the exact completed record, rather than an accumulating history.
    {
        SceneWorld loop(3,1); ground(loop); auto id=loop.spawnCreature({0,0});
        Config quick; quick.provider="proxy"; quick.waitSeconds=0.01;
        for(auto &b:quick.bands) b.interval=0.01;
        auto t=std::make_unique<Controlled>(); auto *handle=t.get(); DecisionSystem system(loop,quick,std::move(t));
        system.update(0.01,0); handle->answer(); system.update(0.01,0.01);
        const auto goal=system.current(id)->id;
        system.update(0.2,0.21);
        check(handle->request.at("previousGoal").at("id")==goal && handle->request.at("previousGoal").at("status")=="succeeded",
              "Next request carries prior execution outcome");
    }
    // All distance bands get service, weighted toward the player's chunk.
    {
        SceneWorld bands(320,2); ground(bands); bands.spawnCreature({0,0},"player",ControlOwnership::Player);
        for(int x : {1,33,97,225,289}) bands.spawnCreature({x,1});
        Config fair; fair.provider="fake"; fair.candidateCount=1; fair.waitSeconds=0.001; fair.queueWait=100;
        for(auto &b:fair.bands) b.interval=0.001;
        DecisionSystem scheduler(bands,fair);
        for(int tick=0;tick<320;++tick) { scheduler.update(0.11,tick*0.11); check(scheduler.queuedCount()<=5,"Deduplicated queue"); }
        auto stats=scheduler.metrics().at("bands");
        for(const auto &band:stats) check(band.at("requests").get<int>()>0,"Every distance band serviced");
        check(stats[0].at("requests").get<int>()>stats[4].at("requests").get<int>(),"Nearby band receives more requests");
    }
    // A newly blocked path reroutes around the obstacle before reporting failure.
    {
        SceneWorld routes(8,3); ground(routes); auto id=routes.spawnCreature({0,1});
        Execution e; e.goal={"reroute","move","fallback","pending",id,{{"destination",{{"x",4},{"y",1}}}},nullptr};
        registry.tick(routes,e,0.01,c); routes.paint({2,1},GridLayer::Walls,{0,0,0});
        for(int n=0;n<20 && e.goal.status!="succeeded";++n) registry.tick(routes,e,0.01,c);
        check(e.goal.status=="succeeded" && e.reroutes==1,"Recoverable blockage reroutes locally");
    }
    // Multiple player areas use their nearest chunk, without duplicate entity registration.
    auto second=w.spawnCreature({65,0},"player",ControlOwnership::Player); connected.update(0,6,true);
    connected.update(0.01,6.01); check(connected.bandFor(actor)==1,"Nearest player band");
    auto far=w.spawnCreature({90,2}); connected.update(0.01,6.02); check(connected.bandFor(far)==0,"Second player area");
    w.removeCreature(player); w.removeCreature(second); connected.update(0.01,6.03); check(connected.bandFor(far)==4,"No player uses distant band");
    // Bounded admission and offline motion for a larger population.
    SceneWorld many(80,80); ground(many);
    for(int i=0;i<1000;++i) many.spawnCreature({i%40*2,i/40*2});
    Config offline; offline.provider="fallback-only"; DecisionSystem load(many,offline);
    for(int t=0;t<180;++t) { load.update(1.0/60,t/60.0); check(load.lastBatchSize()<=32 && !load.inFlight(),"Bounded fallback scheduling"); }
    check(load.metrics()["bands"][4]["fallback"].get<int>()>=1000,"Every offline creature receives a goal");
    if (argc>1) {
        SceneWorld full;
        check(full.width()==2048 && full.height()==2048,"New world dimensions");
        full.paint({2047,2047},GridLayer::Ground,{0,0,0}); auto last=full.spawnCreature({2047,2047});
        check(full.chunkOf({2047,2047})==CellPosition{63,63},"Final chunk");
        const auto path=std::string(argv[1])+".large";
        saveMap(path,full,{{"test.png",8,8}});
        auto loaded=loadMap(path,{{"test.png",8,8}});
        check(loaded.findCreature(last)==full.findCreature(last),"Full-size map roundtrip");
    }
    std::cout<<"Decision and creature tests passed\n";
 } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
