#include "CatalogEditor.h"
#include "TilesetEditor.h"
#include <algorithm>
#include <exception>

namespace {
void textBox(Rectangle bounds, const std::string& text, bool selected = false) {
    DrawRectangleRec(bounds, Color{35,43,54,255});
    DrawRectangleLinesEx(bounds,1,selected ? SKYBLUE : GRAY);
    BeginScissorMode(static_cast<int>(bounds.x+6),static_cast<int>(bounds.y+3),static_cast<int>(bounds.width-12),static_cast<int>(bounds.height-6));
    DrawText(text.c_str(),static_cast<int>(bounds.x+6),static_cast<int>(bounds.y+9),16,RAYWHITE);
    EndScissorMode();
}
const char* behaviorName(ObjectType type) {
    return type == ObjectType::Door ? "Door" : type == ObjectType::Container ? "Container" : "Gatherable";
}
}
void CatalogEditor::reset() { id_.clear(); name_.clear(); message_.clear(); focus_=-1; offset_=0; behavior_=ObjectType::Container; }
bool CatalogEditor::update(SceneWorld& grid,const EditorInput& input,bool types,int height) {
    const int count = static_cast<int>(types ? grid.objectTypes().size() : grid.itemDefinitions().size());
    const int visible = std::max(1,(height-190)/62);
    if (input.mouse.x>=376) offset_=std::clamp(offset_-static_cast<int>(input.wheel.y),0,std::max(0,count-visible));
    if (input.leftPressed) {
        focus_=-1;
        if (CheckCollisionPointRec(input.mouse,{16,124,328,34})) focus_=0;
        if (CheckCollisionPointRec(input.mouse,{16,194,328,34})) focus_=1;
        if (types) for(int i=0;i<3;++i)
            if(CheckCollisionPointRec(input.mouse,{16.0f+110*i,254,106,32})) behavior_=static_cast<ObjectType>(i);
        if(CheckCollisionPointRec(input.mouse,{16,352,328,34})) { reset(); return false; }
        if(CheckCollisionPointRec(input.mouse,{16,306,328,34})) {
            try {
                if(types) grid.createObjectType({id_,name_,behavior_});
                else grid.createItemDefinition({id_,name_});
                message_="Created. Map save requested."; error_=false;
                return true;
            } catch(const std::exception& error) { message_=error.what(); error_=true; }
        }
    }
    if(focus_>=0) {
        auto& text=focus_==0 ? id_ : name_;
        if(input.backspace && !text.empty()) {
            auto start=text.size()-1;
            while(start>0 && (static_cast<unsigned char>(text[start])&0xc0)==0x80) --start;
            text.resize(start);
        }
        if(text.size()+input.text.size()<=Object::maxTextBytes) text+=input.text;
    }
    return false;
}
void CatalogEditor::draw(const SceneWorld& grid,bool types,int height) const {
    DrawRectangle(0,0,360,height,Color{20,25,33,255});
    DrawText(types ? "OBJECT TYPES" : "ITEM CREATION",16,17,20,RAYWHITE);
    DrawText("Create a reusable definition",16,68,16,SKYBLUE);
    DrawText("Readable ID (GUID is automatic)",16,102,14,LIGHTGRAY); textBox({16,124,328,34},id_,focus_==0);
    DrawText("Display name",16,172,16,LIGHTGRAY); textBox({16,194,328,34},name_,focus_==1);
    if(types) {
        DrawText("Base behavior",16,234,14,LIGHTGRAY);
        for(int i=0;i<3;++i) textBox({16.0f+110*i,254,106,32},behaviorName(static_cast<ObjectType>(i)),behavior_==static_cast<ObjectType>(i));
    } else DrawText("Use saved items in object contents.",16,250,14,LIGHTGRAY);
    textBox({16,306,328,34},"Create + Save map");
    textBox({16,352,328,34},"New / Clear draft");
    std::string rest=message_; int y=406;
    while(!rest.empty() && y<height-100) {
        std::size_t n=rest.size();
        while(n>1 && MeasureText(rest.substr(0,n).c_str(),14)>324) --n;
        DrawText(rest.substr(0,n).c_str(),16,y,14,error_ ? Color{255,135,120,255} : SKYBLUE);
        rest.erase(0,n); y+=20;
    }
    DrawText("Definitions save with Maps/world.jevmap",16,height-86,14,LIGHTGRAY);
    DrawText("Ctrl+S Save  |  Ctrl+O Load",16,height-62,14,LIGHTGRAY);
    DrawText("F1 closes editor",16,height-38,14,LIGHTGRAY);
    const int width=std::max(200,GetScreenWidth()-392);
    DrawRectangle(376,88,width,height-142,Color{20,25,33,250});
    DrawText(types ? "OBJECT TYPE CATALOG" : "ITEM CATALOG",392,94,20,RAYWHITE);
    DrawText("Scroll here to browse",392,118,14,GRAY);
    BeginScissorMode(392,142,width-24,std::max(1,height-190));
    int index=0;
    const auto row=[&](const std::string& id,const std::string& name,const std::string& detail,Guid guid) {
        const int top=144+(index++-offset_)*62;
        if(top<82 || top>height-60) return;
        DrawText(name.c_str(),392,top,18,SKYBLUE);
        DrawText((id+detail).c_str(),392,top+23,14,LIGHTGRAY);
        DrawText(guid.toString().c_str(),392,top+42,12,GRAY);
    };
    if(types) for(const auto& [id,type]:grid.objectTypes()) row(id,type.displayName,std::string(" | ")+behaviorName(type.behavior),type.guid);
    else for(const auto& [id,item]:grid.itemDefinitions()) row(id,item.displayName,"",item.guid);
    if(index==0) DrawText("No definitions yet.",392,140,16,LIGHTGRAY);
    EndScissorMode();
}
