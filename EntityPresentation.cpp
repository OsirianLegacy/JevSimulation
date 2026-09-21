#include "EntityPresentation.h"
#include <algorithm>
#include <cmath>

EntityPresentation::EntityPresentation(const std::filesystem::path& assets) {
    units_=LoadTexture((assets/"SpriteSheets/Units.png").string().c_str());
    animals_=LoadTexture((assets/"SpriteSheets/Animals.png").string().c_str());
    monsters_=LoadTexture((assets/"SpriteSheets/Monsters.png").string().c_str());
    interface_=LoadTexture((assets/"UI/Interface.png").string().c_str());
    bars_=LoadTexture((assets/"UI/Bars-Sliders-Scrollbars.png").string().c_str());
    frames_=LoadTexture((assets/"UI/Frames.png").string().c_str());
    for (auto texture : {units_,animals_,monsters_,interface_,bars_,frames_}) if (IsTextureValid(texture)) SetTextureFilter(texture,TEXTURE_FILTER_POINT);
}
EntityPresentation::~EntityPresentation() {
    for (auto texture : {units_,animals_,monsters_,interface_,bars_,frames_}) if (IsTextureValid(texture)) UnloadTexture(texture);
}
void EntityPresentation::drawCreature(const Entity& entity,const scene::CreatureCatalog& catalog,double time) const {
    const auto& species=catalog.species.at(static_cast<std::uint32_t>(entity.species().species));
    const auto& frames=catalog.frames(entity.species(),entity.vocation());
    const auto texture=species.sheet=="Animals.png"?animals_:species.sheet=="Monsters.png"?monsters_:units_;
    const int frame=static_cast<int>(std::fmod(time*frames.fps,frames.count));
    const int row=entity.control()==ControlOwnership::Player?frames.playerRow:frames.aiRow;
    const auto center=entity.position().worldPosition();const auto size=entity.position().cellSize();
    const Rectangle source{float((frames.column+frame)*8),float(row*8),8,8};
    if(IsTextureValid(texture) && source.x+8<=texture.width && source.y+8<=texture.height)
        DrawTexturePro(texture,source,{center.x-size/2,center.y-size/2,size,size},{},0,WHITE);
    else DrawRectangleRec({center.x-size/2,center.y-size/2,size,size},MAGENTA);
}
void EntityPresentation::drawCreature(Vector2 center,float size,ControlOwnership control,double time) const {
    // Idle starts at column 3. Loop only its first three frames; rows are palette variants.
    const int frame=static_cast<int>(std::fmod(time*3,3));
    const float row=control==ControlOwnership::Player?64.0f:32.0f;
    Rectangle destination{center.x-size/2,center.y-size/2,size,size};
    if (IsTextureValid(units_)) DrawTexturePro(units_,{float(24+frame*8),row,8,8},destination,{},0,WHITE);
    else DrawRectangleRec(destination,LIGHTGRAY);
}
void EntityPresentation::drawHover(Rectangle cell,double time) const {
    const int frame=static_cast<int>(std::fmod(time*6,3));
    if (IsTextureValid(interface_)) DrawTexturePro(interface_,{float(4+24*frame),4,16,16},
        {cell.x-cell.width/2,cell.y-cell.height/2,cell.width*2,cell.height*2},{},0,WHITE);
    else DrawRectangleLinesEx(cell,1,WHITE);
}
ai::Json EntityPresentation::inspectionData(const Entity& entity,const ai::DecisionSystem* decisions,int radius) {
    ai::Json result{{"id",entity.id().toString()},{"type",entity.type()},
        {"species",{{"id",static_cast<std::uint32_t>(entity.species().species)},{"subSpecies",static_cast<std::uint32_t>(entity.species().subSpecies)}}},
        {"vocation",entity.vocation().id},
        {"disposition",scene::dispositionName(entity.disposition())},
        {"control",entity.control()==ControlOwnership::Player?"player":"AI"},
        {"position",ai::Json::parse(entity.position().fetchData())},
        {"resources",ai::Json::parse(entity.resources().fetchData())}};
    if(entity.humanName())result["name"]={{"first",entity.humanName()->first},{"last",entity.humanName()->last},{"full",entity.displayName()}};
    auto stacks=ai::Json::array();
    for (const auto &item : entity.inventory())
        stacks.push_back({{"guid",item.guid.toString()},{"definitionId",item.definitionId},
                          {"displayName",item.displayName},{"quantity",item.quantity}});
    result["inventory"]={{"component","Inventory"},{"rulesDescription",scene::Inventory::RulesDescription},
                         {"maxStacks",scene::Inventory::maxStacks},{"items",std::move(stacks)}};
    if (decisions && entity.control()==ControlOwnership::AI) {
        result["perception"]=ai::Json::parse(scene::Range(radius).fetchData(entity.position().cellCoordinates()));
        auto goal=decisions->current(entity.id());
        result["currentGoal"]=goal?goal->json():ai::Json(nullptr);
        result["latestOutcome"]=decisions->lastOutcome(entity.id());
        result["distanceBand"]=decisions->bandFor(entity.id());
    }
    return result;
}
void EntityPresentation::drawTooltip(const SceneWorld& world,std::optional<CellPosition> cell,bool paused,Vector2 mouse) {
    const auto id=paused && cell?world.creatureAt(*cell):std::nullopt;
    if (!id) return;
    const auto entity=world.findCreature(*id); if (!entity) return;
    const auto &resources=entity->resources();
    int rows=0;
    for (const auto key : {scene::Resource::Health,scene::Resource::Hunger,scene::Resource::Thirst,scene::Resource::Sleep})
        if (resources.find(key)) ++rows;
    const float width=std::min(288.0f,float(GetScreenWidth()-16)), height=64+rows*52.0f+82;
    float x=mouse.x+20, y=mouse.y+18;
    if (x+width>GetScreenWidth()-8) x=mouse.x-width-20;
    x=std::max(8.0f,x); y=std::clamp(y,8.0f,std::max(8.0f,float(GetScreenHeight())-height-8));
    DrawRectangleRec({x+4,y+4,width,height},{0,0,0,100});
    DrawRectangleRec({x,y,width,height},{21,25,34,250});
    DrawRectangleLinesEx({x,y,width,height},1,{112,126,147,255});
    BeginScissorMode(int(x+12),int(y+10),int(width-24),24);
    DrawText(entity->displayName().c_str(),int(x+14),int(y+12),18,RAYWHITE);
    EndScissorMode();
    DrawText(scene::dispositionName(entity->disposition()),int(x+14),int(y+36),14,
             entity->disposition()==scene::Disposition::Hostile?RED:entity->disposition()==scene::Disposition::Friendly?LIME:LIGHTGRAY);
    float rowY=y+60;
    for (const auto &[key,label] : {std::pair{scene::Resource::Health,"Health"},
                                  std::pair{scene::Resource::Hunger,"Hunger (food reserve)"},
                                  std::pair{scene::Resource::Thirst,"Thirst (water reserve)"},
                                  std::pair{scene::Resource::Sleep,"Sleep (rest reserve)"}}) {
        if (!resources.find(key)) continue;
        DrawText(label,int(x+14),int(rowY),14,LIGHTGRAY);
        resources.draw(key,bars_,{x+14,rowY+18,width-28,28});
        rowY+=52;
    }
    DrawText(TextFormat("Inventory  %d / 6",static_cast<int>(entity->inventory().size())),int(x+14),int(rowY+4),14,LIGHTGRAY);
    constexpr float gap=4;
    const float slot=(width-28-5*gap)/6;
    for(std::size_t i=0;i<scene::Inventory::maxStacks;++i) {
        const Rectangle bounds{x+14+i*(slot+gap),rowY+24,slot,slot};
        DrawRectangleRec({bounds.x+3,bounds.y+3,bounds.width-6,bounds.height-6},{35,30,39,255});
        // The first gold frame from the existing 32px Frames atlas, with point filtering.
        if(IsTextureValid(frames_))DrawTexturePro(frames_,{4,4,32,32},bounds,{},0,WHITE);
        else DrawRectangleLinesEx(bounds,1,GOLD);
        BeginScissorMode(int(bounds.x+4),int(bounds.y+4),std::max(1,int(slot-8)),std::max(1,int(slot-8)));
        if(i<entity->inventory().size()) {
            const auto &item=entity->inventory()[i];
            std::string label=item.displayName;
            while(!label.empty() && MeasureText(label.c_str(),10)>slot-8) {
                auto n=label.size()-1;while(n>0 && (static_cast<unsigned char>(label[n])&0xc0)==0x80)--n;label.resize(n);
            }
            DrawText(label.c_str(),int(bounds.x+4),int(bounds.y+7),10,RAYWHITE);
            const auto quantity=item.quantity>=1'000'000?std::to_string(item.quantity/1'000'000)+"M":
                item.quantity>=10'000?std::to_string(item.quantity/1'000)+"k":std::to_string(item.quantity);
            DrawText(quantity.c_str(),int(bounds.x+slot-5-MeasureText(quantity.c_str(),10)),int(bounds.y+slot-15),10,GOLD);
        } else DrawText("-",int(bounds.x+slot/2-3),int(bounds.y+slot/2-5),10,GRAY);
        EndScissorMode();
    }
}
