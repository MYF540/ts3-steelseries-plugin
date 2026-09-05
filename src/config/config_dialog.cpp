#include "config/config_dialog.h"

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

#include "resource.h"
#include "util/i18n.h"
#include "util/log.h"
#include "util/win_paths.h"
#include "widgets/registry.h"

namespace ts3ss {
namespace {

// Everything the dialog procedure needs, passed through DialogBoxParam.
struct DialogState {
    Config                     config;
    ConfigDialog::ApplyFn      apply;
    ConfigDialog::StatusFn     status;
};

std::mutex        g_threadMutex;
std::thread       g_thread;

// Two separate flags on purpose: g_open is true from the moment the thread is started,
// while g_window only becomes valid at WM_INITDIALOG. Using the handle alone would let
// a second request slip through in the gap between the two and open a duplicate window.
std::atomic<bool> g_open{false};
std::atomic<HWND> g_window{nullptr};

// The DLL's own instance handle, without needing a DllMain: ask the loader which module
// contains a known address inside this file.
HINSTANCE selfInstance() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                           | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&selfInstance), &module);
    return reinterpret_cast<HINSTANCE>(module);
}

std::wstring displayNameFor(const std::string& id) {
    if (const IWidget* widget = WidgetRegistry::instance().find(id))
        return utf8ToWide(std::string(widget->displayName()));
    return utf8ToWide(id);
}

void fillWidgetList(HWND list, const Config& config) {
    ListView_DeleteAllItems(list);

    for (size_t i = 0; i < config.widgets.size(); ++i) {
        const auto& widget = config.widgets[i];
        auto        name   = displayNameFor(widget.id);

        LVITEMW item{};
        item.mask     = LVIF_TEXT | LVIF_PARAM;
        item.iItem    = static_cast<int>(i);
        item.pszText  = name.data();
        item.lParam   = static_cast<LPARAM>(i);
        ListView_InsertItem(list, &item);

        const auto seconds = std::to_wstring(widget.duration.count() / 1000) + L" s";
        ListView_SetItemText(list, static_cast<int>(i), 1,
                             const_cast<LPWSTR>(seconds.c_str()));

        ListView_SetCheckState(list, static_cast<int>(i), widget.enabled ? TRUE : FALSE);
    }
}

void fillBuddyList(HWND list, const Config& config) {
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& buddy : config.buddies) {
        const auto wide = utf8ToWide(buddy);
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
    }
}

int selectedWidget(HWND list) {
    return ListView_GetNextItem(list, -1, LVNI_SELECTED);
}

// Reads the checkboxes back before any reorder or save, so a click is never lost just
// because the user did not leave the row.
void harvestCheckboxes(HWND list, Config& config) {
    for (size_t i = 0; i < config.widgets.size(); ++i)
        config.widgets[i].enabled = ListView_GetCheckState(list, static_cast<int>(i)) != FALSE;
}

void moveSelected(HWND dialog, Config& config, int delta) {
    HWND      list  = GetDlgItem(dialog, IDC_WIDGETS);
    const int index = selectedWidget(list);
    if (index < 0)
        return;

    const int target = index + delta;
    if (target < 0 || target >= static_cast<int>(config.widgets.size()))
        return;

    harvestCheckboxes(list, config);
    std::swap(config.widgets[static_cast<size_t>(index)],
              config.widgets[static_cast<size_t>(target)]);

    fillWidgetList(list, config);
    ListView_SetItemState(list, target, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    SetFocus(list);
}

void showDurationOf(HWND dialog, const Config& config, int index) {
    if (index < 0 || index >= static_cast<int>(config.widgets.size()))
        return;

    const auto seconds = config.widgets[static_cast<size_t>(index)].duration.count() / 1000;
    SetDlgItemTextW(dialog, IDC_DURATION, std::to_wstring(seconds).c_str());
}

void applyDuration(HWND dialog, Config& config) {
    HWND      list  = GetDlgItem(dialog, IDC_WIDGETS);
    const int index = selectedWidget(list);
    if (index < 0)
        return;

    BOOL      translated = FALSE;
    const int seconds    = static_cast<int>(GetDlgItemInt(dialog, IDC_DURATION, &translated, FALSE));
    if (!translated)
        return;

    // Clamped here as well as in the loader. Silently correcting an out-of-range number
    // in front of the user beats accepting it and quietly changing it on next start.
    const long long ms = std::min(std::max(static_cast<long long>(seconds) * 1000,
                                           Config::kMinDuration.count()),
                                  Config::kMaxDuration.count());

    harvestCheckboxes(list, config);
    config.widgets[static_cast<size_t>(index)].duration = std::chrono::milliseconds(ms);

    fillWidgetList(list, config);
    ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    showDurationOf(dialog, config, index);
}

void removeSelectedBuddy(HWND dialog, Config& config) {
    HWND      list  = GetDlgItem(dialog, IDC_BUDDIES);
    const auto index = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (index < 0 || index >= static_cast<int>(config.buddies.size()))
        return;

    config.buddies.erase(config.buddies.begin() + index);
    fillBuddyList(list, config);
}

std::string trimmed(std::string text) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

// Adds a buddy typed as a unique identifier.
//
// The context menu in the client is the comfortable route, but it only works for people
// currently visible. Pasting a UID covers everyone else - and someone who is offline is
// exactly the person a "comes online" notification is for.
void addTypedBuddy(HWND dialog, Config& config) {
    wchar_t buffer[256]{};
    GetDlgItemTextW(dialog, IDC_BUDDY_UID, buffer, static_cast<int>(std::size(buffer)));

    const auto uid = trimmed(wideToUtf8(buffer));
    if (uid.empty())
        return;

    if (config.isBuddy(uid)) {
        SetDlgItemTextW(dialog, IDC_BUDDY_UID, L"");
        return;
    }

    config.buddies.push_back(uid);
    fillBuddyList(GetDlgItem(dialog, IDC_BUDDIES), config);
    SetDlgItemTextW(dialog, IDC_BUDDY_UID, L"");
}

void showThresholds(HWND dialog, const Config& config) {
    SetDlgItemInt(dialog, IDC_PING, static_cast<UINT>(config.pingWarnMs), FALSE);

    // One decimal: packet loss below a percent still matters, whole percent does not
    // resolve it.
    wchar_t loss[32]{};
    swprintf_s(loss, L"%.1f", config.packetLossWarn);
    SetDlgItemTextW(dialog, IDC_PACKET_LOSS, loss);
}

void harvestThresholds(HWND dialog, Config& config) {
    BOOL       translated = FALSE;
    const UINT ping       = GetDlgItemInt(dialog, IDC_PING, &translated, FALSE);
    if (translated) {
        config.pingWarnMs = std::min(std::max(static_cast<int>(ping), Config::kMinPingWarnMs),
                                     Config::kMaxPingWarnMs);
    }

    wchar_t loss[32]{};
    GetDlgItemTextW(dialog, IDC_PACKET_LOSS, loss, static_cast<int>(std::size(loss)));

    // Accepts both decimal separators: the edit box is free text, and a German keyboard
    // produces a comma without the user thinking about it.
    std::wstring text(loss);
    std::replace(text.begin(), text.end(), L',', L'.');

    try {
        const double value = std::stod(text);
        config.packetLossWarn = std::min(std::max(value, Config::kMinPacketLossWarn),
                                         Config::kMaxPacketLossWarn);
    } catch (...) {
        // Unparseable: keep what was there rather than snapping to a default the user
        // never chose.
    }
}

void fillLanguageBox(HWND dialog, const Config& config) {
    HWND box = GetDlgItem(dialog, IDC_LANGUAGE);
    SendMessageW(box, CB_RESETCONTENT, 0, 0);

    const Str labels[] = {Str::LangAuto, Str::LangGerman, Str::LangEnglish};
    for (Str label : labels)
        SendMessageW(box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(trW(label).c_str()));

    int index = 0;
    switch (config.language) {
        case Language::Auto:    index = 0; break;
        case Language::German:  index = 1; break;
        case Language::English: index = 2; break;
    }
    SendMessageW(box, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
}

void harvestLanguage(HWND dialog, Config& config) {
    const auto index = SendMessageW(GetDlgItem(dialog, IDC_LANGUAGE), CB_GETCURSEL, 0, 0);
    switch (index) {
        case 1:  config.language = Language::German;  break;
        case 2:  config.language = Language::English; break;
        default: config.language = Language::Auto;    break;
    }
}

void initialiseDialog(HWND dialog, DialogState* state) {
    // All user-visible text is set here rather than in the .rc. That is what makes the
    // German/English switch possible at all, and it keeps the resource file ASCII so no
    // umlaut can fall victim to resource-compiler encoding rules.
    SetWindowTextW(dialog, trW(Str::DialogTitle).c_str());
    SetDlgItemTextW(dialog, IDC_LABEL_WIDGETS, trW(Str::LabelWidgets).c_str());
    SetDlgItemTextW(dialog, IDC_UP, trW(Str::ButtonUp).c_str());
    SetDlgItemTextW(dialog, IDC_DOWN, trW(Str::ButtonDown).c_str());
    SetDlgItemTextW(dialog, IDC_LABEL_DURATION, trW(Str::LabelDuration).c_str());
    SetDlgItemTextW(dialog, IDC_DURATION_APPLY, trW(Str::ButtonSet).c_str());
    SetDlgItemTextW(dialog, IDC_LABEL_LANGUAGE, trW(Str::LabelLanguage).c_str());
    SetDlgItemTextW(dialog, IDC_LABEL_THRESHOLDS, trW(Str::LabelThresholds).c_str());
    SetDlgItemTextW(dialog, IDC_LABEL_PING, trW(Str::LabelPing).c_str());
    SetDlgItemTextW(dialog, IDC_LABEL_LOSS, trW(Str::LabelPacketLoss).c_str());
    SetDlgItemTextW(dialog, IDC_LABEL_BUDDIES, trW(Str::LabelBuddies).c_str());
    SetDlgItemTextW(dialog, IDC_BUDDY_ADD, trW(Str::ButtonAdd).c_str());
    SetDlgItemTextW(dialog, IDC_BUDDY_REMOVE, trW(Str::ButtonRemove).c_str());
    SetDlgItemTextW(dialog, IDOK, trW(Str::ButtonSave).c_str());
    SetDlgItemTextW(dialog, IDCANCEL, trW(Str::ButtonClose).c_str());

    HWND list = GetDlgItem(dialog, IDC_WIDGETS);
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);

    LVCOLUMNW column{};
    column.mask    = LVCF_TEXT | LVCF_WIDTH;
    column.cx      = 250;
    auto  headerA  = trW(Str::ColumnDisplay);
    column.pszText = headerA.data();
    ListView_InsertColumn(list, 0, &column);

    column.cx      = 60;
    auto headerB   = trW(Str::ColumnDuration);
    column.pszText = headerB.data();
    ListView_InsertColumn(list, 1, &column);

    fillWidgetList(list, state->config);
    fillBuddyList(GetDlgItem(dialog, IDC_BUDDIES), state->config);
    fillLanguageBox(dialog, state->config);
    showThresholds(dialog, state->config);

    if (!state->config.widgets.empty()) {
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        showDurationOf(dialog, state->config, 0);
    }

    if (state->status)
        SetDlgItemTextW(dialog, IDC_STATUS, utf8ToWide(state->status()).c_str());
}

INT_PTR CALLBACK dialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

    switch (message) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<DialogState*>(lParam);
            SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
            g_window.store(dialog);
            initialiseDialog(dialog, state);
            return TRUE;
        }

        case WM_NOTIFY: {
            if (!state)
                break;
            const auto* header = reinterpret_cast<LPNMHDR>(lParam);
            if (header->idFrom == IDC_WIDGETS && header->code == LVN_ITEMCHANGED) {
                const auto* changed = reinterpret_cast<LPNMLISTVIEW>(lParam);
                if (changed->uNewState & LVIS_SELECTED)
                    showDurationOf(dialog, state->config, changed->iItem);
            }
            break;
        }

        case WM_COMMAND: {
            if (!state)
                break;

            switch (LOWORD(wParam)) {
                case IDC_UP:             moveSelected(dialog, state->config, -1); return TRUE;
                case IDC_DOWN:           moveSelected(dialog, state->config, +1); return TRUE;
                case IDC_DURATION_APPLY: applyDuration(dialog, state->config);    return TRUE;
                case IDC_BUDDY_ADD:      addTypedBuddy(dialog, state->config);    return TRUE;
                case IDC_BUDDY_REMOVE:   removeSelectedBuddy(dialog, state->config); return TRUE;

                case IDOK: {
                    // Everything typed but not confirmed with a button is collected
                    // here too. Losing a threshold because the user went straight to
                    // Save would be the kind of small betrayal nobody reports.
                    harvestCheckboxes(GetDlgItem(dialog, IDC_WIDGETS), state->config);
                    harvestThresholds(dialog, state->config);
                    harvestLanguage(dialog, state->config);
                    addTypedBuddy(dialog, state->config);

                    if (state->apply)
                        state->apply(state->config);
                    EndDialog(dialog, IDOK);
                    return TRUE;
                }

                case IDCANCEL:
                    EndDialog(dialog, IDCANCEL);
                    return TRUE;

                default:
                    break;
            }
            break;
        }

        case WM_CLOSE:
            EndDialog(dialog, IDCANCEL);
            return TRUE;

        default:
            break;
    }

    return FALSE;
}

void runDialog(DialogState state) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC  = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    // A modal loop is safe here because this runs on our own thread, never on the
    // client's UI thread.
    const auto result = DialogBoxParamW(selfInstance(), MAKEINTRESOURCEW(IDD_SETTINGS),
                                        nullptr, dialogProc, reinterpret_cast<LPARAM>(&state));
    if (result == -1)
        TS3SS_ERROR << "Settings dialog failed to open, error " << GetLastError();

    g_window.store(nullptr);
    g_open.store(false);
}

}  // namespace

void ConfigDialog::showAsync(std::shared_ptr<const Config> current, ApplyFn apply,
                             StatusFn status) {
    std::lock_guard<std::mutex> lock(g_threadMutex);

    // Already open: bring it forward instead of opening a second copy, whose save would
    // silently overwrite the first one's edits.
    if (g_open.load()) {
        if (HWND existing = g_window.load())
            SetForegroundWindow(existing);
        return;
    }

    if (!current) {
        TS3SS_WARN << "Settings dialog requested without a config";
        return;
    }

    if (g_thread.joinable())
        g_thread.join();  // previous dialog has closed; reap its thread

    g_open.store(true);
    DialogState state{*current, std::move(apply), std::move(status)};

    g_thread = std::thread([state = std::move(state)]() mutable { runDialog(std::move(state)); });
}

void ConfigDialog::shutdown() {
    if (HWND open = g_window.load())
        PostMessageW(open, WM_CLOSE, 0, 0);

    // Joined without holding g_threadMutex: the dialog thread does not take it, but
    // keeping a lock across a join is how deadlocks get built by accident.
    if (g_thread.joinable())
        g_thread.join();
}

}  // namespace ts3ss
