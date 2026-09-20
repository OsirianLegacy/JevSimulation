#include "Entity.h"
#include <stdexcept>
#include <utility>

Entity::Entity(std::string type, scene::ResourcePool health, scene::Position position)
    : type_(std::move(type)), position_(position) {
    if (type_.empty() || type_.size() > maxTypeBytes ||
        type_.find('\0') != std::string::npos ||
        type_.find_first_not_of(" \t\r\n") == std::string::npos)
        throw std::invalid_argument("Entity type must be nonblank and at most 255 bytes.");
    resources_.set(scene::Resource::Health, health);
    id_ = GenerateUniqueGuid();
}

bool Entity::removeResource(const std::string &key) {
    if (key == scene::Resource::Health)
        throw std::invalid_argument("Entities must have a Health resource.");
    return resources_.remove(key);
}
