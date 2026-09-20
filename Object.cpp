#include "Object.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

void Object::validate(ObjectType type, bool open, const std::vector<Item>& contents) {
    if (type > ObjectType::Gatherable || (type == ObjectType::Gatherable && open) ||
        (type == ObjectType::Door && !contents.empty()) || contents.size() > maxStacks)
        throw std::invalid_argument("Invalid object properties or contents.");
    const auto validText = [](const std::string& text) {
        return !text.empty() && text.size() <= maxTextBytes &&
            std::any_of(text.begin(), text.end(), [](unsigned char c) { return !std::isspace(c); }) &&
            std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; });
    };
    for (const auto& item : contents)
        if (!validText(item.definitionId) || !validText(item.displayName) || !item.quantity)
            throw std::invalid_argument("Items need an ID, name (1-255 bytes), and positive quantity.");
}
