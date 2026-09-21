#pragma once
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace scene {
enum class Disposition : std::uint32_t { Hostile, Neutral, Friendly };
inline const char *dispositionName(Disposition value) {
    switch(value) {
        case Disposition::Hostile: return "hostile";
        case Disposition::Neutral: return "neutral";
        case Disposition::Friendly: return "friendly";
    }
    throw std::invalid_argument("Invalid disposition.");
}
inline Disposition parseDisposition(std::string_view value) {
    for(auto state:{Disposition::Hostile,Disposition::Neutral,Disposition::Friendly})
        if(value==dispositionName(state))return state;
    throw std::invalid_argument("Disposition must be hostile, neutral or friendly.");
}
struct DispositionComponent {
    Disposition value=Disposition::Neutral;
};
}
