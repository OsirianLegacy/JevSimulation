#include "Resource.h"
#include <charconv>

namespace {
void appendKey(std::string &json, const std::string &key) {
    constexpr char hex[] = "0123456789abcdef";
    json += '"';
    for (std::size_t i = 0; i < key.size();) {
        const auto c = static_cast<unsigned char>(key[i]);
        if (c >= 0x80) {
            // Preserve UTF-8, rejecting overlong encodings, surrogates and invalid scalars.
            const std::size_t count = c >= 0xc2 && c <= 0xdf ? 2 :
                                      c >= 0xe0 && c <= 0xef ? 3 :
                                      c >= 0xf0 && c <= 0xf4 ? 4 : 0;
            if (!count || i + count > key.size())
                throw std::invalid_argument("Resource keys must be valid UTF-8 for JSON.");
            unsigned scalar = c & (0x7f >> count);
            for (std::size_t j = 1; j < count; ++j) {
                const auto next = static_cast<unsigned char>(key[i + j]);
                if ((next & 0xc0) != 0x80)
                    throw std::invalid_argument("Resource keys must be valid UTF-8 for JSON.");
                scalar = (scalar << 6) | (next & 0x3f);
            }
            if ((count == 3 && scalar < 0x800) || (count == 4 && scalar < 0x10000) ||
                (scalar >= 0xd800 && scalar <= 0xdfff) || scalar > 0x10ffff)
                throw std::invalid_argument("Resource keys must be valid UTF-8 for JSON.");
            json.append(key, i, count);
            i += count;
            continue;
        }
        if (c == '"' || c == '\\') {
            json += '\\';
            json += static_cast<char>(c);
        } else if (c < 0x20) {
            json += "\\u00";
            json += hex[c >> 4];
            json += hex[c & 0xf];
        } else {
            json += static_cast<char>(c);
        }
        ++i;
    }
    json += '"';
}
void appendNumber(std::string &json, float value) {
    char buffer[64];
    // Locale-independent, shortest representation that round-trips to the float.
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{})
        throw std::runtime_error("Could not serialize resource value.");
    json.append(buffer, result.ptr);
}
} // namespace

std::string scene::Resource::fetchData() const {
    std::string json = "{\"component\":\"Resource\",\"rulesDescription\":";
    appendKey(json, RulesDescription);
    json += ",\"pools\":{";
    bool first = true;
    for (const auto &[key, pool] : pools_) {
        if (!first) json += ',';
        first = false;
        appendKey(json, key);
        json += ":{\"current\":";
        appendNumber(json, pool.current());
        json += ",\"maximum\":";
        appendNumber(json, pool.maximum());
        json += ",\"rulesDescription\":";
        appendKey(json, rulesDescription(key));
        json += '}';
    }
    json += "}}";
    return json;
}
