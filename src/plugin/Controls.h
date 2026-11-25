#pragma once
// Controls that can be driven from outside the editor: MIDI CC, MIDI learn and (in the app)
// keyboard shortcuts. Also the built-in presets.

#include "UiStrings.h"

#include <array>
#include <initializer_list>
#include <utility>

namespace ctl
{
enum class Target : int
{
    filterLen = 0, step, refDelay, resAmount, resFloor,
    autoMode, freeze, aecOff, reset,
    mode, latency, refMode,
    presetNext, presetPrev,
    count
};

enum class Kind { continuous, toggle, trigger, choice };

struct TargetInfo
{
    const char* paramId; // nullptr for actions that are not parameters
    Kind kind;
    ui::Str name;
    int defaultCc;
};

inline const TargetInfo& info (Target t)
{
    static const TargetInfo table[] = {
        { "filterLen", Kind::continuous, ui::Str::knobFilterLen, 20 },
        { "step", Kind::continuous, ui::Str::knobStep, 21 },
        { "refDelay", Kind::continuous, ui::Str::knobRefDelay, 22 },
        { "resAmount", Kind::continuous, ui::Str::knobResAmount, 23 },
        { "resFloor", Kind::continuous, ui::Str::knobResFloor, 24 },
        { "auto", Kind::toggle, ui::Str::autoMode, 25 },
        { "freeze", Kind::toggle, ui::Str::freeze, 26 },
        { "aecOff", Kind::toggle, ui::Str::aecOff, 27 },
        { nullptr, Kind::trigger, ui::Str::resetFilter, 28 },
        { "mode", Kind::choice, ui::Str::ctlOutput, 29 },
        { "latency", Kind::choice, ui::Str::ctlLatency, 30 },
        { "refMode", Kind::choice, ui::Str::ctlRef, 31 },
        { nullptr, Kind::trigger, ui::Str::ctlPresetNext, 32 },
        { nullptr, Kind::trigger, ui::Str::ctlPresetPrev, 33 },
    };
    static_assert (sizeof (table) / sizeof (table[0]) == (size_t) Target::count, "missing control target");
    return table[(size_t) t];
}

constexpr int numTargets = (int) Target::count;

//==============================================================================
// Built-in presets. Values are in each parameter's own units; parameters not listed keep the
// default. The user's language and MIDI mapping are never part of a preset.
struct FactoryPreset
{
    ui::Str name;
    std::initializer_list<std::pair<const char*, float>> values;
};

inline const std::array<FactoryPreset, 7>& factoryPresets()
{
    static const std::array<FactoryPreset, 7> presets { {
        { ui::Str::pDefault, {} },
        // protect the voice: gentle suppression, Auto adapts the filter to the room
        { ui::Str::pSinging, { { "resAmount", 0.15f }, { "resFloor", -15.0f }, { "auto", 1.0f } } },
        // speech intelligibility matters more than a pristine voice: stronger suppression
        { ui::Str::pTalk, { { "step", 0.6f }, { "resAmount", 0.4f }, { "resFloor", -24.0f } } },
        { ui::Str::pStream, { { "resAmount", 0.3f }, { "resFloor", -20.0f }, { "auto", 1.0f } } },
        { ui::Str::pEchoRoom, { { "filterLen", 450.0f }, { "resAmount", 0.3f }, { "resFloor", -22.0f }, { "auto", 1.0f } } },
        { ui::Str::pLowLatency, { { "latency", 1.0f }, { "refMode", 0.0f } } },
        { ui::Str::pMaxRemoval, { { "filterLen", 400.0f }, { "step", 0.6f }, { "resAmount", 0.6f }, { "resFloor", -30.0f } } },
    } };
    return presets;
}

// parameters a preset stores (everything the user tunes)
inline const std::array<const char*, 11>& presetParamIds()
{
    static const std::array<const char*, 11> ids { "filterLen", "step", "refDelay", "resAmount", "resFloor",
                                                   "freeze", "mode", "aecOff", "auto", "latency", "refMode" };
    return ids;
}
} // namespace ctl
