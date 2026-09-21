#include "PlayerController.h"
#include <iostream>
#include <set>
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int main(){try{
    SceneWorld world(24,16);GridCell ground;ground.tile(GridLayer::Ground)={0,0,0};world.fill(ground);
    const auto a=world.spawnCreature({1,1},"A",ControlOwnership::Player);
    const auto b=world.spawnCreature({1,3},"B",ControlOwnership::Player);
    const auto c=world.spawnCreature({3,1},"C",ControlOwnership::Player);
    const auto npc=world.spawnCreature({3,3});
    const auto dead=world.spawnCreature({5,1},"Dead",ControlOwnership::Player);world.adjustResource(dead,"health",-100);
    Camera2D camera{{130,90},{20,10},0,3};
    ai::Config config;config.provider="fallback-only";config.moveStepSeconds=0.01;
    ai::DecisionSystem decisions(world,config);PlayerController controls;
    const auto screen=[&](CellPosition cell){return GetWorldToScreen2D(world.cellCenter(cell),camera);};
    const auto send=[&](PlayerInput in){controls.update(world,decisions,camera,in,0.01f);};
    const auto click=[&](CellPosition cell,bool shift=false){PlayerInput in;in.mouse=screen(cell);in.shift=shift;in.leftPressed=in.leftDown=true;send(in);in.leftPressed=in.leftDown=false;in.leftReleased=true;send(in);};
    const auto drag=[&](Vector2 from,Vector2 to,bool shift=false){PlayerInput in;in.mouse=GetWorldToScreen2D(from,camera);in.shift=shift;in.leftPressed=in.leftDown=true;send(in);in.leftPressed=false;in.mouse=GetWorldToScreen2D(to,camera);send(in);check(controls.dragging(),"Drag threshold reached");in.leftDown=false;in.leftReleased=true;send(in);};
    const auto right=[&](CellPosition cell){PlayerInput in;in.mouse=screen(cell);in.rightPressed=true;send(in);};
    click({1,1});check(controls.selection()==std::vector<Guid>{a},"Click selects player");
    click({3,3});check(controls.selection().empty(),"NPC cannot be selected");
    click({5,1});check(controls.selection().empty(),"Dead player cannot be selected");
    click({1,1});click({1,3},true);check(controls.selection().size()==2,"Shift adds a player");
    click({1,1},true);check(controls.selection()==std::vector<Guid>{b},"Shift click toggles existing player");
    click({8,8},true);check(controls.selection()==std::vector<Guid>{b},"Shift empty click preserves selection");
    // Reverse-direction marquee, excluding NPC and dead player, under pan and zoom.
    drag({48,40},{0,0});check(controls.selection().size()==3,"Drag selects only living player units in either direction");
    drag({8,8},{16,16},true);check(controls.selection().size()==3,"Shift drag adds without toggling existing members");
    PlayerInput blocked;blocked.mouse=screen({10,10});blocked.leftPressed=blocked.leftDown=true;blocked.worldInput=false;send(blocked);
    blocked.worldInput=true;blocked.leftDown=blocked.leftPressed=false;blocked.leftReleased=true;send(blocked);
    check(controls.selection().size()==3,"Press over UI cannot initiate world selection");
    PlayerInput start;start.mouse=screen({1,1});start.leftPressed=start.leftDown=true;send(start);
    PlayerInput lost;lost.focused=false;send(lost);check(!controls.dragging(),"Focus loss cancels drag");
    world.removeCreature(npc);
    right({12,8});
    std::map<Guid,CellPosition,decltype([](Guid x,Guid y){return x.bytes<y.bytes;})> targets;
    std::set<std::pair<int,int>> cells;
    for(auto id:controls.selection()) {
        const auto goal=decisions.current(id);check(goal && goal->action=="move","Group gets move orders");
        const auto &p=goal->parameters.at("destination");CellPosition cell{p.at("x"),p.at("y")};
        targets.emplace(id,cell);check(cells.emplace(cell.x,cell.y).second,"Group destinations are distinct");
    }
    decisions.update(1,1,true);check(world.findCreature(a)->position().cellCoordinates()==CellPosition{1,1},"Paused orders do not move units");
    decisions.step(0.01);
    double time=1;
    for(int i=0;i<100;++i){time+=0.02;decisions.update(0.02,time);}
    for(const auto &[id,target]:targets)check(world.findCreature(id)->position().cellCoordinates()==target,"Group arrives through timed pathfinding");
    right({18,10});
    for(auto id:controls.selection())check(decisions.current(id).has_value(),"New group command replaces old orders");
    PlayerInput stop;stop.cancel=true;send(stop);
    for(auto id:controls.selection())check(!decisions.current(id),"Stop cancels all selected orders");
    click(world.findCreature(a)->position().cellCoordinates());
    world.paint({22,14},GridLayer::Walls,{0,0,0});right({22,14});
    check(!decisions.current(a) && controls.status().find("0 / 1")!=std::string::npos,"Unreachable single-unit move reports failure");
    const auto node=world.placeObject({16,8},{0,0,0},ObjectType::Gatherable);world.setHarvestable(node,"tree");
    right({16,8});check(decisions.current(a)->action=="harvest","Right-click harvestable issues interaction");
    PlayerInput ui;ui.worldInput=false;ui.keyboardInput=false;ui.cancel=true;ui.rightPressed=true;ui.mouse=screen({20,10});send(ui);
    check(decisions.current(a)->action=="harvest","UI and typing do not issue or cancel world commands");
    const auto victim=world.spawnCreature({17,8},"target");
    right({17,8});check(decisions.current(a)->action=="attack" && decisions.current(a)->parameters["target"]==victim.toString(),"Right-click entity issues attack");
    send(stop);check(!decisions.current(a),"Stop cancels player attack");
    click({23,15});check(controls.selection().empty(),"Click empty terrain deselects");
    click(world.findCreature(b)->position().cellCoordinates());world.removeCreature(b);send({});
    check(controls.selection().empty(),"Deleted units are pruned from selection");
    click(world.findCreature(c)->position().cellCoordinates());world.adjustResource(c,"health",-100);send({});
    check(controls.selection().empty(),"Dead units are pruned from selection");
    controls.clear();check(controls.selection().empty() && controls.status().empty(),"Session reset clears transient selection");
    std::cout<<"RTS player controls tests passed.\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
