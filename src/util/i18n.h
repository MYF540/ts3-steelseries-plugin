#pragma once

#include <string>

namespace ts3ss {

enum class Language {
    Auto,     // follow the Windows UI language
    German,
    English,
};

// Every user-visible string in the plugin, on the OLED and in the settings window.
//
// An enum plus a table rather than a lookup by English key: the compiler then catches a
// missing translation, and the display path does no string hashing per frame.
enum class Str : int {
    // --- OLED --------------------------------------------------------------
    MicOff,
    SoundOff,
    AwayShort,
    MutedAlert,       // "MIKRO AUS!"
    MutedAlertLine2,  // "du sprichst"
    PokeFrom,
    IsHere,
    IsOnline,
    Connecting,
    Disconnected,
    Connected,
    PingHigh,
    PacketLoss,

    // --- Widget names in the settings window --------------------------------
    WidgetTalkers,
    WidgetTalkingWhileMuted,
    WidgetPoke,
    WidgetChatMessage,
    WidgetServerJoin,
    WidgetChannelJoin,
    WidgetConnection,
    WidgetConnectionQuality,
    WidgetMuteStatus,
    WidgetChannelInfo,

    // --- Settings window ----------------------------------------------------
    DialogTitle,
    LabelWidgets,
    ButtonUp,
    ButtonDown,
    LabelDuration,
    ButtonSet,
    LabelLanguage,
    LangAuto,
    LangGerman,
    LangEnglish,
    LabelThresholds,
    LabelPing,
    LabelPacketLoss,
    LabelBuddies,
    ButtonAdd,
    ButtonRemove,
    ButtonSave,
    ButtonClose,
    ColumnDisplay,
    ColumnDuration,
    StatusConnected,
    StatusNoGameSense,

    Count,
};

// Auto resolves to German on a German Windows UI, English everywhere else.
void     setLanguage(Language language);
Language currentLanguage();

// UTF-8, valid for the lifetime of the process. Safe from any thread once the language
// has been set at start-up.
const char* tr(Str id);

// For the Win32 controls.
std::wstring trW(Str id);

// Config round-trip: "auto" | "de" | "en".
Language    languageFromString(const std::string& value);
const char* languageToString(Language language);

}  // namespace ts3ss
