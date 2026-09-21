#include "PlayerController.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace {
bool selectable(const std::optional<Entity> &e) {
    return e && e->control()==ControlOwnership::Player && !e->health().depleted();
}
Rectangle bounds(Vector2 a,Vector2 b) {
    return {std::min(a.x,b.x),std::min(a.y,b.y),std::abs(a.x-b.x),std::abs(a.y-b.y)};
}
bool contains(Rectangle r,Vector2 p) {
    return p.x>=r.x && p.x<=r.x+r.width && p.y>=r.y && p.y<=r.y+r.height;
}
}
void PlayerController::clear() {
    selected_.clear();selecting_=dragged_=false;markerSeconds_=0;status_.clear();
}
void PlayerController::prune(const SceneWorld &world) {
    std::erase_if(selected_,[&](Guid id){return !selectable(world.findCreature(id));});
}
void PlayerController::finishSelection(const SceneWorld &world,const Camera2D &camera,Vector2 mouse) {
    std::vector<Guid> hits;
    if(dragged_) {
        const auto area=bounds(startWorld_,endWorld_);
        const auto cells=world.visibleRange(area);
        if(!cells.empty())for(int cy=cells.minY/SceneWorld::chunkSize;cy<=(cells.maxY-1)/SceneWorld::chunkSize;++cy)
            for(int cx=cells.minX/SceneWorld::chunkSize;cx<=(cells.maxX-1)/SceneWorld::chunkSize;++cx)
                for(auto id:world.creaturesInChunk({cx,cy})) {
                    const auto e=world.findCreature(id);
                    if(selectable(e) && contains(area,e->position().worldPosition()))hits.push_back(id);
                }
    } else if(auto cell=world.worldToCell(GetScreenToWorld2D(mouse,camera))) {
        if(auto id=world.creatureAt(*cell);id && selectable(world.findCreature(*id)))hits.push_back(*id);
    }
    if(!additive_)selected_.clear();
    for(auto id:hits) {
        const auto it=std::find(selected_.begin(),selected_.end(),id);
        if(it==selected_.end())selected_.push_back(id);
        else if(additive_ && !dragged_)selected_.erase(it);
    }
    std::sort(selected_.begin(),selected_.end(),[](Guid a,Guid b){return a.bytes<b.bytes;});
    status_=std::to_string(selected_.size())+" player unit(s) selected";
    selecting_=dragged_=false;
}
void PlayerController::command(SceneWorld &world,ai::DecisionSystem &decisions,CellPosition target) {
    if(selected_.empty()) {status_="Select player units first.";return;}
    markerSeconds_=1;commandPoint_=world.cellCenter(target);
    std::size_t issued=0;
    const auto object=world.objectAt(target);
    if(const auto victim=world.creatureAt(target);victim) {
        for(auto id:selected_)if(decisions.assign(id,"attack",{{"target",victim->toString()}},"player"))++issued;
        status_="Attack: "+std::to_string(issued)+" / "+std::to_string(selected_.size())+" ordered";
    } else if(object && world.harvestable(object->id())) {
        for(auto id:selected_)if(decisions.assign(id,"harvest",{{"target",object->id().toString()}},"player"))++issued;
        status_="Harvest: "+std::to_string(issued)+" / "+std::to_string(selected_.size())+" ordered";
    } else {
        // Use spaced, unique slots around the click. Assign nearby units first and test
        // routes before accepting a slot. Never teleport or route through blocked terrain.
        auto pending=selected_;
        std::sort(pending.begin(),pending.end(),[&](Guid a,Guid b) {
            const auto pa=world.findCreature(a)->position().cellCoordinates(),pb=world.findCreature(b)->position().cellCoordinates();
            const int da=std::abs(pa.x-target.x)+std::abs(pa.y-target.y),db=std::abs(pb.x-target.x)+std::abs(pb.y-target.y);
            return da==db?a.bytes<b.bytes:da<db;
        });
        std::vector<CellPosition> slots;
        const int spacing=selected_.size()>1?2:1;
        const int radius=selected_.size()>1?std::min(32,std::max(4,int(std::ceil(std::sqrt(double(selected_.size())))))):0;
        for(int r=0;r<=radius;++r)for(int y=-r;y<=r;++y)for(int x=-r;x<=r;++x) {
            if(std::max(std::abs(x),std::abs(y))!=r)continue;
            CellPosition p{target.x+x*spacing,target.y+y*spacing};
            if(world.contains(p))slots.push_back(p);
        }
        if(object && object->type()==ObjectType::Gatherable && !object->contents().empty())slots=world.neighbors(target);
        std::set<std::pair<int,int>> reserved;
        for(auto id:pending) {
            int attempted=0;
            for(auto slot:slots) {
                if(reserved.contains({slot.x,slot.y}))continue;
                const auto actorCell=world.findCreature(id)->position().cellCoordinates();
                if(!world.isWalkable(slot) && slot!=actorCell)continue;
                if(++attempted>64)break; // Bound work for unreachable formations.
                if(decisions.assign(id,"move",{{"destination",{{"x",slot.x},{"y",slot.y}}}},"player")) {
                    reserved.emplace(slot.x,slot.y);++issued;break;
                }
            }
        }
        status_="Move: "+std::to_string(issued)+" / "+std::to_string(selected_.size())+" ordered";
    }
    commandAccepted_=issued>0;
    if(issued<selected_.size())status_+=" (some units cannot reach or interact)";
}
void PlayerController::update(SceneWorld &world,ai::DecisionSystem &decisions,const Camera2D &camera,const PlayerInput &in,float dt) {
    prune(world);
    if(std::isfinite(dt) && dt>0)markerSeconds_=std::max(0.0f,markerSeconds_-dt);
    if(!in.focused){selecting_=dragged_=false;return;}
    if(in.cancel && in.keyboardInput) {
        for(auto id:selected_)decisions.cancel(id,"player_cancelled");
        selecting_=dragged_=false;status_="Selected units stopped.";
    }
    if(!in.worldInput){selecting_=dragged_=false;return;}
    if(in.leftPressed) {
        selecting_=true;dragged_=false;additive_=in.shift;
        startScreen_=in.mouse;startWorld_=endWorld_=GetScreenToWorld2D(in.mouse,camera);
    }
    if(selecting_) {
        endWorld_=GetScreenToWorld2D(in.mouse,camera);
        if(std::hypot(in.mouse.x-startScreen_.x,in.mouse.y-startScreen_.y)>=5)dragged_=true;
        if(in.leftReleased)finishSelection(world,camera,in.mouse);
        else if(!in.leftDown && !in.leftPressed)selecting_=dragged_=false;
    }
    if(in.rightPressed) {
        selecting_=dragged_=false;
        if(auto cell=world.worldToCell(GetScreenToWorld2D(in.mouse,camera)))command(world,decisions,*cell);
        else status_="Destination is outside the world.";
    }
}
void PlayerController::drawWorld(const SceneWorld &world,const Camera2D &camera,const ai::DecisionSystem &decisions) const {
    for(auto id:selected_)if(auto actor=world.findCreature(id)) {
        DrawRectangleLinesEx(world.cellBounds(actor->position().cellCoordinates()),2/camera.zoom,{85,230,140,255});
        if(auto goal=decisions.current(id);goal && goal->action=="move") {
            const auto &p=goal->parameters.at("destination");
            const auto point=world.cellCenter({p.at("x"),p.at("y")});
            DrawLineV(actor->position().worldPosition(),point,{85,230,140,95});
            DrawCircleLines(int(point.x),int(point.y),3/camera.zoom,{85,230,140,180});
        }
    }
    if(markerSeconds_>0) {
        const auto color=commandAccepted_?Color{85,230,140,255}:Color{240,100,85,255};
        DrawCircleLines(int(commandPoint_.x),int(commandPoint_.y),(5+markerSeconds_*8)/camera.zoom,color);
    }
}
void PlayerController::drawScreen(const Camera2D &camera) const {
    if(!dragging())return;
    const auto area=bounds(GetWorldToScreen2D(startWorld_,camera),GetWorldToScreen2D(endWorld_,camera));
    DrawRectangleRec(area,{85,230,140,35});DrawRectangleLinesEx(area,1,{85,230,140,255});
}
