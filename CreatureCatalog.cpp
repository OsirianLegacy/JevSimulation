#include "CreatureCatalog.h"
#include <set>
namespace scene {
namespace {
using Json=nlohmann::json;
void name(const std::string& value) {
    if(value.empty() || value.size()>64 || value.find_first_not_of(" \t\r\n")==std::string::npos || value.find_first_of("\r\n\t")!=std::string::npos || value.find('\0')!=std::string::npos)
        throw std::invalid_argument("Names must be 1-64 printable characters.");
    Json(value).dump();
}
void validateFrames(const SpriteFrames& f) {
    if(f.column<0 || f.aiRow<0 || f.playerRow<0 || f.column>4096 || f.aiRow>4096 || f.playerRow>4096 || f.count<1 || f.count>64 || !std::isfinite(f.fps) || f.fps<=0 || f.fps>60)
        throw std::invalid_argument("Invalid sprite frames.");
}
Json framesJson(const SpriteFrames& f) {return {{"column",f.column},{"aiRow",f.aiRow},{"playerRow",f.playerRow},{"count",f.count},{"fps",f.fps}};}
SpriteFrames readFrames(const Json& j) {return {j.at("column"),j.at("aiRow"),j.at("playerRow"),j.at("count"),j.at("fps")};}
}
void CreatureCatalog::validate() const {
    if (harvestables.size()>4096) throw std::invalid_argument("Too many harvest definitions.");
    for (const auto &[key,d] : harvestables) { name(key); d.validate(); }
    if(!species.contains(0) || species.size()>4096 || vocations.size()>4096 || vocations.contains(0)) throw std::invalid_argument("Invalid creature catalog.");
    std::set<std::string> names;
    for(const auto& [id,s]:species) {
        dispositionName(s.defaultDisposition);
        name(s.name); if(!names.insert(s.name).second) throw std::invalid_argument("Duplicate species name.");
        if(s.sheet!="Units.png" && s.sheet!="Animals.png" && s.sheet!="Monsters.png") throw std::invalid_argument("Unknown creature sprite sheet.");
        if(!s.subSpecies.contains(0) || s.subSpecies.size()>4096 || !s.resources.contains(Resource::Health) || s.resources.size()>Resource::maxPools) throw std::invalid_argument("Species requires Default subspecies and Health.");
        std::set<std::string> subs;for(const auto& [sub,n]:s.subSpecies) {name(n);if(!subs.insert(n).second)throw std::invalid_argument("Duplicate subspecies name.");}
        Resource pools;for(const auto& [key,max]:s.resources) pools.set(key,ResourcePool(max));
        if (!s.carcassHarvest.empty() && (!harvestables.contains(s.carcassHarvest) || !s.carcassTile.present() || s.carcassTile.column<0 || s.carcassTile.row<0))
            throw std::invalid_argument("Invalid species carcass definition or tile.");
        pools.fetchData();validateFrames(s.idle);
    }
    names.clear();for(const auto& [id,v]:vocations) {
        name(v.name);if(!names.insert(v.name).second)throw std::invalid_argument("Duplicate vocation name.");
        for(const auto& [s,f]:v.species) {if(!species.contains(s))throw std::invalid_argument("Unknown allowed species.");validateFrames(f);}
    }
}
bool CreatureCatalog::permits(SpeciesComponent s,VocationComponent v) const {
    const auto id=static_cast<std::uint32_t>(s.species);
    auto it=species.find(id);
    return it!=species.end() && it->second.subSpecies.contains(static_cast<std::uint32_t>(s.subSpecies)) &&
        (v.id==0 || (vocations.contains(v.id) && vocations.at(v.id).species.contains(id)));
}
const SpriteFrames& CreatureCatalog::frames(SpeciesComponent s,VocationComponent v) const {
    if(!permits(s,v))throw std::invalid_argument("Vocation is not allowed for this species.");
    const auto id=static_cast<std::uint32_t>(s.species);
    return v.id?vocations.at(v.id).species.at(id):species.at(id).idle;
}
nlohmann::json CreatureCatalog::json() const {
    Json j{{"version",2},{"species",Json::array()},{"vocations",Json::array()},{"harvestables",Json::object()}};
    for (const auto &[id,d] : harvestables) j["harvestables"][id]=d.json();
    for(const auto& [id,s]:species) {Json subs=Json::array();for(const auto& [i,n]:s.subSpecies)subs.push_back({{"id",i},{"name",n}});
        j["species"].push_back({{"id",id},{"name",s.name},{"sheet",s.sheet},{"subSpecies",subs},{"resources",s.resources},{"idle",framesJson(s.idle)},
            {"carcassHarvest",s.carcassHarvest},{"carcassTile",{s.carcassTile.tileset,s.carcassTile.column,s.carcassTile.row}},
            {"defaultDisposition",dispositionName(s.defaultDisposition)}});}
    for(const auto& [id,v]:vocations) {Json allowed=Json::array();for(const auto& [s,f]:v.species)allowed.push_back({{"id",s},{"frames",framesJson(f)}});
        j["vocations"].push_back({{"id",id},{"name",v.name},{"species",allowed}});}
    return j;
}
CreatureCatalog CreatureCatalog::fromJson(const Json& j) {
    if((j.at("version")!=1 && j.at("version")!=2) || j.at("species").size()>4096 || j.at("vocations").size()>4096)throw std::invalid_argument("Unsupported creature catalog.");
    CreatureCatalog c;c.species.clear();
    if (j.at("version")==2) {
        if (!j.at("harvestables").is_object() || j.at("harvestables").size()>4096) throw std::invalid_argument("Invalid harvest catalog.");
        c.harvestables.clear();
        for (const auto &[key,value] : j.at("harvestables").items()) c.harvestables.emplace(key,HarvestableDefinition::fromJson(value));
    }
    for(const auto& s:j.at("species")) {SpeciesDefinition d;d.name=s.at("name");d.sheet=s.at("sheet");d.resources=s.at("resources").get<std::map<std::string,float>>();d.idle=readFrames(s.at("idle"));d.subSpecies.clear();
        d.defaultDisposition=parseDisposition(s.value("defaultDisposition",std::string("neutral")));
        if (j.at("version")==2) { d.carcassHarvest=s.at("carcassHarvest");const auto &t=s.at("carcassTile");d.carcassTile={t.at(0),t.at(1),t.at(2)}; }
        for(const auto& sub:s.at("subSpecies"))if(!d.subSpecies.emplace(sub.at("id").get<std::uint32_t>(),sub.at("name").get<std::string>()).second)throw std::invalid_argument("Duplicate subspecies ID.");
        if(!c.species.emplace(s.at("id").get<std::uint32_t>(),d).second)throw std::invalid_argument("Duplicate species ID.");}
    for(const auto& v:j.at("vocations")) {VocationDefinition d;d.name=v.at("name");for(const auto& s:v.at("species"))if(!d.species.emplace(s.at("id").get<std::uint32_t>(),readFrames(s.at("frames"))).second)throw std::invalid_argument("Duplicate permission.");
        if(!c.vocations.emplace(v.at("id").get<std::uint32_t>(),d).second)throw std::invalid_argument("Duplicate vocation ID.");}
    c.validate();return c;
}
}
