#include "util/i18n.h"

#include <windows.h>

#include <atomic>

#include "util/win_paths.h"

namespace ts3ss {
namespace {

struct Entry {
    const char* de;
    const char* en;
};

// Order must match enum Str exactly. The static_assert below is the guard rail: adding
// an id without a translation stops the build rather than showing an empty line on the
// display.
constexpr Entry kStrings[] = {
    // --- OLED --------------------------------------------------------------
    /* MicOff          */ {"Mikro aus", "Mic off"},
    /* SoundOff        */ {"Ton aus", "Sound off"},
    /* AwayShort       */ {"Abwesend", "Away"},
    /* MutedAlert      */ {"MIKRO AUS!", "MIC IS OFF!"},
    /* MutedAlertLine2 */ {"du sprichst", "you talk"},
    /* PokeFrom        */ {"STUPS von", "POKE from"},
    /* IsHere          */ {"ist da", "joined"},
    /* IsOnline        */ {"ist online", "is online"},
    /* Connecting      */ {"Verbinde...", "Connecting..."},
    /* Disconnected    */ {"Getrennt", "Disconnected"},
    /* Connected       */ {"Verbunden", "Connected"},
    /* PingHigh        */ {"Ping hoch", "Ping high"},
    /* PacketLoss      */ {"Paketverlust", "Packet loss"},

    // --- Widget names -------------------------------------------------------
    /* WidgetTalkers           */ {"Wer spricht", "Who is talking"},
    /* WidgetTalkingWhileMuted */ {"Stumm gesprochen", "Talking while muted"},
    /* WidgetPoke              */ {"Angestupst", "Poked"},
    /* WidgetChatMessage       */ {"Neue Nachricht", "New message"},
    /* WidgetServerJoin        */ {"Buddy kommt online", "Buddy comes online"},
    /* WidgetChannelJoin       */ {"Betritt Channel", "Joins channel"},
    /* WidgetConnection        */ {"Verbindungsstatus", "Connection status"},
    /* WidgetConnectionQuality */ {"Ping / Paketverlust", "Ping / packet loss"},
    /* WidgetMuteStatus        */ {"Mute / Deaf", "Mute / deaf"},
    /* WidgetChannelInfo       */ {"Channel", "Channel"},

    // --- Settings window ----------------------------------------------------
    /* DialogTitle      */ {"TS3 SteelSeries OLED \xE2\x80\x93 Einstellungen",
                            "TS3 SteelSeries OLED \xE2\x80\x93 Settings"},
    /* LabelWidgets     */ {"Anzeigen (Reihenfolge = Priorit\xC3\xA4t):",
                            "Display items (order = priority):"},
    /* ButtonUp         */ {"Nach oben", "Move up"},
    /* ButtonDown       */ {"Nach unten", "Move down"},
    /* LabelDuration    */ {"Dauer (1-60 s):", "Duration (1-60 s):"},
    /* ButtonSet        */ {"Setzen", "Apply"},
    /* LabelLanguage    */ {"Sprache:", "Language:"},
    /* LangAuto         */ {"Automatisch", "Automatic"},
    /* LangGerman       */ {"Deutsch", "German"},
    /* LangEnglish      */ {"Englisch", "English"},
    /* LabelThresholds  */ {"Warnschwellen:", "Warning thresholds:"},
    /* LabelPing        */ {"Ping (ms):", "Ping (ms):"},
    /* LabelPacketLoss  */ {"Verlust (%):", "Loss (%):"},
    /* LabelBuddies     */ {"Buddys (UID, oder Rechtsklick im Client):",
                            "Buddies (UID, or right-click in the client):"},
    /* ButtonAdd        */ {"Hinzuf\xC3\xBCgen", "Add"},
    /* ButtonRemove     */ {"Entfernen", "Remove"},
    /* ButtonSave       */ {"Speichern", "Save"},
    /* ButtonClose      */ {"Schlie\xC3\x9F\x65n", "Close"},
    /* ColumnDisplay    */ {"Anzeige", "Display"},
    /* ColumnDuration   */ {"Dauer", "Duration"},
    /* StatusConnected  */ {"GameSense verbunden", "GameSense connected"},
    /* StatusNoGameSense*/ {"SteelSeries GG l\xC3\xA4uft nicht", "SteelSeries GG is not running"},
};

static_assert(sizeof(kStrings) / sizeof(kStrings[0]) == static_cast<size_t>(Str::Count),
              "kStrings and enum Str have drifted apart - every id needs both translations");

std::atomic<int> g_language{static_cast<int>(Language::English)};

Language detectFromSystem() {
    const LANGID id = GetUserDefaultUILanguage();
    return PRIMARYLANGID(id) == LANG_GERMAN ? Language::German : Language::English;
}

}  // namespace

void setLanguage(Language language) {
    if (language == Language::Auto)
        language = detectFromSystem();
    g_language.store(static_cast<int>(language));
}

Language currentLanguage() { return static_cast<Language>(g_language.load()); }

const char* tr(Str id) {
    const auto index = static_cast<size_t>(id);
    if (index >= static_cast<size_t>(Str::Count))
        return "";

    const Entry& entry = kStrings[index];
    return currentLanguage() == Language::German ? entry.de : entry.en;
}

std::wstring trW(Str id) { return utf8ToWide(tr(id)); }

Language languageFromString(const std::string& value) {
    if (value == "de")
        return Language::German;
    if (value == "en")
        return Language::English;
    return Language::Auto;
}

const char* languageToString(Language language) {
    switch (language) {
        case Language::German:  return "de";
        case Language::English: return "en";
        case Language::Auto:    break;
    }
    return "auto";
}

}  // namespace ts3ss
