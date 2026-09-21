#include "CreatureEditor.h"
#include "TilesetEditor.h"
#include <algorithm>
#include <set>

namespace {
void box(Rectangle b,const std::string& text,bool selected=false) {
    DrawRectangleRec(b,{35,43,54,255});DrawRectangleLinesEx(b,1,selected?SKYBLUE:GRAY);
    BeginScissorMode(int(b.x+4),int(b.y+2),int(b.width-8),int(b.height-4));
    DrawText(text.c_str(),int(b.x+6),int(b.y+7),14,RAYWHITE);EndScissorMode();
}
template<class Map> std::uint32_t nextId(const Map& map) {
    if(map.empty())return 1;
    if(map.rbegin()->first==UINT32_MAX)throw std::length_error("Enumeration IDs exhausted.");
    return map.rbegin()->first+1;
}
template<class Map> void cycle(const Map& map,std::uint32_t& id,int step) {
    if(map.empty()){id=0;return;}auto it=map.find(id);if(it==map.end())it=map.begin();
    if(step>0){if(++it==map.end())it=map.begin();}else {if(it==map.begin())it=map.end();--it;}id=it->first;
}
}
CreatureEditor::~CreatureEditor(){if(IsTextureValid(checks_))UnloadTexture(checks_);if(IsTextureValid(sheet_))UnloadTexture(sheet_);}
void CreatureEditor::reset(){loaded_=false;focus_=-1;message_.clear();}
void CreatureEditor::select(){
    focus_=-1;listOffset_=0;scroll_={};
    name_=vocations_?(draft_.vocations.contains(vocation_)?draft_.vocations.at(vocation_).name:""):draft_.species.at(species_).name;
    loadSheet();
}
void CreatureEditor::loadSheet(){
    const auto name=draft_.species.at(species_).sheet;if(name==sheetName_)return;
    if(IsTextureValid(sheet_))UnloadTexture(sheet_);
    sheet_=LoadTexture((assets_/"SpriteSheets"/name).string().c_str());sheetName_=name;scroll_={};
    if(IsTextureValid(sheet_))SetTextureFilter(sheet_,TEXTURE_FILTER_POINT);
}
scene::SpriteFrames* CreatureEditor::frames(){
    if(!vocations_)return &draft_.species.at(species_).idle;
    if(!draft_.vocations.contains(vocation_) || !draft_.vocations.at(vocation_).species.contains(species_))return nullptr;
    return &draft_.vocations.at(vocation_).species.at(species_);
}
const scene::SpriteFrames* CreatureEditor::frames()const{return const_cast<CreatureEditor*>(this)->frames();}
std::vector<std::string> CreatureEditor::resourceKeys()const{
    std::set<std::string> keys{"health","thirst","hunger","stamina","mana","safety","society","reproduction"};
    for(const auto& [k,v]:draft_.species.at(species_).resources)keys.insert(k);
    return {keys.begin(),keys.end()};
}
bool CreatureEditor::update(SceneWorld& world,const EditorInput& input,int height){
    if(!loaded_){draft_=world.creatureCatalog();if(!draft_.species.contains(species_))species_=0;if(!draft_.vocations.contains(vocation_))vocation_=draft_.vocations.empty()?0:draft_.vocations.begin()->first;loaded_=true;select();
        if(!IsTextureValid(checks_)){checks_=LoadTexture((assets_/"UI/Inputs.png").string().c_str());if(IsTextureValid(checks_))SetTextureFilter(checks_,TEXTURE_FILTER_POINT);}}
    auto hit=[&](Rectangle b){return input.leftPressed && CheckCollisionPointRec(input.mouse,b);};
    try {
        if(input.leftPressed)focus_=-1;
        if(hit({16,88,158,28})){vocations_=false;select();}
        if(hit({184,88,160,28})){vocations_=true;select();}
        if(hit({16,128,68,28}) || hit({92,128,68,28})) {
            const int step=input.mouse.x<90?-1:1;if(vocations_)cycle(draft_.vocations,vocation_,step);else cycle(draft_.species,species_,step);select();
        }
        if(hit({176,128,168,28})) {
            if(vocations_){vocation_=nextId(draft_.vocations);draft_.vocations.emplace(vocation_,scene::VocationDefinition{"Vocation "+std::to_string(vocation_),{}});}
            else {species_=nextId(draft_.species);scene::SpeciesDefinition d;d.name="Species "+std::to_string(species_);draft_.species.emplace(species_,d);}select();
        }
        if(hit({16,186,328,30}))focus_=0;
        if(!vocations_){
            if(hit({16,486,328,28})) {
                auto &state=draft_.species.at(species_).defaultDisposition;
                state=state==scene::Disposition::Neutral?scene::Disposition::Friendly:
                    state==scene::Disposition::Friendly?scene::Disposition::Hostile:scene::Disposition::Neutral;
            }
            if(hit({16,228,328,28})) {auto& sheet=draft_.species.at(species_).sheet;sheet=sheet=="Units.png"?"Animals.png":sheet=="Animals.png"?"Monsters.png":"Units.png";loadSheet();}
            if(hit({16,310,230,30}))focus_=1;
            if(hit({254,310,90,30}) && !subName_.empty()){auto& subs=draft_.species.at(species_).subSpecies;subs.emplace(nextId(subs),subName_);subName_.clear();}
            if(hit({16,382,210,30}))focus_=2;
            if(hit({234,382,110,30}))focus_=3;
            if(hit({16,420,328,28})) {std::size_t used=0;const float value=std::stof(capacity_,&used);if(used!=capacity_.size())throw std::invalid_argument("Invalid capacity.");scene::Resource test;test.set(resource_,scene::ResourcePool(value));draft_.species.at(species_).resources[resource_]=value;}
        }
        if(focus_>=0){auto& text=focus_==0?name_:focus_==1?subName_:focus_==2?resource_:capacity_;if(input.backspace && !text.empty())text.pop_back();if(text.size()+input.text.size()<=64)text+=input.text;
            if(focus_==0){if(vocations_ && draft_.vocations.contains(vocation_))draft_.vocations.at(vocation_).name=name_;else if(!vocations_)draft_.species.at(species_).name=name_;}}
        if(CheckCollisionPointRec(input.mouse,{384,124,float(GetScreenWidth()-400),140})) {
            const int count=vocations_?int(draft_.species.size()):int(resourceKeys().size());listOffset_=std::clamp(listOffset_-int(input.wheel.y),0,std::max(0,count-5));
            if(input.leftPressed){int row=int((input.mouse.y-124)/28)+listOffset_;
                if(vocations_ && draft_.vocations.contains(vocation_) && row<int(draft_.species.size())){
                    auto it=draft_.species.begin();std::advance(it,row);species_=it->first;loadSheet();
                    if(input.mouse.x<420){auto& allowed=draft_.vocations.at(vocation_).species;if(allowed.contains(species_))allowed.erase(species_);else allowed[species_]=draft_.species.at(species_).idle;}
                }else if(!vocations_ && row<int(resourceKeys().size())){const auto key=resourceKeys()[row];auto& r=draft_.species.at(species_).resources;
                    if(input.mouse.x<420 && key!="health"){if(r.contains(key))r.erase(key);else r[key]=100;}else {resource_=key;capacity_=std::to_string(r.contains(key)?r.at(key):100);}}
            }
        }
        if(hit({392,276,110,28}))player_=false;if(hit({510,276,110,28}))player_=true;
        if(auto f=frames()){
            if(hit({628,276,30,28}))f->count=std::max(1,f->count-1);if(hit({700,276,30,28}))f->count=std::min(64,f->count+1);
            if(hit({740,276,30,28}))f->fps=std::max(1.0f,f->fps-1);if(hit({814,276,30,28}))f->fps=std::min(60.0f,f->fps+1);
            const Rectangle area{392,356,float(GetScreenWidth()-408),float(height-406)};
            if(CheckCollisionPointRec(input.mouse,area)){
                if(input.shift)scroll_.x-=input.wheel.y*24;else scroll_.y-=input.wheel.y*24;
                scroll_.x=std::clamp(scroll_.x,0.0f,std::max(0.0f,sheet_.width*3-area.width));scroll_.y=std::clamp(scroll_.y,0.0f,std::max(0.0f,sheet_.height*3-area.height));
                if(input.leftPressed){const int col=int((input.mouse.x-area.x+scroll_.x)/24),row=int((input.mouse.y-area.y+scroll_.y)/24);
                    if(col*8<sheet_.width && row*8<sheet_.height){f->column=col;if(player_)f->playerRow=row;else f->aiRow=row;}}
            }
        }
        if(hit({16,float(height-80),328,30})) {
            draft_.validate();
            for(const auto& [id,s]:draft_.species){auto image=LoadImage((assets_/"SpriteSheets"/s.sheet).string().c_str());
                const auto valid=[&](const scene::SpriteFrames& f){return (f.column+f.count)*8<=image.width && (std::max(f.aiRow,f.playerRow)+1)*8<=image.height;};
                bool good=IsImageValid(image) && valid(s.idle);for(const auto& [vid,v]:draft_.vocations)if(v.species.contains(id))good=good && valid(v.species.at(id));
                if(IsImageValid(image))UnloadImage(image);if(!good)throw std::invalid_argument("Selected frames extend beyond the sprite sheet.");}
            world.setCreatureCatalog(draft_);message_="Applied. Map save requested.";return true;
        }
        if(hit({16,float(height-40),328,28})){reset();return false;}
    }catch(const std::exception& e){message_=e.what();}
    return false;
}
void CreatureEditor::draw(int height)const{
    if(!loaded_)return;
    DrawRectangle(0,0,GetScreenWidth(),height,{20,25,33,255});
    DrawText("CREATURE DEFINITIONS",16,18,20,RAYWHITE);box({16,88,158,28},"Species",!vocations_);box({184,88,160,28},"Vocations",vocations_);
    box({16,128,68,28},"Prev");box({92,128,68,28},"Next");box({176,128,168,28},"New definition");
    DrawText(TextFormat("%s ID %u",vocations_?"Vocation":"Species",vocations_?vocation_:species_),16,166,14,GRAY);box({16,186,328,30},name_,focus_==0);
    const auto& s=draft_.species.at(species_);
    if(!vocations_){box({16,228,328,28},s.sheet+" (click to change)");
        DrawText(TextFormat("Subspecies (%d): Default + your entries",int(s.subSpecies.size())),16,268,14,LIGHTGRAY);
        std::string names;for(const auto& [id,n]:s.subSpecies){if(!names.empty())names+=", ";names+=n;}box({16,282,328,24},names);
        box({16,310,230,30},subName_,focus_==1);box({254,310,90,30},"Add sub");
        DrawText("Resource key / capacity",16,360,14,LIGHTGRAY);box({16,382,210,30},resource_,focus_==2);box({234,382,110,30},capacity_,focus_==3);box({16,420,328,28},"Add / update resource");
        DrawText("Health is required. Other pools are optional.",16,460,13,GRAY);
        box({16,486,328,28},std::string("Default: ")+scene::dispositionName(s.defaultDisposition)+" (click to change)");
    }else {DrawText("Check species to allow this vocation.",16,236,14,LIGHTGRAY);DrawText("Click its name to edit its sprite mapping.",16,260,14,LIGHTGRAY);DrawText("Unassigned is available to all species.",16,304,14,GRAY);}
    box({16,float(height-80),328,30},"Apply + Save map");box({16,float(height-40),328,28},"Discard / Reload saved definitions");
    DrawText(vocations_?"Allowed species (scroll list)":"Species resources (scroll list)",392,96,18,SKYBLUE);
    auto checkbox=[&](int y,bool checked){if(IsTextureValid(checks_))DrawTexturePro(checks_,{checked?8.0f:0.0f,264,8,8},{392,float(y),24,24},{},0,WHITE);};
    if(vocations_){int row=0;for(const auto& [id,d]:draft_.species){const int index=row++-listOffset_;if(index<0 || index>=5)continue;int y=124+index*28;
        checkbox(y,draft_.vocations.contains(vocation_) && draft_.vocations.at(vocation_).species.contains(id));DrawText(d.name.c_str(),424,y+4,16,id==species_?SKYBLUE:RAYWHITE);}}
    else {const auto keys=resourceKeys();for(int row=0;row<5 && row+listOffset_<int(keys.size());++row){auto key=keys[row+listOffset_];int y=124+row*28;checkbox(y,s.resources.contains(key));DrawText((key+(s.resources.contains(key)?"  "+std::to_string(s.resources.at(key)):"")).c_str(),424,y+4,16,RAYWHITE);}}
    box({392,276,110,28},"AI row",!player_);box({510,276,110,28},"Player row",player_);
    if(const auto f=frames()){
        if(IsTextureValid(sheet_)) {
            const int frame=static_cast<int>(std::fmod(GetTime()*f->fps,f->count));
            for(int player=0;player<2;++player){const int x=GetScreenWidth()-144+player*66;
                DrawRectangle(x,176,48,48,{42,43,50,255});DrawText(player?"Player":"AI",x,158,13,GRAY);
                const Rectangle source{float((f->column+frame)*8),float((player?f->playerRow:f->aiRow)*8),8,8};
                if(source.x+8<=sheet_.width && source.y+8<=sheet_.height)DrawTexturePro(sheet_,source,{float(x),176,48,48},{},0,WHITE);
            }
        }
        box({628,276,30,28},"-");box({700,276,30,28},"+");DrawText(TextFormat("%d",f->count),668,283,16,RAYWHITE);
        box({740,276,30,28},"-");box({814,276,30,28},"+");DrawText(TextFormat("%.0f",f->fps),780,283,16,RAYWHITE);
        DrawText(TextFormat("Frames / FPS | %s | start %d, AI %d, Player %d",s.name.c_str(),f->column,f->aiRow,f->playerRow),392,314,14,SKYBLUE);
        DrawText("Click first 8x8 frame. Wheel: vertical | Shift+wheel: horizontal",392,334,13,GRAY);
        const Rectangle area{392,356,float(GetScreenWidth()-408),float(height-406)};DrawRectangleRec(area,{42,43,50,255});
        BeginScissorMode(int(area.x),int(area.y),int(area.width),int(area.height));
        if(IsTextureValid(sheet_))DrawTextureEx(sheet_,{area.x-scroll_.x,area.y-scroll_.y},0,3,WHITE);
        DrawRectangleLinesEx({area.x+f->column*24-scroll_.x,area.y+(player_?f->playerRow:f->aiRow)*24-scroll_.y,float(f->count*24),24},2,SKYBLUE);EndScissorMode();
    }else DrawText("Create a vocation and check a species to choose frames.",392,322,14,GRAY);
    BeginScissorMode(392,height-38,GetScreenWidth()-408,30);DrawText(message_.c_str(),392,height-32,14,ORANGE);EndScissorMode();
}
