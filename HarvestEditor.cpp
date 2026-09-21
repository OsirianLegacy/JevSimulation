#include "HarvestEditor.h"
#include "TilesetEditor.h"
#include <charconv>
namespace {
const char *actions[]{"chop","mine","break","butcher"};
const char *labels[]{"Definition ID","Display name","Work seconds","Harvest units","Required tool ID (blank = none)","Depleted state name","Regeneration seconds (0 = none)"};
void box(Rectangle r,const std::string &text,bool focus=false) {
    DrawRectangleRec(r,{35,43,54,255});DrawRectangleLinesEx(r,1,focus?SKYBLUE:GRAY);
    BeginScissorMode(int(r.x+4),int(r.y+2),int(r.width-8),int(r.height-4));
    DrawText(text.c_str(),int(r.x+5),int(r.y+7),14,RAYWHITE);EndScissorMode();
}
std::uint32_t integer(const std::string &s) {
    std::uint32_t value=0;const auto r=std::from_chars(s.data(),s.data()+s.size(),value);
    if(r.ec!=std::errc{} || r.ptr!=s.data()+s.size())throw std::invalid_argument("Expected a whole positive number.");return value;
}
double number(const std::string &s) {std::size_t used=0;const double n=std::stod(s,&used);if(used!=s.size())throw std::invalid_argument("Invalid number.");return n;}
}
void HarvestEditor::select(const SceneWorld &w,const std::string &key) {
    selected_=key;focus_=-1;offset_=0;loaded_=true;message_.clear();
    const auto &d=w.creatureCatalog().harvestables.at(key);
    fields_={key,d.name,std::to_string(d.workSeconds),std::to_string(d.units),d.requiredTool,d.depletedState,std::to_string(d.regenerationSeconds)};
    action_=0;for(int i=0;i<4;++i)if(d.action==actions[i])action_=i;
    remove_=d.removeWhenDepleted;yields_.clear();for(const auto &i:d.yields)yields_.push_back({i.definitionId,i.displayName,std::to_string(i.quantity)});
    depletedTile_=d.depletedTile;
}
bool HarvestEditor::update(SceneWorld &w,const EditorInput &in,int height,TileRef brush) {
    const auto &catalog=w.creatureCatalog();
    if(!loaded_) {
        if(!catalog.harvestables.empty())select(w,catalog.harvestables.contains("tree")?"tree":catalog.harvestables.begin()->first);
        else {loaded_=true;fields_={"new_source","New source","2","5","","Depleted","0"};yields_={{{"wood","Wood","1"}}};}
    }
    if(!catalog.species.contains(species_))species_=0;
    auto hit=[&](Rectangle r){return in.leftPressed && CheckCollisionPointRec(in.mouse,r);};
    const int visible=std::max(1,(height-380)/94);
    if(in.mouse.x>=384)offset_=std::clamp(offset_-int(in.wheel.y),0,std::max(0,int(yields_.size())-visible));
    try {
        if(in.leftPressed)focus_=-1;
        for(int i=0;i<4;++i)if(hit({16.0f+i*83,88,78,28}))action_=i;
        for(int i=0;i<7;++i)if(hit({16,140.0f+i*44,328,28}))focus_=i;
        if(hit({16,452,328,28}))remove_=!remove_;
        if(hit({16,488,242,24}))depletedTile_=brush;
        if(hit({266,488,78,24}))depletedTile_={};
        if(hit({392,88,80,28}) || hit({480,88,80,28})) {
            auto it=catalog.harvestables.find(selected_);
            if(!catalog.harvestables.empty()) {
                if(it==catalog.harvestables.end())it=catalog.harvestables.begin();
                else if(in.mouse.x<480){if(it==catalog.harvestables.begin())it=catalog.harvestables.end();--it;}
                else if(++it==catalog.harvestables.end())it=catalog.harvestables.begin();
                select(w,it->first);
            }
        }
        if(hit({568,88,160,28})){fields_[0]="new_source";fields_[1]="New source";selected_.clear();message_.clear();}
        for(int row=0;row<visible && row+offset_<int(yields_.size());++row) {
            const int i=row+offset_;const float y=154.0f+row*94;
            if(hit({392,y,184,28}))focus_=7+i*3;
            if(hit({584,y,210,28}))focus_=8+i*3;
            if(hit({392,y+34,184,28}))focus_=9+i*3;
            if(hit({584,y+34,100,28})){yields_.erase(yields_.begin()+i);focus_=-1;break;}
        }
        if(hit({392,float(height-222),200,28}) && yields_.size()<16)yields_.push_back({"item","Item","1"});
        if(hit({392,float(height-184),320,28})) {
            auto it=catalog.species.find(species_);if(++it==catalog.species.end())it=catalog.species.begin();species_=it->first;
        }
        if(hit({392,float(height-146),250,28}) || hit({650,float(height-146),140,28})) {
            auto next=catalog;auto &s=next.species.at(species_);
            if(in.mouse.x>=650)s.carcassHarvest.clear();
            else {if(!catalog.harvestables.contains(fields_[0]))throw std::invalid_argument("Save the harvest definition first.");
                s.carcassHarvest=fields_[0];s.carcassTile=brush;}
            w.setCreatureCatalog(next);message_="Species carcass settings saved using the current tile brush.";return true;
        }
        if(focus_>=0) {
            auto &text=focus_<7?fields_[focus_]:yields_.at((focus_-7)/3).at((focus_-7)%3);
            if(in.backspace && !text.empty()){auto n=text.size()-1;while(n>0 && (static_cast<unsigned char>(text[n])&0xc0)==0x80)--n;text.resize(n);}
            if(text.size()+in.text.size()<=255)text+=in.text;
        }
        if(hit({16,float(height-80),328,28})) {
            scene::HarvestableDefinition d;d.name=fields_[1];d.action=actions[action_];d.workSeconds=float(number(fields_[2]));d.units=integer(fields_[3]);
            d.requiredTool=fields_[4];d.depletedState=fields_[5];d.regenerationSeconds=number(fields_[6]);d.removeWhenDepleted=remove_;
            d.depletedTile=depletedTile_;
            for(const auto &i:yields_)d.yields.push_back({i[0],i[1],integer(i[2])});
            d.validate();auto next=catalog;next.harvestables[fields_[0]]=d;w.setCreatureCatalog(next);
            selected_=fields_[0];message_="Saved. Tile Painting > Objects > Paint > Harvest to place.";return true;
        }
        if(hit({16,float(height-40),328,28}))reset();
    }catch(const std::exception &e){message_=e.what();}
    return false;
}
void HarvestEditor::draw(const SceneWorld &w,int height) const {
    DrawRectangle(0,0,GetScreenWidth(),height,{20,25,33,255});DrawText("HARVEST DEFINITIONS",16,18,20,RAYWHITE);
    for(int i=0;i<4;++i)box({16.0f+i*83,88,78,28},actions[i],action_==i);
    for(int i=0;i<7;++i){DrawText(labels[i],16,122+i*44,13,LIGHTGRAY);box({16,140.0f+i*44,328,28},fields_[i],focus_==i);}
    box({16,452,328,28},remove_?"On depletion: remove if contents empty":"On depletion: keep as depleted node");
    box({16,488,242,24},depletedTile_.present()?"Depleted tile set (use brush)":"Set depleted tile from brush");box({266,488,78,24},"Clear");
    box({16,float(height-80),328,28},"Apply + Save map");box({16,float(height-40),328,28},"Discard / Reload");
    box({392,88,80,28},"Prev");box({480,88,80,28},"Next");box({568,88,160,28},"Copy to new");
    box({736,88,170,28},"Place in level");
    DrawText("DROPS / 25% health: ID / name / max quantity",392,126,16,SKYBLUE);
    const int visible=std::max(1,(height-380)/94);
    for(int row=0;row<visible && row+offset_<int(yields_.size());++row) {
        const int i=row+offset_;const float y=154.0f+row*94;
        box({392,y,184,28},yields_[i][0],focus_==7+i*3);box({584,y,210,28},yields_[i][1],focus_==8+i*3);
        box({392,y+34,184,28},yields_[i][2],focus_==9+i*3);box({584,y+34,100,28},"Remove");
    }
    box({392,float(height-222),200,28},"Add yield (scroll list)");
    if(w.creatureCatalog().species.contains(species_)) {
        const auto &s=w.creatureCatalog().species.at(species_);
        box({392,float(height-184),320,28},"Carcass species: "+s.name+" >");
        box({392,float(height-146),250,28},"Assign + use current tile brush");box({650,float(height-146),140,28},"Disable carcass");
        DrawText(("Current: "+(s.carcassHarvest.empty()?"disabled":s.carcassHarvest)).c_str(),392,height-108,14,GRAY);
    }
    BeginScissorMode(392,height-76,std::max(1,GetScreenWidth()-404),68);
    DrawText(message_.c_str(),392,height-70,14,ORANGE);EndScissorMode();
}
