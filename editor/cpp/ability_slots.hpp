#pragma once
#include <array>
namespace starlight::editor {
enum class AbilityAction { None, BasicAttack };
struct AbilitySlot {char key;AbilityAction action=AbilityAction::None;bool enabled=false;};
struct AbilitySlots {
    std::array<AbilitySlot,6> slots{{{'1',AbilityAction::BasicAttack,true},{'2'},{'3'},{'4'},{'5'},{'R'}}};
    static bool recognizes(unsigned key){return (key>='1' && key<='5') || key=='R';}
    AbilityAction action(unsigned key)const{for(const auto& slot:slots)if(slot.key==key && slot.enabled)return slot.action;return AbilityAction::None;}
};
}
