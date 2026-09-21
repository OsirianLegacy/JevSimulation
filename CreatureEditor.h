#pragma once
#include "SceneWorld.h"
#include <filesystem>
struct EditorInput;
class CreatureEditor {
public:
    explicit CreatureEditor(std::filesystem::path assets):assets_(std::move(assets)){}
    ~CreatureEditor();
    void reset();
    bool capturesKeyboard() const {return focus_>=0;}
    bool update(SceneWorld&,const EditorInput&,int height);
    void draw(int height) const;
private:
    std::filesystem::path assets_;
    scene::CreatureCatalog draft_;
    bool loaded_=false,vocations_=false,player_=false;
    std::uint32_t species_=0,vocation_=0;
    int focus_=-1, listOffset_=0;
    Vector2 scroll_{};
    std::string name_,subName_,resource_="thirst",capacity_="100",message_;
    Texture2D checks_{}, sheet_{};
    std::string sheetName_;
    void select();
    void loadSheet();
    scene::SpriteFrames* frames();
    const scene::SpriteFrames* frames() const;
    std::vector<std::string> resourceKeys() const;
};
