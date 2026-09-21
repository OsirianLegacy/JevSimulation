#include "Harvestable.h"
namespace scene {
void validateHarvestPool(const ResourcePool &pool) {
    if (pool.maximum() < 1 || pool.maximum() > 1'000'000 ||
        std::floor(pool.maximum()) != pool.maximum() || std::floor(pool.current()) != pool.current())
        throw std::invalid_argument("Harvest units must be whole numbers with capacity 1..1000000.");
}
void HarvestableDefinition::validate() const {
    if (depletedTile.tileset < -1 || depletedTile.column<0 || depletedTile.row<0)
        throw std::invalid_argument("Invalid depleted tile.");
    Object::validate(ObjectType::Container, false, {{"name", name, 1}, {"state", depletedState, 1}});
    if (action != "chop" && action != "mine" && action != "break" && action != "butcher")
        throw std::invalid_argument("Harvest action must be chop, mine, break, or butcher.");
    if (!std::isfinite(workSeconds) || workSeconds <= 0 || workSeconds > 3600 ||
        !units || units > 1'000'000 || !std::isfinite(regenerationSeconds) ||
        regenerationSeconds < 0 || regenerationSeconds > 86400 * 365 ||
        (removeWhenDepleted && regenerationSeconds > 0) || yields.empty() || yields.size() > 16)
        throw std::invalid_argument("Invalid harvest duration, units, yields, or regeneration.");
    Object::validate(ObjectType::Container, false, yields);
    if (!requiredTool.empty()) Object::validate(ObjectType::Container, false, {{requiredTool,requiredTool,1}});
    for (const auto &item : yields) if (!item.guid.empty())
        throw std::invalid_argument("Harvest recipes cannot contain stack GUIDs.");
    json().dump(); // Validate UTF-8 before the definition reaches inspection or persistence.
}
nlohmann::json HarvestableDefinition::json() const {
    auto items = nlohmann::json::array();
    for (const auto &i : yields) items.push_back({{"id",i.definitionId},{"name",i.displayName},{"quantity",i.quantity}});
    return {{"name",name},{"action",action},{"workSeconds",workSeconds},{"units",units},{"yields",items},
            {"requiredTool",requiredTool},{"depletedState",depletedState},{"removeWhenDepleted",removeWhenDepleted},
            {"regenerationSeconds",regenerationSeconds},{"depletedTile",{depletedTile.tileset,depletedTile.column,depletedTile.row}}};
}
HarvestableDefinition HarvestableDefinition::fromJson(const nlohmann::json &j) {
    HarvestableDefinition d;
    d.name=j.at("name"); d.action=j.at("action"); d.workSeconds=j.at("workSeconds");
    const auto integer=[](const nlohmann::json &v, double max) {
        if (!v.is_number_integer() || v.get<double>() < 1 || v.get<double>() > max)
            throw std::invalid_argument("Invalid harvest integer.");
        return v.get<std::uint32_t>();
    };
    d.units=integer(j.at("units"),1'000'000);
    if (!j.at("yields").is_array() || j.at("yields").size()>16) throw std::invalid_argument("Too many yields.");
    for (const auto &i : j.at("yields")) d.yields.push_back({i.at("id"),i.at("name"),integer(i.at("quantity"),UINT32_MAX)});
    d.requiredTool=j.at("requiredTool"); d.depletedState=j.at("depletedState");
    d.removeWhenDepleted=j.at("removeWhenDepleted"); d.regenerationSeconds=j.at("regenerationSeconds");
    if(j.contains("depletedTile")){const auto &t=j.at("depletedTile");d.depletedTile={t.at(0),t.at(1),t.at(2)};}
    d.validate(); return d;
}
std::map<std::string, HarvestableDefinition> defaultHarvestables() {
    return {
        {"tree",{"Tree","chop",2,5,{{"wood","Wood",2},{"branch","Branch",1}},"","Stump"}},
        {"ore",{"Ore node","mine",3,4,{{"ore","Ore",1},{"stone","Stone",1}},"","Empty deposit"}},
        {"rock",{"Rock","break",2,3,{{"stone","Stone",2}},"","Rubble"}},
        {"carcass",{"Animal carcass","butcher",3,1,{{"hide","Hide",1},{"meat","Meat",2},{"bone","Bone",1}},"","Remains"}}
    };
}
}
