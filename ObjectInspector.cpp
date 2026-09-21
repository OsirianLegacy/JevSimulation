#include "ObjectInspector.h"
#include "TilesetEditor.h"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace {
bool hit(Vector2 p, Rectangle r) { return CheckCollisionPointRec(p, r); }
Rectangle view(int height) { return {16, 214, 328, static_cast<float>(std::max(32, height - 390))}; }
void box(Rectangle r, const std::string& text, bool active = false) {
    DrawRectangleRec(r, Color{35,43,54,255});
    DrawRectangleLinesEx(r, 1, active ? SKYBLUE : GRAY);
    // Local clipping is supplied by the inspector's enclosing scissor.
    std::string shown = text;
    while (!shown.empty() && MeasureText(shown.c_str(), 14) > r.width - 10) shown.pop_back();
    DrawText(shown.c_str(), static_cast<int>(r.x + 5), static_cast<int>(r.y + 7), 14, RAYWHITE);
}
std::vector<std::string> fieldLines(const SceneWorld& world,Guid id){
    std::vector<std::string> lines;
    for(auto text:world.componentNames(id))while(!text.empty()){
        std::size_t n=text.size();while(n>1 && MeasureText(text.substr(0,n).c_str(),12)>324)--n;
        lines.push_back(text.substr(0,n));text.erase(0,n);
    }
    return lines;
}
const char* typeName(ObjectType type) {
    switch(type) { case ObjectType::Door: return "Door"; case ObjectType::Container: return "Container";
        case ObjectType::Gatherable: return "Gatherable"; }
    return "Unknown";
}
}
void ObjectInspector::clear() { selected_ = {}; detail_={}; detailsMode_=false; rows_.clear(); focus_ = -1; scroll_ = 0; error_.clear(); itemChoice_=0; }
void ObjectInspector::resetDraft(const Object& object) {
    detail_={};
    canOpen_=grid_ && grid_->editableField(object.id(),"open");
    canContain_=grid_ && grid_->editableField(object.id(),"contents");
    selected_ = object.id(); type_ = object.type(); open_ = object.isOpen();
    const auto harvest=grid_?grid_->harvestable(selected_):std::nullopt;
    harvestId_=harvest?harvest->definitionId:"";
    rows_.clear(); focus_ = -1; error_.clear();
    for (const auto& item : object.contents()) rows_.push_back({item.definitionId, item.displayName, std::to_string(item.quantity),item.guid});
}
void ObjectInspector::select(const SceneWorld& grid, CellPosition cell) {
    grid_ = &grid;
    clear(); if (const auto object = grid.objectAt(cell)) resetDraft(*object);
}
void ObjectInspector::refresh(const SceneWorld& world){grid_=&world;if(const auto object=world.findObject(selected_))resetDraft(*object);else clear();}
void ObjectInspector::clampScroll(int height) {
    if(detailsMode_ && grid_){scroll_=std::clamp(scroll_,0.0f,std::max(0.0f,static_cast<float>(fieldLines(*grid_,detail_.empty()?selected_:detail_).size()*20+60)-view(height).height));return;}
    const float content = 134 + static_cast<float>(rows_.size()) * 122 + (canContain_ ? 36 : 0) + (type_==ObjectType::Gatherable?76:0);
    scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, content - view(height).height));
}
void ObjectInspector::update(SceneWorld& grid, const EditorInput& input, int height, bool readOnly) {
    readOnly_=readOnly;
    const auto definitions=grid.itemDefinitions();
    grid_ = &grid;
    if(itemChoice_>grid.itemDefinitions().size()) itemChoice_=0;
    const auto object = grid.findObject(selected_);
    if (!object) { clear(); return; }
    clampScroll(height);
    const auto area = view(height);
    if (hit(input.mouse, area)) { scroll_ -= input.wheel.y * 36; clampScroll(height); }
    if(input.leftPressed && hit(input.mouse,{252,214,92,26})){detailsMode_=!detailsMode_;scroll_=0;focus_=-1;return;}
    if(detailsMode_){focus_=-1;return;}
    if(input.leftPressed && hit(input.mouse,area)) {
        const int row=static_cast<int>((input.mouse.y-area.y+scroll_-134)/122);
        if(input.mouse.y-area.y+scroll_>=134 && row>=0 && row<static_cast<int>(rows_.size()) && input.mouse.x<250 && static_cast<int>(input.mouse.y-area.y+scroll_-134)%122<26){detail_=rows_[row].guid;detailsMode_=true;scroll_=0;focus_=-1;return;}
        else detail_={};
    }
    if(readOnly){focus_=-1;return;}
    if (input.leftPressed) {
        focus_ = -1;
        if (hit(input.mouse, {16, static_cast<float>(height - 168), 158, 28})) {
            try {
                std::vector<Item> items;
                for (const auto& row : rows_) {
                    std::uint32_t quantity = 0;
                    const auto result = std::from_chars(row.quantity.data(), row.quantity.data() + row.quantity.size(), quantity);
                    if (result.ec != std::errc{} || result.ptr != row.quantity.data() + row.quantity.size() || !quantity)
                        throw std::invalid_argument("Quantity must be 1..4294967295.");
                    items.push_back({row.definition, row.name, quantity,row.guid});
                }
                grid.beginEdit();
                try {
                    grid.updateObject(selected_, open_, std::move(items));
                    const auto h=grid.harvestable(selected_);
                    if(harvestId_.empty())grid.removeHarvestable(selected_);
                    else if(!h || h->definitionId!=harvestId_)grid.setHarvestable(selected_,harvestId_);
                    grid.endEdit();
                } catch (...) {grid.cancelEdit();throw;}
                resetDraft(*grid.findObject(selected_));
            } catch (const std::exception& error) { error_ = error.what(); }
            return;
        }
        if (hit(input.mouse, {184, static_cast<float>(height - 168), 160, 28})) { resetDraft(*object); return; }
        if (hit(input.mouse, area)) {
            const float top = area.y - scroll_;
            if(type_==ObjectType::Gatherable && hit(input.mouse,{16,top+170+rows_.size()*122,328,28})) {
                const auto &defs=grid.creatureCatalog().harvestables;
                auto it=defs.find(harvestId_);
                if(harvestId_.empty())it=defs.begin();else if(it!=defs.end())++it;
                harvestId_=it==defs.end()?"":it->first;
            }
            if (canOpen_ && hit(input.mouse, {16, top + 78, 328, 28})) open_ = !open_;
            if(canContain_ && !grid.itemDefinitions().empty() && hit(input.mouse,{16,top+110,328,24}))
                itemChoice_=(itemChoice_+1)%(grid.itemDefinitions().size()+1);
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                const float y = top + 134 + i * 122;
                if (hit(input.mouse, {260, y, 84, 26})) {
                    rows_.erase(rows_.begin() + i); clampScroll(height); return;
                }
                for (int field = 0; field < 3; ++field) {
                    const Rectangle r = field == 0 ? Rectangle{16,y+28,328,26} :
                        field == 1 ? Rectangle{16,y+58,328,26} : Rectangle{100,y+88,244,26};
                    if (hit(input.mouse, r)) focus_ = static_cast<int>(i) * 3 + field;
                }
            }
            if (canContain_ && hit(input.mouse, {16, top + 134 + rows_.size()*122, 328, 28})) {
                if (rows_.size() < Object::maxStacks) {
                    if(itemChoice_==0) rows_.push_back({"", "", "1"});
                    else {
                        const auto& item=std::next(definitions.begin(),itemChoice_-1)->second;
                        rows_.push_back({item.id,item.displayName,"1"});
                    }
                    error_.clear();
                }
                else error_ = "Maximum 1024 stacks reached.";
            }
        }
    }
    if (focus_ >= 0) {
        auto& row = rows_.at(focus_ / 3);
        auto& text = focus_ % 3 == 0 ? row.definition : focus_ % 3 == 1 ? row.name : row.quantity;
        if (input.backspace && !text.empty()) {
            auto start = text.size() - 1;
            while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80) --start;
            text.resize(start);
        }
        if (text.size() + input.text.size() <= Object::maxTextBytes) text += input.text;
    }
}
void ObjectInspector::draw(int height) const {
    const auto area = view(height);
    if (selected_.empty()) { DrawText("Click a world object to inspect.", 16, 222, 16, LIGHTGRAY); return; }
    if(detailsMode_ && grid_){
        DrawText(detail_.empty()?"OBJECT COMPONENTS":"ITEM COMPONENTS",16,218,16,SKYBLUE);
        box({252,214,92,26},"Properties");
        BeginScissorMode(16,244,328,static_cast<int>(area.height)-30);
        int y=static_cast<int>(250-scroll_);
        DrawText((detail_.empty()?selected_:detail_).toString().c_str(),16,y,12,GRAY);y+=30;
        for(const auto& line:fieldLines(*grid_,detail_.empty()?selected_:detail_)){
            DrawText(line.c_str(),16,y,12,LIGHTGRAY);y+=20;
        }
        EndScissorMode();
        DrawText("Wheel: scroll component fields",16,height-156,14,GRAY);
        return;
    }
    BeginScissorMode(16, 214, 328, static_cast<int>(area.height));
    const float top = area.y - scroll_;
    std::string title=typeName(type_);
    if(grid_) if(const auto object=grid_->findObject(selected_);object && !object->typeDefinitionId().empty())
        title=grid_->objectTypes().at(object->typeDefinitionId()).displayName+" ("+title+")";
    while(!title.empty() && MeasureText(title.c_str(),18)>230)title.pop_back();
    DrawText(title.c_str(), 16, static_cast<int>(top), 18, SKYBLUE);
    DrawText(selected_.toString().c_str(), 16, static_cast<int>(top+27), 12, LIGHTGRAY);
    DrawText(type_ == ObjectType::Container ? "Container: yes" : "Container: no", 16, static_cast<int>(top+52), 14, LIGHTGRAY);
    if (canOpen_) box({16,top+78,328,28}, open_ ? "Open: yes (click to toggle)" : "Open: no (click to toggle)");
    if(canContain_ && grid_ && !grid_->itemDefinitions().empty()) {
        std::string choice="New stack: blank";
        if(itemChoice_>0 && itemChoice_<=grid_->itemDefinitions().size()) choice="New stack: "+std::next(grid_->itemDefinitions().begin(),itemChoice_-1)->second.displayName;
        box({16,top+110,328,24},choice+" >");
    } else DrawText(type_ == ObjectType::Door ? "Doors cannot contain items." : "CONTENTS: ID / Name / Quantity", 16, static_cast<int>(top+114), 14, LIGHTGRAY);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto& row = rows_[i]; const float y = top + 134 + i * 122;
        if (y > area.y + area.height || y + 114 < area.y) continue;
        DrawText(TextFormat("Stack %d", static_cast<int>(i+1)), 16, static_cast<int>(y+1), 12, LIGHTGRAY);
        DrawText(row.guid.empty() ? "GUID assigned on Apply" : row.guid.toString().c_str(),16,static_cast<int>(y+15),10,GRAY);
        box({260,y,84,26}, "Remove");
        box({16,y+28,328,26}, row.definition.empty() ? "Item definition ID" : row.definition, focus_ == static_cast<int>(i)*3);
        box({16,y+58,328,26}, row.name.empty() ? "Display name" : row.name, focus_ == static_cast<int>(i)*3+1);
        DrawText("Quantity", 16, static_cast<int>(y+94), 14, LIGHTGRAY);
        box({100,y+88,244,26}, row.quantity, focus_ == static_cast<int>(i)*3+2);
    }
    if (canContain_) box({16,top+134+rows_.size()*122,328,28}, "+ Add item stack");
    if(type_==ObjectType::Gatherable) {
        box({16,top+170+rows_.size()*122,328,28},"Harvest: "+(harvestId_.empty()?"None":harvestId_)+" >");
        DrawText("Apply to assign. Yields are separate from contents.",16,int(top+204+rows_.size()*122),12,GRAY);
    }
    EndScissorMode();
    const float content = 134 + rows_.size()*122 + (canContain_ ? 36 : 0) + (type_==ObjectType::Gatherable?76:0);
    if (content > area.height) {
        const float thumb = std::max(16.0f, area.height * area.height / content);
        DrawRectangle(347, 214, 5, static_cast<int>(area.height), Color{35,43,54,255});
        DrawRectangle(347, static_cast<int>(214 + scroll_/(content-area.height)*(area.height-thumb)),
                      5, static_cast<int>(thumb), SKYBLUE);
    }
    box({16,static_cast<float>(height-168),158,28}, readOnly_?"Pause to edit":"Apply");
    box({184,static_cast<float>(height-168),160,28}, "Cancel");
    box({252,214,92,26},"Fields");
    if (!error_.empty()) {
        DrawRectangle(12, height-98, 338, 86, Color{65,22,22,255});
        // Wrap messages without extending beyond the panel.
        std::string remaining = error_; int y = height-94;
        while (!remaining.empty() && y < height-14) {
            std::size_t length = remaining.size();
            while (length > 1 && MeasureText(remaining.substr(0,length).c_str(),14) > 322) --length;
            DrawText(remaining.substr(0,length).c_str(), 18, y, 14, RAYWHITE);
            remaining.erase(0,length); y += 18;
        }
    }
}
