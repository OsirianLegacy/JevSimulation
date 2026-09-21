#include "Resource.h"
#include <charconv>

void scene::Resource::advanceNeeds(float seconds) {
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::invalid_argument("Need elapsed time must be finite and nonnegative.");
    for (const auto &[key, duration] : {std::pair{Hunger, HungerDuration},
                                       std::pair{Thirst, ThirstDuration},
                                       std::pair{Sleep, SleepDuration}}) {
        const auto it = pools_.find(key);
        if (it == pools_.end()) continue;
        auto &pool = it->second;
        const double drain = static_cast<double>(pool.maximum()) * seconds / duration;
        pool.adjust(-static_cast<float>(std::min(drain, static_cast<double>(pool.current()))));
    }
}

Color scene::Resource::drawColor(const std::string &key) {
    if (key==Health) return {225,55,65,255};
    if (key==Stamina) return {80,185,110,255};
    if (key==Mana || key==Thirst) return {70,155,230,255};
    if (key==Hunger) return {230,175,65,255};
    if (key==Sleep) return {155,125,225,255};
    return {170,115,205,255};
}

void scene::Resource::draw(const std::string &key,Texture2D atlas,Rectangle b) const {
    const auto* pool=find(key);
    if (!pool || b.width<=0 || b.height<=0) return;
    const float sx=b.width/24,sy=b.height/8;
    // The atlas frame at (0,88) has a transparent interior. Paint under its edges.
    const Rectangle interior{b.x+sx,b.y+2*sy,22*sx,4*sy};
    DrawRectangleRec(interior,{49,42,45,255});
    DrawRectangleRec({interior.x,interior.y,interior.width*pool->fraction(),interior.height},drawColor(key));
    if (IsTextureValid(atlas)) DrawTexturePro(atlas,{0,88,24,8},b,{},0,WHITE);
    else DrawRectangleLinesEx(b,1,GOLD);
    char text[64];
    const auto result=std::to_chars(text,text+sizeof(text)-1,pool->current());
    *result.ptr='\0';
    const int size=std::max(10,static_cast<int>(b.height*0.45f));
    const int x=static_cast<int>(b.x+(b.width-MeasureText(text,size))/2);
    const int y=static_cast<int>(b.y+(b.height-size)/2);
    DrawText(text,x+1,y+1,size,BLACK);
    DrawText(text,x,y,size,RAYWHITE);
}

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
