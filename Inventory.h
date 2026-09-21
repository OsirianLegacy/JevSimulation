#pragma once
#include "Object.h"

namespace scene {
// ECS ownership component. Stacks live on ItemStack entities linked with ContainedBy;
// they are never duplicated into a second mutable list on the owner.
struct Inventory {
    static constexpr std::size_t maxStacks = 6;
    static constexpr std::uint32_t gatherableStackLimit = 10;
    static constexpr const char *RulesDescription =
        "Ordered item stacks with persistent identities and positive quantities. "
        "Units have six slots. Gatherable pickup fills matching stacks to ten units before using another slot. "
        "No weight limits or consumption.";
    static void validate(const std::vector<Item> &items) {
        if(items.size()>maxStacks)throw std::length_error("Unit inventory is limited to six slots.");
        Object::validate(ObjectType::Container, false, items);
    }
};
}
