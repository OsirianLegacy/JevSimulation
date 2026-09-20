#pragma once
#include "Guid.h"
#include <cstdint>
#include <string>
#include <vector>

enum class ObjectType : std::uint32_t { Door, Container, Gatherable };
struct ItemDefinition {
    std::string id, displayName;
    Guid guid{};
    bool operator==(const ItemDefinition&) const = default;
};
struct ObjectTypeDefinition {
    std::string id, displayName;
    ObjectType behavior = ObjectType::Container;
    Guid guid{};
    bool operator==(const ObjectTypeDefinition&) const = default;
};
struct Item {
    std::string definitionId;
    std::string displayName;
    std::uint32_t quantity = 1;
    Guid guid{}; // Assigned when the stack is added to the world.
    bool operator==(const Item&) const = default;
};
class WorldDocument;
class SceneWorld;
class Object {
public:
    bool operator==(const Object&) const = default;
    const Guid& id() const { return id_; }
    ObjectType type() const { return type_; }
    const std::string& typeDefinitionId() const { return typeDefinitionId_; }
    bool isOpen() const { return isOpen_; }
    bool isContainer() const { return type_ == ObjectType::Container; }
    const std::vector<Item>& contents() const { return contents_; }
    static constexpr std::size_t maxStacks = 1024, maxTextBytes = 255;
    static void validate(ObjectType type, bool open, const std::vector<Item>& contents);
private:
    friend class WorldDocument;
    friend class SceneWorld;
    Guid id_;
    ObjectType type_ = ObjectType::Container;
    std::string typeDefinitionId_;
    bool isOpen_ = false;
    std::vector<Item> contents_;
};
