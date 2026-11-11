// 設定パネルの実装。寸法は 1200×740 の画像の px で書く。
#include "settings_panel.h"

#include "brightness.h"
#include "draw.h"
#include "eye_actions.h"
#include "frame_apps_cairo.h"  // vendor/frame-apps/cpp
#include "hud_render.h"
#include "i18n.h"
#include "icons.h"
#include "media.h"
#include "osc.h"
#include "theme.h"

#include <cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace {

constexpr double kW = SettingsPanel::kWidth;
constexpr double kH = SettingsPanel::kHeight;
constexpr double kPad = 24;            // パネルの外側の余白
constexpr double kHeaderY = 24;        // 見出しの行
constexpr double kHeaderH = 60;
constexpr double kContentY = 100;      // タブの中身の上端（見出しの下に 16px）
constexpr double kFooterLineH = 20;    // いちばん下の 1 行（14px）
constexpr double kContentBottom = kH - kPad - kFooterLineH - 16;  // タブの中身の下端
constexpr double kContentW = kW - kPad * 2;
constexpr double kLongMinMs = 300;     // 押し方タブの − / ＋ で動かせる範囲
constexpr double kLongMaxMs = 1500;
constexpr double kLongStepMs = 100;
constexpr double kGapMinMs = 200;
constexpr double kGapMaxMs = 800;
constexpr double kGapStepMs = 50;
constexpr double kRecentSec = 600;     // 「さっき押した」を出す時間（秒）
constexpr double kAssignGap = 12;      // 割り当てタブのカードの間
constexpr double kPickerH = 190;       // 割り当てタブの下の列（選んだカードに割り当てる）の高さ
constexpr double kPickerChipH = 50;    // 下の列の選択肢の高さ
constexpr int kPickerActionCols = 5;   // 下の列のアクションの側の列の数（9 つを 2 段に）

/**
 * CSS の行（line-height: normal ≒ 1.45）の上端から、文字のベースラインを出す。
 * @param top 行の上端
 * @param size 文字の大きさ
 * @return ベースラインの y
 */
double lineBaseline(double top, double size) {
    return centerBaseline(top, size * 1.45, size);
}

/**
 * UTF-8 の 1 文字の長さ（バイト）。
 * @param c 先頭のバイト
 * @return 1〜4
 */
size_t utf8Length(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    return 4;
}

/**
 * 幅で折り返す（英語は空白で、日本語は文字で）。maxLines を超えたら最後の行を「…」で切る。
 * @param pen 描画の道具
 * @param text 文字列
 * @param size 大きさ
 * @param maxW 幅
 * @param maxLines 行の上限
 * @return 行の一覧
 */
std::vector<std::string> wrapText(const Pen& pen, const std::string& text, double size, double maxW, int maxLines) {
    std::vector<std::string> lines;
    std::string line;
    size_t lastSpace = std::string::npos;  // line の中の、最後の空白の位置
    for (size_t i = 0; i < text.size();) {
        const size_t len = utf8Length(static_cast<unsigned char>(text[i]));
        const std::string ch = text.substr(i, len);
        i += len;
        if (ch == "\n") {
            lines.push_back(line);
            line.clear();
            lastSpace = std::string::npos;
            continue;
        }
        if (pen.measure(line + ch, size, false) > maxW && !line.empty()) {
            if (ch == " ") {
                lines.push_back(line);
                line.clear();
                lastSpace = std::string::npos;
                continue;
            }
            if (lastSpace != std::string::npos && lastSpace > 0) {
                lines.push_back(line.substr(0, lastSpace));
                line = line.substr(lastSpace + 1);
            } else {
                lines.push_back(line);
                line.clear();
            }
            lastSpace = std::string::npos;
        }
        if (ch == " ") lastSpace = line.size();
        line += ch;
    }
    if (!line.empty()) lines.push_back(line);
    if (static_cast<int>(lines.size()) > maxLines) {
        lines.resize(static_cast<size_t>(maxLines));
        lines.back() = ellipsize(pen, lines.back() + "…", size, false, maxW);
    }
    return lines;
}

/**
 * printf の書式に文字列を 2 つ当てはめる。
 * @param format 書式（%s が 2 つ）
 * @param a 1 つ目
 * @param b 2 つ目
 * @return 結果
 */
std::string format2(const char* format, const std::string& a, const std::string& b) {
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), format, a.c_str(), b.c_str());
    return buffer;
}

/**
 * % の表記（例: 60%）。
 * @param value 値
 * @return 文字列
 */
std::string percentText(double value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%.0f%%", value);
    return text;
}

/**
 * 秒の表記（例: 0.6 秒 / 0.35 秒）。
 * @param t 言語の表
 * @param ms ミリ秒
 * @param decimals 小数点以下の桁
 * @return 文字列
 */
std::string secondsText(const UiText& t, double ms, int decimals) {
    char number[16];
    std::snprintf(number, sizeof(number), "%.*f", decimals, ms / 1000.0);
    return formatText(t.secondsFormat, number);
}

/**
 * ホームフォルダの下なら ~ で書く。
 * @param path パス
 * @return 表示用のパス
 */
std::string homeShort(const std::string& path) {
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') return path;
    const std::string prefix = home;
    if (path == prefix) return "~";
    if (path.compare(0, prefix.size() + 1, prefix + "/") == 0) return "~" + path.substr(prefix.size());
    return path;
}

/**
 * 明るさの段の並び（例: 100% → 60% → 40%）。
 * @param levels 段
 * @return 文字列
 */
std::string levelsText(const std::vector<double>& levels) {
    std::string out;
    for (size_t i = 0; i < levels.size(); ++i) out += (i == 0 ? "" : " → ") + percentText(levels[i]);
    return out;
}

/**
 * 角の丸い四角を塗る。
 * @param pen 描画の道具
 * @param x 左
 * @param y 上
 * @param w 幅
 * @param h 高さ
 * @param r 角の半径
 * @param c 色
 * @param alpha 不透明度
 */
void fillRounded(const Pen& pen, double x, double y, double w, double h, double r, Color c, double alpha = 1.0) {
    pen.color(c, alpha);
    pen.roundedRect(x, y, w, h, std::min(r, std::min(w, h) / 2));
    cairo_fill(pen.cr);
}

/**
 * 点線の枠を描く。
 * @param pen 描画の道具
 * @param x 左
 * @param y 上
 * @param w 幅
 * @param h 高さ
 * @param r 角の半径
 * @param c 色
 * @param width 線の太さ
 */
void strokeDashed(const Pen& pen, double x, double y, double w, double h, double r, Color c, double width) {
    const double dashes[] = {width * 3, width * 2};
    cairo_set_dash(pen.cr, dashes, 2, 0);
    strokeRounded(pen, x, y, w, h, r, c, width);
    cairo_set_dash(pen.cr, nullptr, 0, 0);
}

/**
 * ✓ を前に付けた文言の幅。
 * @param pen 描画の道具
 * @param label 文言
 * @param size 大きさ
 * @param checked ✓ を付けるか（付けるなら太字）
 * @return 幅
 */
double checkedLabelWidth(const Pen& pen, const std::string& label, double size, bool checked) {
    return pen.measure(label, size, checked) + (checked ? size * 0.95 : 0);
}

/**
 * ✓ を前に付けた文言を描く（✓ はフォントに頼らず線で描く）。
 * @param pen 描画の道具
 * @param x 左
 * @param baseline ベースライン
 * @param label 文言
 * @param size 大きさ
 * @param c 色
 * @param checked ✓ を付けるか（付けるなら太字）
 */
void drawCheckedLabel(const Pen& pen, double x, double baseline, const std::string& label, double size, Color c,
                      bool checked) {
    if (checked) {
        drawCheck(pen.cr, x + size * 0.4, baseline - size * 0.36, size * 0.75, c);
        x += size * 0.95;
    }
    pen.text(x, baseline, label, size, c, checked);
}

/**
 * 押し方の番号から、その押し方の印（回数の丸・長押しの棒・タップの回数の輪）を描く。
 * @param pen 描画の道具
 * @param slot 押し方
 * @param x 左
 * @param cy 縦の真ん中
 * @return 印の右端
 */
double drawSlotMarks(const Pen& pen, int slot, double x, double cy) {
    if (isTapSlot(slot)) {
        // タップは中の空いた輪（aux の回数押しの丸と見分ける）
        const int count = slot == kTap2Slot ? 2 : 3;
        cairo_set_source_rgb(pen.cr, kTextLabel.r, kTextLabel.g, kTextLabel.b);
        cairo_set_line_width(pen.cr, 2);
        for (int k = 0; k < count; ++k) {
            cairo_new_sub_path(pen.cr);
            cairo_arc(pen.cr, x + 5 + k * 18, cy, 4, 0, 2 * M_PI);
        }
        cairo_stroke(pen.cr);
        return x + count * 10 + (count - 1) * 8;
    }
    if (slot >= kLongPressSlot) {
        fillRounded(pen, x, cy - 5, 44, 10, 5, kTextLabel);
        return x + 44;
    }
    for (int k = 0; k <= slot; ++k) drawDisc(pen.cr, x + 5 + k * 18, cy, 5, kTextLabel);
    return x + (slot + 1) * 10 + slot * 8;
}

/**
 * アクションが今使えるか（入っていないアプリを使うものは使えない）。
 * @param type 種類
 * @param view 状態
 * @return 使えるなら true
 */
bool actionUsable(ActionType type, const PanelView& view) {
    if (type == ActionType::PerfOverlayToggle) return !(view.haveStatus && view.status.perfOverlay == AppState::Missing);
    if (isEyeAction(type)) {
        return view.eyePanel.state != EyePanelState::Missing && view.eyePanel.state != EyePanelState::Outdated;
    }
    return true;
}

/**
 * 使えないアクションの選択肢に付ける札（入っていない・更新が必要）。
 * @param t 言語の表
 * @param type 種類
 * @param view 状態
 * @return 札（使えるなら空）
 */
std::string unusableTag(const UiText& t, ActionType type, const PanelView& view) {
    if (actionUsable(type, view)) return "";
    if (isEyeAction(type) && view.eyePanel.state == EyePanelState::Outdated) return t.chipOutdated;
    return t.chipMissing;
}

/**
 * 割り当てが今使えるか（入っていないアプリ・消えたコマンドは使えない）。
 * @param action 割り当て
 * @param config 設定
 * @param view 状態
 * @return 使えるなら true
 */
bool assignmentUsable(const Action& action, const Config& config, const PanelView& view) {
    if (action.type == ActionType::Command) {
        return findCommand(config.commands, view.folderCommands, action.commandId) != nullptr;
    }
    return actionUsable(action.type, view);
}

/**
 * 設定のコマンドとフォルダのコマンドを、設定の並び（command_order）の順に 1 つにまとめる。
 * @param config 設定
 * @param view 状態
 * @return 一覧
 */
std::vector<CommandDef> allCommands(const Config& config, const PanelView& view) {
    return orderedCommands(config, view.folderCommands);
}

/**
 * ページの数（少なくとも 1）。
 * @param count 項目の数
 * @param perPage 1 ページの数
 * @return ページの数
 */
int pageCount(int count, int perPage) {
    return std::max(1, (count + perPage - 1) / perPage);
}

/**
 * ページめくりのボタン（‹ ›）を描く。
 * @param pen 描画の道具
 * @param x 左
 * @param y 上
 * @param w 幅
 * @param h 高さ
 * @param r 角の半径
 * @param label ‹ か ›
 * @param size 文字の大きさ
 * @param enabled 押せるか
 * @param hot ポインターが乗っているか
 */
void drawNavButton(const Pen& pen, double x, double y, double w, double h, double r, const char* label, double size,
                   bool enabled, bool hot) {
    if (enabled) fillRounded(pen, x, y, w, h, r, hot ? kControlHover : kControl);
    strokeRounded(pen, x, y, w, h, r, enabled ? kBorder : kBorderDisabled, 2);
    textCentered(pen, x + w / 2, centerBaseline(y, h, size) - size * 0.08, label, size, enabled ? kText : kBorder, false);
}

/**
 * カードの 2 行目（今の状態）。
 * @param t 言語の表
 * @param action 割り当て
 * @param config 設定
 * @param view 状態
 * @return 文言
 */
std::string stateText(const UiText& t, const Action& action, const Config& config, const PanelView& view) {
    const Status& s = view.status;
    const bool known = view.haveStatus;
    switch (action.type) {
        case ActionType::MicMuteToggle:
            if (!known || s.micMuted < 0) return t.stMicUnknown;
            return s.micMuted ? t.stMicMuted : t.stMicOn;
        case ActionType::BrightnessCycle:
            if (!known || s.brightnessPercent < 0) return levelsText(config.brightnessLevels);
            return format2(t.stBrightFormat, percentText(s.brightnessPercent), levelsText(config.brightnessLevels));
        case ActionType::RoomViewToggle:
            if (!known || s.roomView < 0) return t.stPassUnknown;
            return s.roomView ? t.stPassOn : t.stPassOff;
        case ActionType::Screenshot: return homeShort(expandHome(config.screenshotDir));
        case ActionType::PerfOverlayToggle:
            if (!known) return t.stPerfUnknown;
            if (s.perfOverlay == AppState::Missing) return t.stPerfMissing;
            if (s.perfOverlay == AppState::Stopped) return t.stPerfStopped;
            if (s.perfVisible < 0) return t.stPerfUnknown;
            return s.perfVisible ? t.stPerfShown : t.stPerfHidden;
        case ActionType::DashboardOpen: return t.stDash;
        case ActionType::Recenter: return t.stRecenter;
        case ActionType::OriginalAux: return t.stOriginal;
        case ActionType::MediaPlayPause:
        case ActionType::MediaNext:
        case ActionType::MediaPrevious: return t.stMedia;
        case ActionType::EyeRecenter:
        case ActionType::EyeFit:
        case ActionType::EyeRelearn:
            if (view.eyePanel.state == EyePanelState::Missing) return t.stEyeMissing;
            if (view.eyePanel.state == EyePanelState::Outdated) return t.stEyeOutdated;
            return action.type == ActionType::EyeRecenter ? t.stEyeRecenter
                   : action.type == ActionType::EyeFit    ? t.stEyeFit
                                                          : t.stEyeRelearn;
        case ActionType::Command: {
            const CommandDef* command = findCommand(config.commands, view.folderCommands, action.commandId);
            if (command == nullptr) return t.stCmdMissing;
            // 組み込みの OSC の送信（VRChat の状態は返ってこないので、送り先だけを書く）
            SendOscCommand osc;
            if (parseSendOscCommand(command->argv, osc)) {
                return osc.address == kVrchatVoiceAddress ? std::string(t.stOscVoice) : formatText(t.stOscFormat, osc.address);
            }
            return joinCommandLine(command->argv);
        }
        case ActionType::None: return t.stNone;
    }
    return "";
}

/**
 * 明るさの段を 1 つ動かす（10〜125 の 5 刻み）。
 * @param value 今の値
 * @param delta 動かす量
 * @return 新しい値
 */
double stepLevel(double value, double delta) {
    const double next = std::round((value + delta) / kBrightnessStepPct) * kBrightnessStepPct;
    return std::clamp(next, kMinBrightnessPct, kMaxBrightnessPct);
}

/**
 * 時間を刻みの目盛りに寄せて動かす。
 * @param value 今の値（ms）
 * @param delta 動かす量（ms、正か負）
 * @param minValue 最小
 * @param maxValue 最大
 * @return 新しい値（ms）
 */
int stepMs(int value, double delta, double minValue, double maxValue) {
    const double step = std::fabs(delta);
    const double steps = value / step;
    const double next = delta > 0 ? (std::floor(steps + 1e-6) + 1) * step : (std::ceil(steps - 1e-6) - 1) * step;
    return static_cast<int>(std::lround(std::clamp(next, minValue, maxValue)));
}

/**
 * 欄の文字の大きさと、等幅か。
 * @param field 欄の番号
 * @param mono 等幅なら true の書き込み先
 * @return 文字の大きさ
 */
double fieldFont(int field, bool& mono) {
    mono = field != SettingsPanel::kFieldName;
    return field == SettingsPanel::kFieldName ? 19 : (field == SettingsPanel::kFieldCommand ? 17 : 18);
}

/**
 * 欄の文字の幅。
 * @param pen 描画の道具
 * @param field 欄の番号
 * @param text 文字
 * @return 幅
 */
double fieldTextWidth(const Pen& pen, int field, const std::string& text) {
    if (text.empty()) return 0.0;
    bool mono = false;
    const double size = fieldFont(field, mono);
    return mono ? pen.measureMono(text, size) : pen.measure(text, size, false);
}

/**
 * 等幅の文字を幅に入るよう後ろを「…」にして縮める。
 * @param pen 描画の道具
 * @param text 文字
 * @param size 大きさ
 * @param maxW 幅
 * @return 入る文字列
 */
std::string ellipsizeMono(const Pen& pen, std::string text, double size, double maxW) {
    if (pen.measureMono(text, size) <= maxW) return text;
    while (!text.empty() && pen.measureMono(text + "…", size) > maxW) {
        size_t cut = text.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
        text.erase(cut);
    }
    return text + "…";
}

}  // namespace

const char* panelTabName(PanelTab tab) {
    switch (tab) {
        case PanelTab::Assign: return "assign";
        case PanelTab::Brightness: return "brightness";
        case PanelTab::Press: return "press";
        case PanelTab::Commands: return "commands";
        case PanelTab::Display: return "display";
        case PanelTab::App: return "app";
    }
    return "assign";
}

bool parsePanelTab(const std::string& name, PanelTab& tab) {
    for (int i = 0; i < kPanelTabCount; ++i) {
        const PanelTab candidate = static_cast<PanelTab>(i);
        if (name == panelTabName(candidate)) {
            tab = candidate;
            return true;
        }
    }
    return false;
}

bool applyPanelAction(const PanelAction& action, Config& config) {
    using K = PanelAction::Kind;
    const std::string before = configToJson(config);
    std::vector<double>& levels = config.brightnessLevels;
    const auto levelIndexValid = [&](int i) { return i >= 0 && i < static_cast<int>(levels.size()); };
    switch (action.kind) {
        case K::Assign: {
            if (action.slot < 0 || action.slot >= kSlotCount) break;
            const ActionType type = static_cast<ActionType>(action.arg);
            Action& slot = config.slot(action.slot);
            if (type == ActionType::Command && action.text.empty()) break;  // どのコマンドかが要る
            slot.type = type;
            slot.commandId = type == ActionType::Command ? action.text : std::string();
            break;
        }
        case K::CommandDelete: removeCommand(config, action.text); break;
        case K::LevelDec:
            if (levelIndexValid(action.arg)) levels[action.arg] = stepLevel(levels[action.arg], -kBrightnessStepPct);
            break;
        case K::LevelInc:
            if (levelIndexValid(action.arg)) levels[action.arg] = stepLevel(levels[action.arg], kBrightnessStepPct);
            break;
        case K::LevelRemove:
            if (levelIndexValid(action.arg) && static_cast<int>(levels.size()) > kMinBrightnessLevels) {
                levels.erase(levels.begin() + action.arg);
            }
            break;
        case K::LevelAdd:
            if (static_cast<int>(levels.size()) < kMaxBrightnessLevels) {
                levels.push_back(stepLevel(levels.empty() ? 100.0 : levels.back(), -20.0));
            }
            break;
        case K::LongDec: config.longPressMs = stepMs(config.longPressMs, -kLongStepMs, kLongMinMs, kLongMaxMs); break;
        case K::LongInc: config.longPressMs = stepMs(config.longPressMs, kLongStepMs, kLongMinMs, kLongMaxMs); break;
        case K::GapDec: config.multiPressGapMs = stepMs(config.multiPressGapMs, -kGapStepMs, kGapMinMs, kGapMaxMs); break;
        case K::GapInc: config.multiPressGapMs = stepMs(config.multiPressGapMs, kGapStepMs, kGapMinMs, kGapMaxMs); break;
        case K::SoundOn: config.soundEnabled = true; break;
        case K::SoundOff: config.soundEnabled = false; break;
        case K::HudOn: config.hudEnabled = true; break;
        case K::HudOff: config.hudEnabled = false; break;
        case K::BadgeOn: config.muteBadge = true; break;
        case K::BadgeOff: config.muteBadge = false; break;
        case K::BackgroundOn: config.runInBackground = true; break;
        case K::BackgroundOff: config.runInBackground = false; break;
        case K::HeadPointerOn: config.originalAuxOnHeadPointer = true; break;
        case K::HeadPointerOff: config.originalAuxOnHeadPointer = false; break;
        case K::CheckOn: config.updateCheck = true; break;
        case K::CheckOff: config.updateCheck = false; break;
        case K::LanguageJa: config.language = Language::Ja; break;
        case K::LanguageEn: config.language = Language::En; break;
        case K::ResetAll: {
            // 言語は画面の見え方なので残す（それ以外はすべて既定値）
            const Language language = config.language;
            config = Config();
            config.language = language;
            break;
        }
        default: break;  // 設定を変えない操作（呼び出し側やパネルの中で扱う）
    }
    return configToJson(config) != before;
}

SettingsPanel::SettingsPanel(const FontSet& fonts) : fonts_(fonts) {
    surface_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kWidth, kHeight);
    cr_ = cairo_create(surface_);
    cairo_font_options_t* options = cairo_font_options_create();
    cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_SLIGHT);
    cairo_set_font_options(cr_, options);
    cairo_font_options_destroy(options);
}

SettingsPanel::~SettingsPanel() {
    cairo_destroy(cr_);
    cairo_surface_destroy(surface_);
}

void SettingsPanel::addButton(const PanelAction& action, double x, double y, double w, double h, bool enabled) {
    buttons_.push_back({action, x, y, w, h, enabled});
}

PanelAction SettingsPanel::hitTest(double x, double y) const {
    // 後から足したもの（手前に描いたもの）を優先する
    for (auto it = buttons_.rbegin(); it != buttons_.rend(); ++it) {
        if (!it->enabled) continue;
        if (x >= it->x && x <= it->x + it->w && y >= it->y && y <= it->y + it->h) return it->action;
    }
    return PanelAction();
}

bool SettingsPanel::buttonCenter(const PanelAction& action, double& x, double& y) const {
    for (const auto& b : buttons_) {
        if (b.action == action) {
            x = b.x + b.w / 2;
            y = b.y + b.h / 2;
            return true;
        }
    }
    return false;
}

bool SettingsPanel::buttonBounds(const PanelAction& action, double& x, double& y, double& w, double& h) const {
    for (const auto& b : buttons_) {
        if (b.action == action) {
            x = b.x;
            y = b.y;
            w = b.w;
            h = b.h;
            return true;
        }
    }
    return false;
}

static std::vector<std::string> reorderedIds(const std::vector<std::string>& origOrder, int origIndex, double offsetPx,
                                             double pitch);

bool SettingsPanel::pointerMove(double x, double y) {
    if (appsDragging_) {
        // 一覧の地を押したまま動かしている: 指についていくようにスクロールする
        const double next = std::clamp(appsDragStartScroll_ + (appsDragStartY_ - y), 0.0, appsMaxScroll_);
        if (next == appsScroll_) return false;
        appsScroll_ = next;
        return true;
    }
    if (!dragId_.empty()) {
        // つまみで動かしている間は、ほかのボタンに乗っても反応しない
        dragPointerY_ = y;
        const std::vector<std::string> next =
            reorderedIds(dragOrigOrder_, dragOrigIndex_, y - dragStartY_ + listScroll_ - dragStartScroll_,
                         kListRowH + kListRowGap);
        if (next == dragOrder_) return false;
        dragOrder_ = next;
        return true;
    }
    const PanelAction now = hitTest(x, y);
    if (now == hover_) return false;
    hover_ = now;
    return true;
}

PanelAction SettingsPanel::pointerDown(double x, double y, double now) {
    using K = PanelAction::Kind;
    hover_ = hitTest(x, y);
    pressed_ = hover_;
    const PanelAction action = pressed_;
    // 終了・すべて既定に戻すは、誤って押しても動かないよう 1 回目は確認の表示にするだけ
    if (action.kind == K::Quit) {
        resetArmed_ = false;
        if (quitArmed_ && now <= quitArmedUntil_) return action;
        quitArmed_ = true;
        quitArmedUntil_ = now + kConfirmSec;
        return PanelAction();
    }
    if (action.kind == K::ResetAll) {
        quitArmed_ = false;
        if (resetArmed_ && now <= resetArmedUntil_) {
            resetArmed_ = false;
            return action;
        }
        resetArmed_ = true;
        resetArmedUntil_ = now + kConfirmSec;
        return PanelAction();
    }
    if (action.kind == K::CommandDelete) {
        quitArmed_ = false;
        resetArmed_ = false;
        if (deleteArmedId_ == action.text && now <= deleteArmedUntil_) {
            deleteArmedId_.clear();
            return action;
        }
        deleteArmedId_ = action.text;
        deleteArmedUntil_ = now + kConfirmSec;
        return PanelAction();
    }
    quitArmed_ = false;   // 別のボタンを押したら確認は取り消す
    resetArmed_ = false;
    deleteArmedId_.clear();
    if (action.kind == K::UpdateInstall) {
        updateConfirmArmed_ = true;
        return PanelAction();
    }
    if (action.kind == K::UpdateConfirmNo) {
        updateConfirmArmed_ = false;
        return PanelAction();
    }
    updateConfirmArmed_ = false;
    if (action.kind == K::Tab) {
        tab_ = static_cast<PanelTab>(action.arg);
        appsListOpen_ = false;
        appsConfirmOpen_ = false;
        return PanelAction();
    }
    if (action.kind == K::SelectSlot) {
        selectedSlot_ = action.arg;
        autoPicker_ = true;  // 割り当てに合わせて、アクション・コマンドの側とページを決め直す
        return PanelAction();
    }
    if (action.kind == K::SheetName || action.kind == K::SheetCommand || action.kind == K::SheetIp) {
        // 欄を押した: 押した位置に一番近い文字の境目を入力位置にする（開いていても閉じていても。呼び出し側がそこから
        // キーボードを開く）。押した瞬間に SteamVR がキーボードを閉じることがあるので、開き直しても同じ位置になる
        const Pen pen {cr_, &fonts_};
        const int field = action.kind == K::SheetName ? kFieldName : (action.kind == K::SheetCommand ? kFieldCommand : kFieldIp);
        const std::string& value = sheetFieldText(field);
        const double scroll = field == activeField_ ? fieldScroll_ : 0.0;  // 入力中でない欄はずらしていない
        const double target = x - fieldTextX_[field] + scroll;
        size_t best = 0;
        double bestDistance = std::fabs(target);
        for (size_t i = 0; i < value.size();) {
            i += utf8Length(static_cast<unsigned char>(value[i]));
            const std::string prefix = value.substr(0, std::min(i, value.size()));
            const double w = fieldTextWidth(pen, field, prefix);
            if (std::fabs(target - w) < bestDistance) {
                bestDistance = std::fabs(target - w);
                best = prefix.size();
            }
        }
        if (field == activeField_) sheetCursor_ = best;
        tapCursor_ = best;
        tapCursorPending_ = true;
        caretRestart_ = true;
        return action;
    }
    switch (action.kind) {
        case K::PickerActions: pickerSide_ = PickerSide::Actions; return PanelAction();
        case K::PickerMedia: pickerSide_ = PickerSide::Media; return PanelAction();
        case K::PickerEye: pickerSide_ = PickerSide::Eye; return PanelAction();
        case K::PickerCommands: pickerSide_ = PickerSide::Commands; return PanelAction();
        case K::PickerPrev: pickerPage_ = std::max(0, pickerPage_ - 1); return PanelAction();
        case K::PickerNext: pickerPage_ = std::min(pickerPageCount_ - 1, pickerPage_ + 1); return PanelAction();
        case K::Grip: {
            // つまみ: 押したまま上下に動かすと並び替える（離すまで pointerMove で追う）
            dragId_ = action.text;
            dragOrigOrder_.clear();
            for (const CommandDef& command : lastCommands_) dragOrigOrder_.push_back(command.id);
            dragOrder_ = dragOrigOrder_;
            dragOrigIndex_ = 0;
            for (size_t i = 0; i < dragOrigOrder_.size(); ++i) {
                if (dragOrigOrder_[i] == dragId_) dragOrigIndex_ = static_cast<int>(i);
            }
            dragStartY_ = y;
            dragPointerY_ = y;
            dragStartScroll_ = listScroll_;
            return PanelAction();
        }
        case K::AddCommand:
            tab_ = PanelTab::Commands;
            openSheet(nullptr);
            return PanelAction();
        case K::CommandEdit:
            for (const CommandDef& command : lastCommands_) {
                if (command.id == action.text && !command.fromFolder) openSheet(&command);
            }
            return PanelAction();
        case K::SheetCancel:
            sheetOpen_ = false;
            activeField_ = -1;
            return PanelAction();
        case K::SheetModePresets:
        case K::SheetModeOwn:
            presetMode_ = action.kind == K::SheetModePresets;
            presetTest_ = PresetTestResult::None;
            return PanelAction();
        case K::PresetPick:
            if (action.arg >= 0 && action.arg < kCommandPresetCount) preset_ = static_cast<CommandPreset>(action.arg);
            presetTest_ = PresetTestResult::None;
            return PanelAction();
        case K::OscDestAuto:
        case K::OscDestManual:
            oscAuto_ = action.kind == K::OscDestAuto;
            presetTest_ = PresetTestResult::None;
            return PanelAction();
        case K::AppsOpenList:
            appsListOpen_ = true;
            appsConfirmOpen_ = false;
            appsLaunchResult_ = frame_apps::LaunchResult::Started;
            return PanelAction();
        case K::AppsCloseList:
            appsListOpen_ = false;
            return PanelAction();
        case K::AppsListDrag:
            appsDragging_ = true;
            appsDragStartY_ = y;
            appsDragStartScroll_ = appsScroll_;
            return PanelAction();
        case K::AppInstall:
        case K::AppsOpenMenu:
            // まず確認（実行するコマンドを見せる）。開くのは確認の［Konsole で開く］
            appsConfirmOpen_ = true;
            appsConfirmKey_ = action.kind == K::AppInstall ? action.text : std::string();
            appsConfirmFromList_ = appsListOpen_;
            appsLaunchResult_ = frame_apps::LaunchResult::Started;
            return PanelAction();
        case K::AppsConfirmCancel:
            appsConfirmOpen_ = false;  // 一覧から開いたなら一覧に戻る（一覧は開いたまま）
            appsLaunchResult_ = frame_apps::LaunchResult::Started;
            return PanelAction();
        case K::AppsConfirmLaunch: return PanelAction(K::AppsConfirmLaunch, 0, 0, appsConfirmKey_);
        default: break;
    }
    return action;
}

const std::string& SettingsPanel::sheetFieldText(int field) const {
    static const std::string kEmpty;
    switch (field) {
        case kFieldName: return sheetName_;
        case kFieldCommand: return sheetCommand_;
        case kFieldIp: return oscIp_;
        default: return kEmpty;
    }
}

bool SettingsPanel::sheetFieldShown(int field) const {
    if (!sheetOpen_ || tab_ != PanelTab::Commands) return false;
    if (field == kFieldIp) return presetMode() && preset_ == CommandPreset::VrchatMute && !oscAuto_;
    return (field == kFieldName || field == kFieldCommand) && !presetMode();
}

bool SettingsPanel::presetCommand(Language language, std::string& name, std::vector<std::string>& argv) const {
    const UiText& t = uiText(language);
    if (preset_ == CommandPreset::SpeakerMute) {
        name = t.presetSpkName;
        argv = {"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"};
        return true;
    }
    if (!oscAuto_ && !isValidIpv4(oscIp_)) return false;
    name = t.presetVrcName;
    argv = makeSendOscArgv(oscAuto_, oscIp_, kVrchatOscPort, kVrchatVoiceAddress);
    return true;
}

bool SettingsPanel::setDetectedPeer(bool found, const std::string& ip) {
    const std::string shown = found ? ip : std::string();
    if (found == peerFound_ && shown == peerIp_) return false;
    peerFound_ = found;
    peerIp_ = shown;
    return true;
}

bool SettingsPanel::scroll(double dy) {
    if (dy == 0.0) return false;
    if (tab_ == PanelTab::App) {
        if (!appsListOpen_ || appsConfirmOpen_) return false;
        const double next = std::clamp(appsScroll_ - dy * kScrollPxPerUnit, 0.0, appsMaxScroll_);
        if (next == appsScroll_) return false;
        appsScroll_ = next;
        return true;
    }
    if (sheetOpen_) return false;
    if (tab_ == PanelTab::Assign && pickerSide_ == PickerSide::Commands) {
        // ページめくり: 1 ぶんたまるごとに 1 ページ（上へ倒すと前のページ）
        pickerScrollAccum_ += dy;
        if (std::fabs(pickerScrollAccum_) < 1.0) return false;
        const int step = pickerScrollAccum_ > 0 ? -1 : 1;
        pickerScrollAccum_ = 0.0;
        const int next = std::clamp(pickerPage_ + step, 0, pickerPageCount_ - 1);
        if (next == pickerPage_) return false;
        pickerPage_ = next;
        return true;
    }
    if (tab_ == PanelTab::Commands) {
        const double next = std::clamp(listScroll_ - dy * kScrollPxPerUnit, 0.0, listMaxScroll_);
        if (next == listScroll_) return false;
        listScroll_ = next;
        deleteArmedId_.clear();  // 動かしたら「消す」の確認は取り消す
        return true;
    }
    return false;
}

/**
 * 並び替えの途中の並びを、今のポインターの位置から決め直す（1 行ぶんの高さ＋すき間を動くごとに 1 つずれる）。
 * @param origOrder 動かし始めたときの並び
 * @param origIndex 動かしているものの元の位置
 * @param offsetPx 動かした量（px）
 * @param pitch 1 行ぶんの高さ＋すき間
 * @return 新しい並び
 */
static std::vector<std::string> reorderedIds(const std::vector<std::string>& origOrder, int origIndex, double offsetPx,
                                             double pitch) {
    std::vector<std::string> ids = origOrder;
    if (ids.empty()) return ids;
    const int last = static_cast<int>(ids.size()) - 1;
    const int target = std::clamp(origIndex + static_cast<int>(std::lround(offsetPx / pitch)), 0, last);
    const std::string id = ids[static_cast<size_t>(origIndex)];
    ids.erase(ids.begin() + origIndex);
    ids.insert(ids.begin() + target, id);
    return ids;
}

bool SettingsPanel::dragTick(double dt) {
    if (dragId_.empty()) return false;
    // 一覧の上下の端から 48px 以内なら、端に近いほど速く（最大 600px/秒）スクロールする
    constexpr double kEdge = 48;
    constexpr double kMaxSpeed = 600;
    double speed = 0;
    if (dragPointerY_ < listBoxTop_ + kEdge) speed = -kMaxSpeed * (listBoxTop_ + kEdge - dragPointerY_) / kEdge;
    if (dragPointerY_ > listBoxBottom_ - kEdge) speed = kMaxSpeed * (dragPointerY_ - (listBoxBottom_ - kEdge)) / kEdge;
    if (speed == 0) return false;
    const double next = std::clamp(listScroll_ + std::clamp(speed, -kMaxSpeed, kMaxSpeed) * dt, 0.0, listMaxScroll_);
    if (next == listScroll_) return false;
    listScroll_ = next;
    dragOrder_ = reorderedIds(dragOrigOrder_, dragOrigIndex_, dragPointerY_ - dragStartY_ + listScroll_ - dragStartScroll_,
                              kListRowH + kListRowGap);
    return true;
}

bool SettingsPanel::takeReorder(std::vector<std::string>& ids) {
    if (!reorderDone_) return false;
    reorderDone_ = false;
    ids = dragOrder_;
    return true;
}

void SettingsPanel::dragForPreview(const std::string& id, int rows) {
    dragId_ = id;
    dragOrigOrder_.clear();
    for (const CommandDef& command : lastCommands_) dragOrigOrder_.push_back(command.id);
    for (size_t i = 0; i < dragOrigOrder_.size(); ++i) {
        if (dragOrigOrder_[i] == id) dragOrigIndex_ = static_cast<int>(i);
    }
    dragOrder_ = reorderedIds(dragOrigOrder_, dragOrigIndex_, rows * (kListRowH + kListRowGap), kListRowH + kListRowGap);
}

bool SettingsPanel::caretTick(double now) {
    if (activeField_ < 0) return false;
    if (caretRestart_) {
        caretRestart_ = false;
        caretSince_ = now;
        const bool changed = !caretOn_;
        caretOn_ = true;
        return changed;
    }
    const bool on = static_cast<long long>((now - caretSince_) / kCaretBlinkSec) % 2 == 0;
    if (on == caretOn_) return false;
    caretOn_ = on;
    return true;
}

bool SettingsPanel::takeTapCursor(size_t& byteIndex) {
    if (!tapCursorPending_) return false;
    tapCursorPending_ = false;
    byteIndex = tapCursor_;
    return true;
}

void SettingsPanel::setSheetText(int field, const std::string& text, size_t cursor) {
    caretRestart_ = true;  // 文字が入ったら、カーソルは点灯から始め直す
    if (field == activeField_) sheetCursor_ = cursor == std::string::npos || cursor > text.size() ? text.size() : cursor;
    if (field == kFieldName) {
        sheetName_ = text;
    } else if (field == kFieldCommand) {
        sheetCommand_ = text;
        sheetTest_ = TestResult::None;  // 変えたら前の［試す］の結果は消す
    } else if (field == kFieldIp) {
        oscIp_ = text;
        presetTest_ = PresetTestResult::None;
    }
}

void SettingsPanel::setSheetTestResult(bool ran) {
    sheetTest_ = ran ? TestResult::Ran : TestResult::Failed;
}

void SettingsPanel::setRowTestResult(const std::string& id, bool ran) {
    testedRowId_ = id;
    testedRowRan_ = ran;
}

void SettingsPanel::openSheet(const CommandDef* command) {
    sheetOpen_ = true;
    activeField_ = -1;
    sheetId_ = command != nullptr ? command->id : std::string();
    sheetName_ = command != nullptr ? command->name : std::string();
    sheetCommand_ = command != nullptr ? joinCommandLine(command->argv) : std::string();
    sheetTest_ = TestResult::None;
    deleteArmedId_.clear();
    // 追加は「プリセットから」・VRChat のミュート・送り先は自動から始める
    presetMode_ = command == nullptr;
    preset_ = CommandPreset::VrchatMute;
    oscAuto_ = true;
    oscIp_.clear();
    presetTest_ = PresetTestResult::None;
}

void SettingsPanel::onCommandSaved(const std::string& /*id*/, const Config& /*config*/) {
    const bool added = sheetId_.empty();
    sheetOpen_ = false;
    activeField_ = -1;
    // 足したら、一覧の一番下（足したコマンド）までスクロールして見せる
    if (added) listScrollToEnd_ = true;
}

void SettingsPanel::armDeleteForPreview(const std::string& id) {
    deleteArmedId_ = id;
    deleteArmedUntil_ = 1e300;
}

void SettingsPanel::setAppsLaunchResult(frame_apps::LaunchResult result) {
    appsLaunchResult_ = result;
    if (result == frame_apps::LaunchResult::Started) appsConfirmOpen_ = false;  // 開けたら確認を閉じる（一覧からなら一覧に戻る）
}

void SettingsPanel::openAppsForPreview(bool list, const std::string& confirmKey, double scroll) {
    tab_ = PanelTab::App;
    appsListOpen_ = list;
    appsConfirmOpen_ = !confirmKey.empty();
    appsConfirmKey_ = confirmKey == "menu" ? std::string() : confirmKey;
    appsConfirmFromList_ = list;
    appsScroll_ = scroll;
}

const frame_apps::Entry* SettingsPanel::appsConfirmEntry(const PanelView& view) const {
    if (appsConfirmKey_.empty()) return nullptr;
    for (const frame_apps::Entry& entry : view.apps) {
        if (entry.app.key == appsConfirmKey_) return &entry;
    }
    return nullptr;
}

std::string SettingsPanel::appsConfirmCommand(const PanelView& view) const {
    const frame_apps::Entry* entry = appsConfirmEntry(view);
    return frame_apps::installerCommand(entry != nullptr ? entry->app.key : std::string());
}

void SettingsPanel::setPickerForPreview(PickerSide side, int page) {
    pickerSide_ = side;
    pickerPage_ = page;
    autoPicker_ = false;
}

bool SettingsPanel::pointerUp() {
    if (appsDragging_) {
        appsDragging_ = false;
        pressed_ = PanelAction();
        return true;
    }
    if (!dragId_.empty()) {
        // 離したら、その並びを呼び出し側に渡す（takeReorder）
        reorderDone_ = dragOrder_ != dragOrigOrder_;
        dragId_.clear();
        pressed_ = PanelAction();
        return true;
    }
    if (pressed_.kind == PanelAction::Kind::None) return false;
    pressed_ = PanelAction();
    return true;
}

bool SettingsPanel::pointerLeave() {
    appsDragging_ = false;
    if (!dragId_.empty()) {
        // パネルの外へ出たら、そこで離したことにする
        reorderDone_ = dragOrder_ != dragOrigOrder_;
        dragId_.clear();
    }
    const bool changed = hover_.kind != PanelAction::Kind::None || pressed_.kind != PanelAction::Kind::None;
    hover_ = PanelAction();
    pressed_ = PanelAction();
    return changed;
}

bool SettingsPanel::tick(double now) {
    bool changed = false;
    if (quitArmed_ && now > quitArmedUntil_) {
        quitArmed_ = false;
        changed = true;
    }
    if (resetArmed_ && now > resetArmedUntil_) {
        resetArmed_ = false;
        changed = true;
    }
    if (!deleteArmedId_.empty() && now > deleteArmedUntil_) {
        deleteArmedId_.clear();
        changed = true;
    }
    return changed;
}

void SettingsPanel::armQuitForPreview() {
    quitArmed_ = true;
    quitArmedUntil_ = 1e300;
}

void SettingsPanel::armResetForPreview() {
    resetArmed_ = true;
    resetArmedUntil_ = 1e300;
}

void SettingsPanel::armUpdateConfirmForPreview() {
    updateConfirmArmed_ = true;
}

double SettingsPanel::drawToggle(const Pen& pen, double x, double y, const PanelAction& left, const PanelAction& right,
                                 const std::string& leftLabel, const std::string& rightLabel, int selected,
                                 bool enabled, bool alignRight) {
    // 高さ 56・枠 2・内側 2・ボタンは高さ 48・左右 20・19px、間 4
    const double size = 19;
    const double wLeft = 40 + checkedLabelWidth(pen, leftLabel, size, selected == 0);
    const double wRight = 40 + checkedLabelWidth(pen, rightLabel, size, selected == 1);
    const double w = 4 + wLeft + 4 + wRight + 4;
    if (alignRight) x -= w;
    strokeRounded(pen, x, y, w, 56, 28, enabled ? kBorder : kBorderDisabled, 2);
    const PanelAction actions[2] = {left, right};
    const std::string labels[2] = {leftLabel, rightLabel};
    const double widths[2] = {wLeft, wRight};
    double bx = x + 4;
    for (int i = 0; i < 2; ++i) {
        const bool on = selected == i;
        const bool hot = enabled && isHot(actions[i]);
        if (on) {
            fillRounded(pen, bx, y + 4, widths[i], 48, 24, pressed_ == actions[i] ? kAccentPressed : kAccent,
                        enabled ? 1.0 : 0.4);
        } else if (hot) {
            fillRounded(pen, bx, y + 4, widths[i], 48, 24, kControlHover);
        }
        const Color fg = !enabled ? kTextDisabled : (on ? kOnAccent : kText);
        drawCheckedLabel(pen, bx + 20, centerBaseline(y + 4, 48, size), labels[i], size, fg, on);
        addButton(actions[i], bx, y, widths[i], 56, enabled);
        bx += widths[i] + 4;
    }
    return w;
}

void SettingsPanel::drawHeader(const Pen& pen, const UiText& t, const Config& /*config*/, const PanelView& view) {
    using K = PanelAction::Kind;
    // 題名
    const double titleW = pen.text(kPad, centerBaseline(kHeaderY, kHeaderH, 30), t.title, 30, kText, true);
    // 状態のピル（色だけで伝えない: ● と文字）
    // 従来の aux（コントローラーが無いので返している）は、待機中と同じ灰色で、文字だけ分ける
    const bool idle = view.daemon == PanelView::Daemon::Standby || view.daemon == PanelView::Daemon::HandedBack;
    const char* label = view.daemon == PanelView::Daemon::Running      ? t.statusRunning
                        : view.daemon == PanelView::Daemon::HandedBack ? t.statusHandedBack
                        : idle                                         ? t.statusStandby
                                                                       : t.statusStopped;
    const Color fill = view.daemon == PanelView::Daemon::Running ? kSuccessTint : idle ? kStoppedTint : kDangerTint;
    const Color fg = view.daemon == PanelView::Daemon::Running ? kSuccess : idle ? kTextLabel : kDanger;
    const double pillX = kPad + titleW + 16;
    const double pillW = 16 + 10 + 8 + pen.measure(label, 18, true) + 16;
    fillRounded(pen, pillX, kHeaderY + 10, pillW, 40, 20, fill);
    drawDisc(pen.cr, pillX + 16 + 5, kHeaderY + 30, 5, fg);
    pen.text(pillX + 16 + 10 + 8, centerBaseline(kHeaderY + 10, 40, 18), label, 18, fg, true);

    // タブ（右寄せ。枠 2・内側 4・ボタンの高さ 48・左右 20・19px・間 4）。状態のピルとぶつかるときは、
    // ボタンの左右の余白を 20 → 12 に詰め、それでも入らなければ文字を小さくする
    const char* names[kPanelTabCount] = {t.tabAssign, t.tabBrightness, t.tabPress, t.tabCommands, t.tabDisplay, t.tabApp};
    const double available = kW - kPad - (pillX + pillW + 16);
    double size = 19;
    double padX = 20;
    double widths[kPanelTabCount];
    double total = 0;
    for (int attempt = 0; attempt < 12; ++attempt) {
        total = 2 + 4 + 4 + 2;
        for (int i = 0; i < kPanelTabCount; ++i) {
            widths[i] = padX * 2 + checkedLabelWidth(pen, names[i], size, static_cast<int>(tab_) == i);
            total += widths[i] + (i > 0 ? 4 : 0);
        }
        if (total <= available) break;
        if (padX > 12) {
            padX -= 4;
        } else {
            size -= 1;
        }
    }
    const double navX = kW - kPad - total;
    strokeRounded(pen, navX, kHeaderY, total, kHeaderH, 32, kBorder, 2);
    double bx = navX + 6;
    const bool updateAvailable = view.update.state == frame_updater::UpdateState::Available;
    for (int i = 0; i < kPanelTabCount; ++i) {
        const PanelAction action {K::Tab, i, 0, ""};
        const bool on = static_cast<int>(tab_) == i;
        if (on) {
            fillRounded(pen, bx, kHeaderY + 6, widths[i], 48, 24, kAccent);
        } else if (isHot(action)) {
            fillRounded(pen, bx, kHeaderY + 6, widths[i], 48, 24, kControlHover);
        }
        drawCheckedLabel(pen, bx + padX, centerBaseline(kHeaderY + 6, 48, size), names[i], size, on ? kOnAccent : kText,
                         on);
        // 「アプリ」タブ: 新しい版があるときはピンクの点
        if (i == static_cast<int>(PanelTab::App) && updateAvailable && !on) {
            drawDisc(pen.cr, bx + widths[i] - 8 - 5, kHeaderY + 6 + 6 + 5, 5, kAccent);
        }
        addButton(action, bx, kHeaderY, widths[i], kHeaderH);
        bx += widths[i] + 4;
    }
}

void SettingsPanel::drawAssignTab(const Pen& pen, const UiText& t, const Config& config, const PanelView& view) {
    using K = PanelAction::Kind;
    cairo_t* cr = pen.cr;
    const Language lang = config.language;
    // カードは 3 列 × 3 段（aux の押し方 6 つ・ヘッドセットのタップ 2 つ・タップの説明）。下に割り当ての列
    const double gap = kAssignGap;
    const double cardW = (kContentW - gap * 2) / 3;
    const double cardH = (kContentBottom - kContentY - kPickerH - gap * 3) / 3;
    const int recentSlot = view.haveStatus && view.status.lastPress.at > 0 &&
                                   view.now - view.status.lastPress.at <= kRecentSec
                               ? view.status.lastPress.slot()
                               : -1;
    for (int slot = 0; slot < kSlotCount; ++slot) {
        const Action& action = config.slot(slot);
        const double x = kPad + (slot % 3) * (cardW + gap);
        const double y = kContentY + (slot / 3) * (cardH + gap);
        const bool selected = slot == selectedSlot_;
        const bool usable = assignmentUsable(action, config, view);
        const PanelAction pick {K::SelectSlot, slot, 0, ""};
        fillRounded(pen, x, y, cardW, cardH, 20, isHot(pick) && !selected ? kControl : kCard);
        if (selected) {
            strokeRounded(pen, x, y, cardW, cardH, 20, kAccent, 3);
        } else if (!usable) {
            strokeRounded(pen, x, y, cardW, cardH, 20, kDangerCardBorder, 3);
        }
        addButton(pick, x, y, cardW, cardH);

        const double left = x + 3 + 20;
        const double right = x + cardW - 3 - 20;
        // 上の行: 押し方の印・名前・さっき押した
        const double rowCy = y + 3 + 11 + 14;
        const double marksRight = drawSlotMarks(pen, slot, left, rowCy);
        double titleRight = right;
        if (slot == recentSlot) {
            char clock[16];
            const std::time_t at = static_cast<std::time_t>(view.status.lastPress.at);
            std::tm tm {};
            localtime_r(&at, &tm);
            std::strftime(clock, sizeof(clock), "%H:%M", &tm);
            const std::string badge = formatText(isTapSlot(slot) ? t.recentTapFormat : t.recentFormat, clock);
            const double bw = pen.measure(badge, 14, true) + 24;
            fillRounded(pen, right - bw, rowCy - 13, bw, 26, 13, kAccent);
            pen.text(right - bw + 12, centerBaseline(rowCy - 13, 26, 14), badge, 14, kOnAccent, true);
            titleRight = right - bw - 8;
        }
        // タップは「ヘッドセットを 2 回タップ」。さっき押したの印と並んで入らなければ短い名前（「2 回タップ」）にする
        const double titleX = marksRight + 8 + 4;
        std::string title = slotTitle(lang, slot);
        if (pen.measure(title, 17, true) > titleRight - titleX) title = slotLabel(lang, slot);
        pen.text(titleX, centerBaseline(rowCy - 15, 30, 17), ellipsize(pen, title, 17, true, titleRight - titleX), 17,
                 kTextLabel, true);
        // 下の行: アイコン・アクションの名前・今の状態
        const double box = 52;
        const double rowY = y + cardH - 3 - 13 - box;
        const bool muted = !(view.haveStatus && view.status.micMuted == 0);
        Color boxFill = kIconBox;
        Color boxFg = kText;
        if (!usable || (action.type == ActionType::MicMuteToggle && view.haveStatus && view.status.micMuted == 1)) {
            boxFill = kDangerTint;
            boxFg = kDanger;
        } else if (selected) {
            boxFill = kAccentTint;
            boxFg = kAccent;
        }
        fillRounded(pen, left, rowY, box, box, 16, boxFill);
        drawIcon(cr, iconForAction(action.type, muted), left + box / 2, rowY + box / 2, 26, boxFg);
        const double textX = left + box + 16;
        const double textW = right - textX;
        std::string name = actionLabel(lang, action.type);
        if (action.type == ActionType::Command) {
            const CommandDef* command = findCommand(config.commands, view.folderCommands, action.commandId);
            name = command != nullptr ? command->name : t.missingCommand;
        }
        // 名前（23px）と状態（14px）の 2 行を、アイコンの高さの真ん中にそろえる
        const double blockTop = rowY + (box - (23 * 1.45 + 2 + 14 * 1.45)) / 2;
        const double nameSize = fitSize(pen, name, 23, 18, textW, true);
        name = ellipsize(pen, name, nameSize, true, textW);
        pen.text(textX, lineBaseline(blockTop, 23) + (23 - nameSize) * 0.3, name, nameSize, kText, true);
        const std::string sub = ellipsize(pen, stateText(t, action, config, view), 14, !usable, textW);
        pen.text(textX, lineBaseline(blockTop + 23 * 1.45 + 2, 14), sub, 14, usable ? kTextMuted : kDanger, !usable);
    }
    // 最後の 1 枠: タップの説明（押せない。点線の枠）
    {
        const double x = kPad + 2 * (cardW + gap);
        const double y = kContentY + 2 * (cardH + gap);
        strokeDashed(pen, x, y, cardW, cardH, 20, kBorder, 2);
        const double innerX = x + 3 + 20;
        const double innerW = cardW - 2 * (3 + 20);
        const std::vector<std::string> lines = wrapText(pen, t.tapNote, 14, innerW, 3);
        const double blockH = 16 * 1.45 + 4 + lines.size() * 14 * 1.45;
        double top = y + (cardH - blockH) / 2;
        pen.text(innerX, lineBaseline(top, 16), t.tapNoteTitle, 16, kTextLabel, true);
        top += 16 * 1.45 + 4;
        for (const std::string& line : lines) {
            pen.text(innerX, lineBaseline(top, 14), line, 14, kTextMuted);
            top += 14 * 1.45;
        }
    }
    drawPicker(pen, t, config, view, kContentBottom - kPickerH);
}

void SettingsPanel::drawPicker(const Pen& pen, const UiText& t, const Config& config, const PanelView& view,
                               double secY) {
    using K = PanelAction::Kind;
    cairo_t* cr = pen.cr;
    const Language lang = config.language;

    // 下の区画: 選んだ押し方に割り当てる（余白 14 / 20・間 10。見出しの行は高さ 44、選択肢は高さ kPickerChipH の 2 段）
    const double secH = kContentBottom - secY;
    fillRounded(pen, kPad, secY, kContentW, secH, 20, kCard);
    const double innerX = kPad + 20;
    const double innerW = kContentW - 40;
    const double rowY = secY + 14;
    const double headH = 44;
    const double chipH = kPickerChipH;
    const Action& current = config.slot(selectedSlot_);
    const std::vector<CommandDef> commands = allCommands(config, view);
    // 側とページ: カードを選んだとき（とパネルを開いたとき）は、割り当ての種類の側（コマンドならそのページ）
    const int slotCount = static_cast<int>(commands.size()) + 1;  // 最後は「＋ コマンドを足す」
    pickerPageCount_ = pageCount(slotCount, kPickerPerPage);
    if (autoPicker_) {
        autoPicker_ = false;
        pickerSide_ = current.type == ActionType::Command ? PickerSide::Commands
                      : isMediaAction(current.type)       ? PickerSide::Media
                      : isEyeAction(current.type)         ? PickerSide::Eye
                                                          : PickerSide::Actions;
        pickerPage_ = 0;
        for (size_t i = 0; i < commands.size(); ++i) {
            if (pickerSide_ == PickerSide::Commands && commands[i].id == current.commandId) {
                pickerPage_ = static_cast<int>(i) / kPickerPerPage;
            }
        }
    }
    pickerPage_ = std::clamp(pickerPage_, 0, pickerPageCount_ - 1);

    // 見出しの行: 見出し・説明（左）、「1 / 2」と［アクション｜音楽｜目｜コマンド（件数）］（右）
    struct Segment {
        K kind;
        PickerSide side;
        std::string label;
        double w;
    };
    Segment segments[] = {
        {K::PickerActions, PickerSide::Actions, t.pickerActions, 0},
        {K::PickerMedia, PickerSide::Media, t.pickerMedia, 0},
        {K::PickerEye, PickerSide::Eye, t.pickerEye, 0},
        {K::PickerCommands, PickerSide::Commands, formatText(t.pickerCommandsFormat, std::to_string(commands.size())), 0},
    };
    double segW = 2 + 2 + 2 + 2 + 4 * 3;  // 枠の線と内側の余白（左右）＋ 区切りの間 3 つ
    for (Segment& segment : segments) {
        segment.w = 32 + checkedLabelWidth(pen, segment.label, 16, pickerSide_ == segment.side);
        segW += segment.w;
    }
    const double segX = innerX + innerW - segW;
    strokeRounded(pen, segX, rowY, segW, headH, headH / 2, kBorder, 2);
    double bx = segX + 4;
    for (const Segment& segment : segments) {
        const bool on = pickerSide_ == segment.side;
        const PanelAction action {segment.kind, 0, 0, ""};
        if (on) {
            fillRounded(pen, bx, rowY + 4, segment.w, headH - 8, (headH - 8) / 2, kAccent);
        } else if (isHot(action)) {
            fillRounded(pen, bx, rowY + 4, segment.w, headH - 8, (headH - 8) / 2, kControlHover);
        }
        drawCheckedLabel(pen, bx + 16, centerBaseline(rowY + 4, headH - 8, 16), segment.label, 16, on ? kOnAccent : kText, on);
        addButton(action, bx, rowY, segment.w, headH);
        bx += segment.w + 4;
    }
    double rightEdge = segX - 12;
    if (pickerSide_ == PickerSide::Commands) {
        const std::string pageText = std::to_string(pickerPage_ + 1) + " / " + std::to_string(pickerPageCount_);
        rightEdge -= 6;
        rightEdge -= pen.text(rightEdge, centerBaseline(rowY, headH, 15), pageText, 15, kTextMuted, true, true) + 12;
    }
    const std::string heading = formatText(t.assignHeadingFormat, slotLabel(lang, selectedSlot_));
    const double headW = pen.text(innerX, centerBaseline(rowY, headH, 20), heading, 20, kText, true);
    const double hintW = rightEdge - (innerX + headW + 12);
    if (hintW > 40) {
        pen.text(innerX + headW + 12, centerBaseline(rowY, headH, 20), ellipsize(pen, t.assignHint, 14, false, hintW), 14,
                 kTextMuted);
    }

    const double gridY = rowY + headH + 10;
    const double chipGap = 10;  // 横の間
    const double rowGap = 8;    // 縦の間
    const double chipW = (innerW - chipGap * 3) / 4;  // 音楽・目の側は 4 列の幅
    const double actionChipW = (innerW - chipGap * (kPickerActionCols - 1)) / kPickerActionCols;  // アクションの側は 5 列
    /**
     * アクションの選択肢を 1 つ描く（アクション・音楽・目の側）。
     * @param type 種類
     * @param cx 左
     * @param cy 上
     * @param chipW 幅
     * @param withIcon 名前の前にアイコンを描く（音楽・目の側）
     */
    const auto drawActionChip = [&](ActionType type, double cx, double cy, double chipW, bool withIcon) {
        const bool on = current.type == type;
        const bool usable = actionUsable(type, view);
        const PanelAction assign {K::Assign, static_cast<int>(type), selectedSlot_, ""};
        if (on) {
            fillRounded(pen, cx, cy, chipW, chipH, chipH / 2, pressed_ == assign ? kAccentPressed : kAccent);
        } else if (!usable) {
            strokeDashed(pen, cx, cy, chipW, chipH, chipH / 2, kBorder, 2);
            if (isHot(assign)) fillRounded(pen, cx + 2, cy + 2, chipW - 4, chipH - 4, chipH / 2 - 2, kControlHover);
        } else {
            fillRounded(pen, cx, cy, chipW, chipH, chipH / 2, isHot(assign) ? kControlHover : kControl);
            strokeRounded(pen, cx, cy, chipW, chipH, chipH / 2, isHot(assign) ? kAccent : kBorder, 2);
        }
        const std::string tag = unusableTag(t, type, view);
        const Color fg = on ? kOnAccent : (!usable ? kTextFaint : kText);
        const double tagW = tag.empty() ? 0 : pen.measure(tag, 12, true) + 8;
        const std::string label = actionLabel(lang, type);
        if (withIcon) {
            // コマンドの側の選択肢と同じ並び（チェック・アイコン・名前）
            double tx = cx + 16;
            if (on) {
                drawCheck(cr, tx + 17 * 0.4, cy + chipH / 2, 17 * 0.75, fg);
                tx += 17 * 0.95;
            }
            drawIcon(cr, iconForAction(type), tx + 10, cy + chipH / 2, 20, fg, 2.2);
            tx += 20 + 8;
            const double labelW = cx + chipW - 16 - tagW - tx;
            const double labelSize = fitSize(pen, label, 17, 12, labelW, true);
            pen.text(tx, centerBaseline(cy, chipH, labelSize), ellipsize(pen, label, labelSize, true, labelW), labelSize, fg,
                     true);
        } else if (!tag.empty()) {
            // 入っていないアプリ: 5 列の幅では名前と札が横に並ばないので、名前の下に札を置く（2 行を真ん中にそろえる）
            const double labelW = chipW - 32 - (on ? 17 : 0);
            const double labelSize = fitSize(pen, label, 15, 12, labelW, true);
            const double blockTop = cy + (chipH - (labelSize * 1.2 + 12 * 1.2)) / 2;
            const double labelBase = centerBaseline(blockTop, labelSize * 1.2, labelSize);
            const std::string shown = ellipsize(pen, label, labelSize, true, labelW);
            if (on) {
                drawCheckedLabel(pen, cx + 16, labelBase, shown, labelSize, fg, true);
            } else {
                pen.text(cx + 16, labelBase, shown, labelSize, fg, true);
            }
            pen.text(cx + 16 + (on ? labelSize * 0.95 : 0), centerBaseline(blockTop + labelSize * 1.2, 12 * 1.2, 12), tag, 12,
                     on ? kOnAccent : kDanger, true);
        } else {
            const double labelW = chipW - 32 - (on ? 17 : 0);
            const double labelSize = fitSize(pen, label, 17, 12, labelW, true);
            const std::string shown = ellipsize(pen, label, labelSize, true, labelW);
            // 選んでいないときも太字（700）
            if (on) {
                drawCheckedLabel(pen, cx + 16, centerBaseline(cy, chipH, labelSize), shown, labelSize, fg, true);
            } else {
                pen.text(cx + 16, centerBaseline(cy, chipH, labelSize), shown, labelSize, fg, true);
            }
        }
        if (!tag.empty() && withIcon) {
            pen.text(cx + chipW - 16, centerBaseline(cy, chipH, 12), tag, 12, on ? kOnAccent : kDanger, true, true);
        }
        addButton(assign, cx, cy, chipW, chipH);
    };
    if (pickerSide_ == PickerSide::Actions) {
        // アクション: 5 列 × 2 段のグリッド（9 つ。「なし」は最後。音楽・目・コマンドはそれぞれの側で選ぶ）
        int index = 0;
        for (const ActionSpec& spec : actionSpecs()) {
            if (spec.type == ActionType::Command || isMediaAction(spec.type) || isEyeAction(spec.type)) continue;
            drawActionChip(spec.type, innerX + (index % kPickerActionCols) * (actionChipW + chipGap),
                           gridY + (index / kPickerActionCols) * (chipH + rowGap), actionChipW, false);
            ++index;
        }
        return;
    }
    if (pickerSide_ == PickerSide::Media) {
        // 音楽: 1 段目に 3 つ（アクションの側と同じ幅）、2 段目に説明（何が相手か・PC の音は対象外）
        int index = 0;
        for (const ActionSpec& spec : actionSpecs()) {
            if (!isMediaAction(spec.type)) continue;
            drawActionChip(spec.type, innerX + index * (chipW + chipGap), gridY, chipW, true);
            ++index;
        }
        const std::vector<std::string> lines = wrapText(pen, t.mediaNote, 14, innerW - 32, 2);
        double top = gridY + chipH + rowGap + (chipH - lines.size() * 14 * 1.45) / 2;
        for (const std::string& line : lines) {
            pen.text(innerX + 16, lineBaseline(top, 14), line, 14, kTextMuted);
            top += 14 * 1.45;
        }
        return;
    }
    if (pickerSide_ == PickerSide::Eye) {
        // 目: 1 段目に 3 つ（音楽の側と同じ幅）と、使えないときのわけ（入っていない・更新が必要。赤）、2 段目に説明
        int index = 0;
        for (const ActionSpec& spec : actionSpecs()) {
            if (!isEyeAction(spec.type)) continue;
            drawActionChip(spec.type, innerX + index * (chipW + chipGap), gridY, chipW, true);
            ++index;
        }
        const EyePanelInfo& eye = view.eyePanel;
        std::string problem;
        if (eye.state == EyePanelState::Missing) {
            problem = t.eyeMissingNote;
        } else if (eye.state == EyePanelState::Outdated) {
            problem = t.eyeOutdatedNote;
            if (!eye.version.empty()) problem += formatText(t.eyeVersionNowFormat, eye.version);
        }
        if (!problem.empty()) {
            const double px = innerX + index * (chipW + chipGap) + 6;
            const std::vector<std::string> lines = wrapText(pen, problem, 14, innerX + innerW - px - 16, 2);
            double top = gridY + (chipH - lines.size() * 14 * 1.45) / 2;
            for (const std::string& line : lines) {
                pen.text(px, lineBaseline(top, 14), line, 14, kDanger, true);
                top += 14 * 1.45;
            }
        }
        const std::vector<std::string> lines = wrapText(pen, t.eyeNote, 14, innerW - 32, 2);
        double top = gridY + chipH + rowGap + (chipH - lines.size() * 14 * 1.45) / 2;
        for (const std::string& line : lines) {
            pen.text(innerX + 16, lineBaseline(top, 14), line, 14, kTextMuted);
            top += 14 * 1.45;
        }
        return;
    }

    // コマンド: 左右に［‹］［›］（幅 52）、真ん中に 4 列 × 2 行。最後のページの最後に「＋ コマンドを足す」
    const double navW = 52;
    const double gridH = chipH * 2 + rowGap;
    const bool canPrev = pickerPage_ > 0;
    const bool canNext = pickerPage_ < pickerPageCount_ - 1;
    const PanelAction prev {K::PickerPrev, 0, 0, ""};
    const PanelAction next {K::PickerNext, 0, 0, ""};
    drawNavButton(pen, innerX, gridY, navW, gridH, 16, "‹", 30, canPrev, isHot(prev));
    drawNavButton(pen, innerX + innerW - navW, gridY, navW, gridH, 16, "›", 30, canNext, isHot(next));
    addButton(prev, innerX, gridY, navW, gridH, canPrev);
    addButton(next, innerX + innerW - navW, gridY, navW, gridH, canNext);
    const double gx = innerX + navW + chipGap;
    const double gw = innerW - 2 * (navW + chipGap);
    const double commandChipW = (gw - chipGap * 3) / 4;
    const int first = pickerPage_ * kPickerPerPage;
    for (int k = 0; k < kPickerPerPage && first + k < slotCount; ++k) {
        const int index = first + k;
        const double cx = gx + (k % 4) * (commandChipW + chipGap);
        const double cy = gridY + (k / 4) * (chipH + rowGap);
        if (index == slotCount - 1) {
            // 「＋ コマンドを足す」（点線のピンク）
            const PanelAction add {K::AddCommand, 0, 0, ""};
            if (isHot(add)) fillRounded(pen, cx, cy, commandChipW, chipH, chipH / 2, kAccentTint);
            strokeDashed(pen, cx, cy, commandChipW, chipH, chipH / 2, kAccent, 2);
            const double size = fitSize(pen, t.cmdAdd, 17, 12, commandChipW - 24, true);
            textCentered(pen, cx + commandChipW / 2, centerBaseline(cy, chipH, size), t.cmdAdd, size, kAccent, true);
            addButton(add, cx, cy, commandChipW, chipH);
            continue;
        }
        const CommandDef& command = commands[static_cast<size_t>(index)];
        const PanelAction assign {K::Assign, static_cast<int>(ActionType::Command), selectedSlot_, command.id};
        const bool on = current.type == ActionType::Command && current.commandId == command.id;
        if (on) {
            fillRounded(pen, cx, cy, commandChipW, chipH, chipH / 2, pressed_ == assign ? kAccentPressed : kAccent);
        } else {
            fillRounded(pen, cx, cy, commandChipW, chipH, chipH / 2, isHot(assign) ? kControlHover : kControl);
            strokeRounded(pen, cx, cy, commandChipW, chipH, chipH / 2, isHot(assign) ? kAccent : kBorder, 2);
        }
        const Color fg = on ? kOnAccent : kText;
        double tx = cx + 16;
        if (on) {
            drawCheck(cr, tx + 17 * 0.4, cy + chipH / 2, 17 * 0.75, fg);
            tx += 17 * 0.95;
        }
        drawIcon(cr, Icon::Terminal, tx + 10, cy + chipH / 2, 20, fg, 2.2);
        tx += 20 + 8;
        const std::string label = ellipsize(pen, command.name, 17, true, cx + commandChipW - 16 - tx);
        pen.text(tx, centerBaseline(cy, chipH, 17), label, 17, fg, true);
        addButton(assign, cx, cy, commandChipW, chipH);
    }
}

void SettingsPanel::drawCommandsTab(const Pen& pen, const UiText& t, const Config& config) {
    using K = PanelAction::Kind;
    cairo_t* cr = pen.cr;
    const Language lang = config.language;
    const double secX = kPad;
    const double secY = kContentY;
    const double secW = kContentW;
    const double secH = kContentBottom - kContentY;
    fillRounded(pen, secX, secY, secW, secH, 20, kCard);
    const size_t buttonsBefore = buttons_.size();
    const double innerX = secX + 26;
    const double innerW = secW - 52;
    const double headY = secY + 22;

    // 見出しの行（高さ 56）: 見出しと説明（左）、「＋ コマンドを足す」（右）
    const double addW = pen.measure(t.cmdAdd, 18, true) + 48;
    const double addX = innerX + innerW - addW;
    const PanelAction add {K::AddCommand, 0, 0, ""};
    fillRounded(pen, addX, headY, addW, 56, 16, pressed_ == add ? kAccentPressed : kAccent);
    textCentered(pen, addX + addW / 2, centerBaseline(headY, 56, 18), t.cmdAdd, 18, kOnAccent, true);
    addButton(add, addX, headY, addW, 56);
    const double headTextW = addX - 14 - innerX;
    const double blockTop = headY + (56 - (22 * 1.45 + 2 + 14 * 1.45)) / 2;
    pen.text(innerX, lineBaseline(blockTop, 22), t.cmdHeading, 22, kText, true);
    pen.text(innerX, lineBaseline(blockTop + 22 * 1.45 + 2, 14), ellipsize(pen, t.cmdHint, 14, false, headTextW), 14,
             kTextMuted);

    // 下の説明（2 行まで、行の高さ 1.6）
    const std::vector<std::string> noteLines = wrapText(pen, t.cmdNote, 14, innerW, 2);
    const double noteTop = secY + secH - 22 - noteLines.size() * 14 * 1.6;
    double noteY = noteTop;
    for (const auto& line : noteLines) {
        pen.text(innerX, centerBaseline(noteY, 14 * 1.6, 14), line, 14, kTextMuted);
        noteY += 14 * 1.6;
    }

    // 一覧の箱（残りの高さいっぱい。右に 8px あけて、細いスクロールバー）
    const double boxTop = headY + 56 + 14;
    const double boxBottom = noteTop - 14;
    const double boxH = boxBottom - boxTop;
    listBoxTop_ = boxTop;
    listBoxBottom_ = boxBottom;
    // 並び: つまみで動かしている間はその並び
    std::vector<CommandDef> commands;
    if (!dragId_.empty()) {
        for (const std::string& id : dragOrder_) {
            for (const CommandDef& command : lastCommands_) {
                if (command.id == id) commands.push_back(command);
            }
        }
    } else {
        commands = lastCommands_;
    }
    const double pitch = kListRowH + kListRowGap;
    const double contentH = commands.empty() ? 120 : commands.size() * pitch - kListRowGap;
    listMaxScroll_ = std::max(0.0, contentH - boxH);
    if (listScrollToEnd_) {
        listScroll_ = listMaxScroll_;
        listScrollToEnd_ = false;
    }
    listScroll_ = std::clamp(listScroll_, 0.0, listMaxScroll_);
    const double rowW = innerW - 16;
    if (commands.empty()) {
        strokeDashed(pen, innerX, boxTop, rowW, 120, 16, kDivider, 2);
        textCentered(pen, innerX + rowW / 2, centerBaseline(boxTop, 120, 17), t.cmdEmpty, 17, kTextMuted, false);
    }
    /**
     * 一覧の箱の中に見えている部分だけボタンにする（スクロールで隠れた部分は押せない）。
     */
    const auto addListButton = [&](const PanelAction& action, double bx, double by, double bw, double bh) {
        const double top = std::max(by, boxTop);
        const double bottom = std::min(by + bh, boxBottom);
        if (bottom - top > 4) addButton(action, bx, top, bw, bottom - top);
    };
    cairo_save(cr);
    cairo_rectangle(cr, innerX - 12, boxTop, innerW + 24, boxH);
    cairo_clip(cr);
    double draggedY = 0;
    bool draggedVisible = false;
    for (size_t index = 0; index < commands.size(); ++index) {
        const CommandDef& command = commands[index];
        const double y = boxTop + index * pitch - listScroll_;
        if (y + kListRowH < boxTop || y > boxBottom) continue;
        const bool dragged = command.id == dragId_;
        if (dragged) {
            draggedY = y;  // 持ち上げた行は最後に描く（ほかの行の上に影を落とす）
            draggedVisible = true;
        }
    }
    /**
     * 1 行の中身を描く（つまみ・アイコン・名前と中身・割り当て先・ボタン）。
     */
    const auto drawRow = [&](const CommandDef& command, double y, bool dragged) {
        const double rowH = kListRowH;
        if (dragged) {
            // 持ち上げた見た目: 影・地 #262a33・ピンクの枠
            cairo_set_source_rgba(cr, 0, 0, 0, 0.30);
            pen.roundedRect(innerX - 4, y + 6, rowW + 8, rowH + 14, 20);
            cairo_fill(cr);
            cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
            pen.roundedRect(innerX, y + 4, rowW, rowH + 4, 16);
            cairo_fill(cr);
            fillRounded(pen, innerX, y, rowW, rowH, 16, kIconBox);
            strokeRounded(pen, innerX, y, rowW, rowH, 16, kAccent, 2);
        } else {
            fillRounded(pen, innerX, y, rowW, rowH, 16, kControl);
        }
        // つまみ（2 × 3 の点々。幅 40・高さ 60。動かしている間はピンク）
        const double gripX = innerX + 2 + 6;
        const double gripY = y + (rowH - 60) / 2;
        const PanelAction grip {K::Grip, 0, 0, command.id};
        const Color gripColor = dragged ? kAccent : (isHot(grip) ? kTextMuted : kArrow);
        for (int gy = 0; gy < 3; ++gy) {
            for (int gx = 0; gx < 2; ++gx) {
                drawDisc(cr, gripX + 9 + 7 + gx * 8, gripY + 16 + 7 + gy * 7, 2.2, gripColor);
            }
        }
        if (dragId_.empty()) addListButton(grip, gripX, gripY, 40, 60);
        // 右から: ［消す］［編集］［試す］（フォルダのものは［試す］だけ）
        double right = innerX + rowW - 2 - 14;
        const double by = y + (rowH - 52) / 2;
        const auto button = [&](const PanelAction& action, const std::string& label, bool danger) {
            const double w = pen.measure(label, 17, true) + 36;
            right -= w;
            const bool hot = dragId_.empty() && isHot(action);
            fillRounded(pen, right, by, w, 52, 16, hot ? kControlHover : (danger ? kDangerFill : kCard));
            strokeRounded(pen, right, by, w, 52, 16, danger ? kDangerBorder : kBorder, 2);
            textCentered(pen, right + w / 2, centerBaseline(by, 52, 17), label, 17, kText, true);
            if (dragId_.empty()) addListButton(action, right, by, w, 52);
            right -= 12;
        };
        if (!command.fromFolder) {
            const bool armed = deleteArmedId_ == command.id;
            button({K::CommandDelete, 0, 0, command.id}, armed ? t.cmdDeleteConfirm : t.cmdDelete, true);
            button({K::CommandEdit, 0, 0, command.id}, t.cmdEdit, false);
        }
        button({K::CommandTest, 0, 0, command.id}, t.cmdTest, false);
        // 割り当て先のチップ（幅 230 の中に右寄せ。入らなければ折り返す）
        const double usesRight = right;
        const double usesLeft = usesRight - 230;
        struct Chip {
            std::string label;
            int kind;  // 0 = ピンク、1 = 緑、2 = 赤、3 = 灰色の文字、4 = 枠だけ（フォルダ）
            double w;
        };
        std::vector<Chip> chips;
        for (int slot = 0; slot < kSlotCount; ++slot) {
            const Action& action = config.slot(slot);
            if (action.type == ActionType::Command && action.commandId == command.id) chips.push_back({slotLabel(lang, slot), 0, 0});
        }
        if (testedRowId_ == command.id) {
            chips.push_back({testedRowRan_ ? t.cmdTested : t.cmdTestFailed, testedRowRan_ ? 1 : 2, 0});
        } else if (chips.empty()) {
            chips.push_back({t.cmdUnused, 3, 0});
        }
        if (command.fromFolder) chips.push_back({t.cmdFolder, 4, 0});
        std::vector<std::vector<Chip>> lines(1);
        double lineW = 0;
        for (Chip chip : chips) {
            chip.w = chip.kind == 3 ? pen.measure(chip.label, 14, false) : pen.measure(chip.label, 14, true) + 24;
            if (lineW > 0 && lineW + 6 + chip.w > usesRight - usesLeft) {
                lines.emplace_back();
                lineW = 0;
            }
            lineW += (lineW > 0 ? 6 : 0) + chip.w;
            lines.back().push_back(chip);
        }
        const double chipsH = lines.size() * 28 + (lines.size() - 1) * 6;
        double cyChip = y + (rowH - chipsH) / 2;
        for (const auto& line : lines) {
            double cx = usesRight;
            for (size_t i = line.size(); i-- > 0;) {
                const Chip& chip = line[i];
                cx -= chip.w;
                switch (chip.kind) {
                    case 0:
                        fillRounded(pen, cx, cyChip, chip.w, 28, 14, kAccentTint);
                        pen.text(cx + 12, centerBaseline(cyChip, 28, 14), chip.label, 14, kAccent, true);
                        break;
                    case 1:
                        fillRounded(pen, cx, cyChip, chip.w, 28, 14, kSuccessTint);
                        pen.text(cx + 12, centerBaseline(cyChip, 28, 14), chip.label, 14, kSuccess, true);
                        break;
                    case 2:
                        fillRounded(pen, cx, cyChip, chip.w, 28, 14, kDangerTint);
                        pen.text(cx + 12, centerBaseline(cyChip, 28, 14), chip.label, 14, kDanger, true);
                        break;
                    case 4:
                        strokeRounded(pen, cx, cyChip, chip.w, 28, 14, kBorder, 1.5);
                        pen.text(cx + 12, centerBaseline(cyChip, 28, 14), chip.label, 14, kTextMuted, true);
                        break;
                    default:
                        pen.text(cx, centerBaseline(cyChip, 28, 14), chip.label, 14, kArrow, false);
                        break;
                }
                cx -= 6;
            }
            cyChip += 28 + 6;
        }
        // 左: アイコン・名前・中身（行の左の余白 6・間 12）
        const double iconX = gripX + 40 + 12;
        fillRounded(pen, iconX, y + (rowH - 52) / 2, 52, 52, 16, dragged ? kControl : kIconBox);
        drawIcon(cr, Icon::Terminal, iconX + 26, y + rowH / 2, 26, kText);
        const double textLeft = iconX + 52 + 12;
        const double textW = usesLeft - 12 - textLeft;
        const double textTop = y + (rowH - (19 * 1.45 + 4 + 14 * 1.45)) / 2;
        pen.text(textLeft, lineBaseline(textTop, 19), ellipsize(pen, command.name, 19, true, textW), 19, kText, true);
        std::string line = joinCommandLine(command.argv);
        if (pen.measureMono(line, 14) > textW) {
            while (!line.empty() && pen.measureMono(line + "…", 14) > textW) {
                size_t cut = line.size() - 1;
                while (cut > 0 && (static_cast<unsigned char>(line[cut]) & 0xC0) == 0x80) --cut;
                line.erase(cut);
            }
            line += "…";
        }
        pen.textMono(textLeft, lineBaseline(textTop + 19 * 1.45 + 4, 14), line, 14, kTextMuted);
    };
    for (size_t index = 0; index < commands.size(); ++index) {
        const double y = boxTop + index * pitch - listScroll_;
        if (y + kListRowH < boxTop || y > boxBottom || commands[index].id == dragId_) continue;
        drawRow(commands[index], y, false);
    }
    if (draggedVisible) {
        for (const CommandDef& command : commands) {
            if (command.id == dragId_) drawRow(command, draggedY, true);
        }
    }
    cairo_restore(cr);

    // スクロールバー（溝 #14171d・つまみ #4a505c・幅 8）。入りきらないときだけ
    if (listMaxScroll_ > 0) {
        const double barX = innerX + innerW - 8;
        fillRounded(pen, barX, boxTop, 8, boxH, 4, kDiagram);
        const double thumbH = std::max(40.0, boxH * boxH / contentH);
        const double thumbY = boxTop + (boxH - thumbH) * (listScroll_ / listMaxScroll_);
        fillRounded(pen, barX, thumbY, 8, thumbH, 4, kBorder);
    }

    if (sheetOpen_) {
        // シートの下のボタンは押せないようにする（見出しのタブは残す）
        buttons_.resize(buttonsBefore);
        drawSheet(pen, t, lang, secX, secY, secW, secH);
    }
}

void SettingsPanel::drawTextField(const Pen& pen, int field, double x, double fy, double w, const std::string& placeholder) {
    using K = PanelAction::Kind;
    const PanelAction action {field == kFieldName ? K::SheetName : (field == kFieldCommand ? K::SheetCommand : K::SheetIp), 0, 0, ""};
    const bool active = activeField_ == field;
    fieldTextX_[field] = x + 16;
    const std::string& value = sheetFieldText(field);
    bool mono = false;
    const double size = fieldFont(field, mono);
    const double maxW = w - 32;
    if (active) {
        // 入力中の欄: 外側にうっすらピンクの光（4px・0.25）、枠は 2px のピンク
        fillRounded(pen, x - 4, fy - 4, w + 8, 56 + 8, 18, kAccent, 0.25);
        fillRounded(pen, x, fy, w, 56, 14, kDiagram);
        strokeRounded(pen, x, fy, w, 56, 14, kAccent, 2);
        // 文字（幅を超えたら、カーソルが見えるよう末尾側を見せる＝左側を切る）とカーソル（ピンクの縦棒、幅 2）
        const double textW = fieldTextWidth(pen, field, value);
        const double caretX = fieldTextWidth(pen, field, value.substr(0, std::min(sheetCursor_, value.size())));
        // カーソルが端に来たら、見える範囲に横にずらす（文字が欄より短ければずらさない）
        const double caretRoom = 6;
        const double viewW = maxW - caretRoom;
        if (caretX - fieldScroll_ > viewW) fieldScroll_ = caretX - viewW;
        if (caretX - fieldScroll_ < 0) fieldScroll_ = caretX;
        fieldScroll_ = std::clamp(fieldScroll_, 0.0, std::max(0.0, textW - viewW));
        cairo_save(pen.cr);
        cairo_rectangle(pen.cr, x + 16, fy + 4, maxW, 48);
        cairo_clip(pen.cr);
        const double tx = x + 16 - fieldScroll_;
        const double baseline = centerBaseline(fy, 56, size);
        if (!value.empty()) {
            if (mono) {
                pen.textMono(tx, baseline, value, size, kText);
            } else {
                pen.text(tx, baseline, value, size, kText, false);
            }
        }
        if (caretOn_) {
            pen.color(kAccent);
            cairo_rectangle(pen.cr, tx + caretX + (sheetCursor_ >= value.size() ? 1 : -1), fy + (56 - size * 1.2) / 2, 2,
                            size * 1.2);
            cairo_fill(pen.cr);
        }
        cairo_restore(pen.cr);
    } else {
        fillRounded(pen, x, fy, w, 56, 14, kDiagram);
        strokeRounded(pen, x, fy, w, 56, 14, isHot(action) ? kAccent : kBorder, 2);
        // 入力中でない欄: 文字（入らなければ後ろを「…」）か、空なら例の薄い文字
        const Color color = value.empty() ? kArrow : kText;
        if (mono) {
            pen.textMono(x + 16, centerBaseline(fy, 56, size),
                         ellipsizeMono(pen, value.empty() ? placeholder : value, size, maxW), size, color);
        } else {
            const std::string shown = value.empty() ? placeholder : ellipsize(pen, value, size, false, maxW);
            pen.text(x + 16, centerBaseline(fy, 56, size), shown, size, color, false);
        }
    }
    addButton(action, x, fy, w, 56);
}

double SettingsPanel::drawSheetFields(const Pen& pen, const UiText& t, double ix, double y, double innerW) {
    // 欄 2 つ（押すと SteamVR のキーボード）と説明
    for (int field = kFieldName; field <= kFieldCommand; ++field) {
        pen.text(ix, lineBaseline(y, 16), field == kFieldName ? t.fieldName : t.fieldCommand, 16, kText, true);
        const double fy = y + 16 * 1.45 + 6;
        drawTextField(pen, field, ix, fy, innerW, field == kFieldName ? t.namePlaceholder : t.commandPlaceholder);
        y = fy + 56 + 14;
    }
    for (const auto& line : wrapText(pen, t.sheetHint, 14, innerW, 3)) {
        pen.text(ix, centerBaseline(y, 14 * 1.6, 14), line, 14, kTextMuted);
        y += 14 * 1.6;
    }
    return y;
}

void SettingsPanel::drawSheetButtons(const Pen& pen, const UiText& t, double ix, double y, double innerW) {
    using K = PanelAction::Kind;
    // ボタンの行: ［試す］結果（左）、［やめる］［保存する］（右）
    std::vector<std::string> argv;
    std::string splitError;
    const bool splitOk = splitCommandLine(sheetCommand_, argv, splitError);
    const bool nameOk = sheetName_.find_first_not_of(" \t\r\n") != std::string::npos;
    const bool canSave = nameOk && splitOk && !argv.empty();
    const auto plainButton = [&](double bx, const PanelAction& action, const char* label, bool enabled) {
        const double bw = pen.measure(label, 18, true) + 44;
        if (enabled) fillRounded(pen, bx, y, bw, 56, 16, isHot(action) ? kControlHover : kControl);
        strokeRounded(pen, bx, y, bw, 56, 16, enabled ? kBorder : kBorderDisabled, 2);
        textCentered(pen, bx + bw / 2, centerBaseline(y, 56, 18), label, 18, enabled ? kText : kTextDisabled, true);
        addButton(action, bx, y, bw, 56, enabled);
        return bw;
    };
    const PanelAction test {K::SheetTest, 0, 0, ""};
    const double testW = plainButton(ix, test, t.cmdTest, splitOk && !argv.empty());
    std::string result;
    Color resultColor = kSuccess;
    if (!splitOk) {
        result = t.sheetQuoteError;
        resultColor = kDanger;
    } else if (sheetTest_ == TestResult::Ran) {
        result = t.cmdTested;
    } else if (sheetTest_ == TestResult::Failed) {
        result = t.cmdTestFailed;
        resultColor = kDanger;
    }
    if (!result.empty()) pen.text(ix + testW + 12, centerBaseline(y, 56, 15), result, 15, resultColor, true);
    // 保存する（保存できないときは押せない色）
    const PanelAction save {K::SheetSave, 0, 0, ""};
    const double saveW = pen.measure(t.sheetSave, 18, true) + 52;
    const double saveX = ix + innerW - saveW;
    if (canSave) {
        fillRounded(pen, saveX, y, saveW, 56, 16, pressed_ == save ? kAccentPressed : kAccent);
    } else {
        strokeRounded(pen, saveX, y, saveW, 56, 16, kBorderDisabled, 2);
    }
    textCentered(pen, saveX + saveW / 2, centerBaseline(y, 56, 18), t.sheetSave, 18, canSave ? kOnAccent : kTextDisabled, true);
    addButton(save, saveX, y, saveW, 56, canSave);
    const PanelAction cancel {K::SheetCancel, 0, 0, ""};
    const double cancelW = pen.measure(t.sheetCancel, 18, true) + 44;
    plainButton(saveX - 12 - cancelW, cancel, t.sheetCancel, true);
}

void SettingsPanel::drawPresetBody(const Pen& pen, const UiText& t, Language lang, double ix, double y, double innerW,
                                   double h) {
    using K = PanelAction::Kind;
    cairo_t* cr = pen.cr;
    // 左: プリセットの一覧（幅 340、カードの高さ 96、間 10）と、下に 1 行の補足
    const double listW = 340;
    struct PresetDef {
        CommandPreset preset;
        Icon icon;
        const char* name;
        const char* desc;
    };
    const PresetDef defs[kCommandPresetCount] = {
        {CommandPreset::VrchatMute, Icon::Monitor, t.presetVrcName, t.presetVrcDesc},
        {CommandPreset::SpeakerMute, Icon::SpeakerOff, t.presetSpkName, t.presetSpkDesc},
    };
    double cy = y;
    for (const PresetDef& d : defs) {
        const bool on = d.preset == preset_;
        const PanelAction pick {K::PresetPick, static_cast<int>(d.preset), 0, ""};
        const double cardH = 96;
        fillRounded(pen, ix, cy, listW, cardH, 16, isHot(pick) && !on ? kControlHover : kControl);
        if (on) strokeRounded(pen, ix + 1.5, cy + 1.5, listW - 3, cardH - 3, 15, kAccent, 3);
        const double iconX = ix + 3 + 14;
        fillRounded(pen, iconX, cy + (cardH - 48) / 2, 48, 48, 14, on ? kAccentTint : kIconBox);
        drawIcon(cr, d.icon, iconX + 24, cy + cardH / 2, 26, on ? kAccent : kText);
        const double textX = iconX + 48 + 14;
        const double textW = ix + listW - 16 - textX;
        const std::vector<std::string> descLines = wrapText(pen, d.desc, 14, textW, 2);
        const double blockH = 18 * 1.45 + 2 + descLines.size() * 14 * 1.5;
        double ty = cy + (cardH - blockH) / 2;
        pen.text(textX, lineBaseline(ty, 18), ellipsize(pen, d.name, 18, true, textW), 18, kText, true);
        ty += 18 * 1.45 + 2;
        for (const auto& line : descLines) {
            pen.text(textX, centerBaseline(ty, 14 * 1.5, 14), line, 14, kTextMuted);
            ty += 14 * 1.5;
        }
        addButton(pick, ix, cy, listW, cardH);
        cy += cardH + 10;
    }
    const std::vector<std::string> noteLines = wrapText(pen, t.presetNote, 13, listW, 3);
    double ny = y + h - noteLines.size() * 13 * 1.6;
    for (const auto& line : noteLines) {
        pen.text(ix, centerBaseline(ny, 13 * 1.6, 13), line, 13, kTextFaint);
        ny += 13 * 1.6;
    }

    // 右: 選んだプリセットの中身（地 #14171d・余白 16 / 20）
    const double bx = ix + listW + 16;
    const double bw = innerW - listW - 16;
    fillRounded(pen, bx, y, bw, h, 16, kDiagram);
    const double px = bx + 20;
    const double pw = bw - 40;
    const double top = y + 16;
    const double bottom = y + h - 16;
    /**
     * 下の「実行するコマンド」の箱（高さ 38）。
     */
    const auto drawCommandBox = [&](double boxY, const std::vector<std::string>& argv) {
        fillRounded(pen, px, boxY, pw, 38, 12, kCard);
        const double labelW = pen.text(px + 14, centerBaseline(boxY, 38, 13), t.fieldCommand, 13, kTextMuted);
        const double cmdX = px + 14 + labelW + 10;
        pen.textMono(cmdX, centerBaseline(boxY, 38, 14), ellipsizeMono(pen, joinCommandLine(argv), 14, px + pw - 14 - cmdX),
                     14, kTextLabel);
    };
    std::string name;
    std::vector<std::string> argv;
    if (preset_ == CommandPreset::SpeakerMute) {
        presetCommand(lang, name, argv);
        pen.text(px, lineBaseline(top, 18), t.spkDetailTitle, 18, kText, true);
        double ty = top + 18 * 1.45 + 10;
        for (const auto& line : wrapText(pen, t.spkDetail, 15, pw, 3)) {
            pen.text(px, centerBaseline(ty, 15 * 1.6, 15), line, 15, kTextLabel);
            ty += 15 * 1.6;
        }
        drawCommandBox(bottom - 38, argv);
        return;
    }

    // VRChat のミュート: 送り先の PC（自動・手で入れる）
    double ry = top;
    const PanelAction autoAction {K::OscDestAuto, 0, 0, ""};
    const PanelAction manualAction {K::OscDestManual, 0, 0, ""};
    const double toggleW = drawToggle(pen, px + pw, ry, autoAction, manualAction, t.oscDestAuto, t.oscDestManual,
                                      oscAuto_ ? 0 : 1, true, true);
    const double headW = pw - toggleW - 14;
    const double headTop = ry + (56 - (18 * 1.45 + 13 * 1.45)) / 2;
    pen.text(px, lineBaseline(headTop, 18), ellipsize(pen, t.oscDestTitle, 18, true, headW), 18, kText, true);
    const double hintSize = fitSize(pen, t.oscDestHint, 13, 11, headW, false);
    pen.text(px, lineBaseline(headTop + 18 * 1.45, 13), ellipsize(pen, t.oscDestHint, hintSize, false, headW), hintSize,
             kTextMuted);
    ry += 56 + 12;
    if (oscAuto_) {
        // 見つかった PC（緑）か、見つからない（赤）。高さ 60
        fillRounded(pen, px, ry, pw, 60, 14, peerFound_ ? kSuccessTint : kDangerFill);
        drawDisc(cr, px + 16 + 6, ry + 30, 6, peerFound_ ? kSuccess : kDanger);
        const double tx = px + 16 + 12 + 14;
        const double tw = px + pw - 16 - tx;
        const std::string title = peerFound_ ? formatText(t.oscFoundFormat, peerIp_) : std::string(t.oscNotFound);
        const char* sub = peerFound_ ? t.oscFoundHint : t.oscNotFoundHint;
        const double subSize = fitSize(pen, sub, 13, 11, tw, false);
        const double blockTop = ry + (60 - (17 * 1.45 + subSize * 1.45)) / 2;
        pen.text(tx, lineBaseline(blockTop, 17), ellipsize(pen, title, 17, true, tw), 17, kText, true);
        pen.text(tx, lineBaseline(blockTop + 17 * 1.45, subSize), ellipsize(pen, sub, subSize, false, tw), subSize, kTextMuted);
    } else {
        // IP アドレスの欄（押すと SteamVR のキーボード）
        const double labelW = pen.text(px, centerBaseline(ry + 2, 56, 16), t.oscIpLabel, 16, kText, true);
        const double fx = px + labelW + 12;
        drawTextField(pen, kFieldIp, fx, ry + 2, px + pw - fx, t.oscIpPlaceholder);
    }
    ry += 60 + 12;
    // 準備（PC の VRChat で 1 回だけ）
    pen.text(px, lineBaseline(ry, 16), t.oscSetupTitle, 16, kText, true);
    ry += 16 * 1.45 + 6;
    const char* steps[2] = {t.oscSetup1, t.oscSetup2};
    for (int i = 0; i < 2; ++i) {
        drawDisc(cr, px + 15, ry + 18, 15, kIconBox);
        textCentered(pen, px + 15, centerBaseline(ry + 3, 30, 15), std::to_string(i + 1), 15, kText, true);
        const double sx = px + 30 + 12;
        const double sw = px + pw - sx;
        const double size = fitSize(pen, steps[i], 15, 12, sw, false);
        pen.text(sx, centerBaseline(ry, 36, size), ellipsize(pen, steps[i], size, false, sw), size, kText);
        ry += 36 + 6;
    }
    // 下から: ファイアウォールの一言（2 行まで）と、実行するコマンド
    const std::vector<std::string> fwLines = wrapText(pen, t.oscFirewall, 13, pw, 2);
    double fy = bottom - fwLines.size() * 13 * 1.6;
    const double cmdY = fy - 8 - 38;
    for (const auto& line : fwLines) {
        pen.text(px, centerBaseline(fy, 13 * 1.6, 13), line, 13, kTextFaint);
        fy += 13 * 1.6;
    }
    if (!presetCommand(lang, name, argv)) argv = makeSendOscArgv(false, "[IP]", kVrchatOscPort, kVrchatVoiceAddress);
    drawCommandBox(cmdY, argv);
}

void SettingsPanel::drawPresetButtons(const Pen& pen, const UiText& t, Language lang, double ix, double y, double innerW) {
    using K = PanelAction::Kind;
    std::string name;
    std::vector<std::string> argv;
    const bool ready = presetCommand(lang, name, argv);
    const auto plainButton = [&](double bx, const PanelAction& action, const char* label, bool enabled) {
        const double bw = pen.measure(label, 18, true) + 44;
        if (enabled) fillRounded(pen, bx, y, bw, 56, 16, isHot(action) ? kControlHover : kControl);
        strokeRounded(pen, bx, y, bw, 56, 16, enabled ? kBorder : kBorderDisabled, 2);
        textCentered(pen, bx + bw / 2, centerBaseline(y, 56, 18), label, 18, enabled ? kText : kTextDisabled, true);
        addButton(action, bx, y, bw, 56, enabled);
        return bw;
    };
    const double testW = plainButton(ix, {K::PresetTest, 0, 0, ""}, t.cmdTest, ready);
    // 追加する（追加できないときは押せない色）・やめる
    const PanelAction add {K::PresetAdd, 0, 0, ""};
    const double addW = pen.measure(t.presetAdd, 18, true) + 52;
    const double addX = ix + innerW - addW;
    if (ready) {
        fillRounded(pen, addX, y, addW, 56, 16, pressed_ == add ? kAccentPressed : kAccent);
    } else {
        strokeRounded(pen, addX, y, addW, 56, 16, kBorderDisabled, 2);
    }
    textCentered(pen, addX + addW / 2, centerBaseline(y, 56, 18), t.presetAdd, 18, ready ? kOnAccent : kTextDisabled, true);
    addButton(add, addX, y, addW, 56, ready);
    const double cancelW = pen.measure(t.sheetCancel, 18, true) + 44;
    const double cancelX = addX - 12 - cancelW;
    plainButton(cancelX, {K::SheetCancel, 0, 0, ""}, t.sheetCancel, true);
    // ［試す］の結果（手で入れた IP の形が違うときは、そのことを出す）
    std::string result;
    Color color = kSuccess;
    switch (presetTest_) {
        case PresetTestResult::Ran: result = t.cmdTested; break;
        case PresetTestResult::Sent: result = t.oscTested; break;
        case PresetTestResult::NoPc:
            result = t.oscTestNoPc;
            color = kDanger;
            break;
        case PresetTestResult::Failed:
            result = preset_ == CommandPreset::VrchatMute ? t.oscTestFailed : t.cmdTestFailed;
            color = kDanger;
            break;
        case PresetTestResult::None:
            if (preset_ == CommandPreset::VrchatMute && !oscAuto_ && !oscIp_.empty() && !ready) {
                result = t.oscIpInvalid;
                color = kDanger;
            }
            break;
    }
    if (!result.empty()) {
        const double rx = ix + testW + 12;
        const double rw = cancelX - 12 - rx;
        const double size = fitSize(pen, result, 15, 12, rw, true);
        pen.text(rx, centerBaseline(y, 56, size), ellipsize(pen, result, size, true, rw), size, color, true);
    }
}

void SettingsPanel::drawSheet(const Pen& pen, const UiText& t, Language lang, double secX, double secY, double secW,
                              double secH) {
    using K = PanelAction::Kind;
    // 区画を暗くする
    fillRounded(pen, secX, secY, secW, secH, 20, kBg, 0.85);
    if (!sheetId_.empty()) {
        // 編集: 真ん中に幅 780 のシート（名前とコマンドの欄）
        const double w = 780;
        const double innerW = w - 52;
        const std::vector<std::string> hintLines = wrapText(pen, t.sheetHint, 14, innerW, 3);
        const double fieldH = 16 * 1.45 + 6 + 56;
        const double h = 24 + 22 * 1.45 + 14 + fieldH + 14 + fieldH + 14 + hintLines.size() * 14 * 1.6 + 14 + 4 + 56 + 24;
        const double x = secX + (secW - w) / 2;
        double y = secY + (secH - h) / 2;
        fillRounded(pen, x, y, w, h, 20, kCard);
        strokeRounded(pen, x, y, w, h, 20, kBorder, 2);
        const double ix = x + 26;
        y += 24;
        pen.text(ix, lineBaseline(y, 22), t.sheetEditTitle, 22, kText, true);
        y += 22 * 1.45 + 14;
        y = drawSheetFields(pen, t, ix, y, innerW);
        drawSheetButtons(pen, t, ix, y + 14 + 4, innerW);
        return;
    }
    // 追加: 区画いっぱいのシート。上に「プリセットから / 自分で書く」の切り替え、下にボタンの行
    const double w = std::min(1088.0, secW - 32);
    const double h = secH - 16;
    const double x = secX + (secW - w) / 2;
    const double y = secY + (secH - h) / 2;
    fillRounded(pen, x, y, w, h, 20, kCard);
    strokeRounded(pen, x, y, w, h, 20, kBorder, 2);
    const double ix = x + 24;
    const double innerW = w - 48;
    const double headY = y + 20;
    pen.text(ix, centerBaseline(headY, 56, 22), t.sheetAddTitle, 22, kText, true);
    drawToggle(pen, ix + innerW, headY, {K::SheetModePresets, 0, 0, ""}, {K::SheetModeOwn, 0, 0, ""}, t.sheetModePresets,
               t.sheetModeOwn, presetMode_ ? 0 : 1, true, true);
    const double bodyTop = headY + 56 + 16;
    const double buttonsY = y + h - 20 - 56;
    const double bodyBottom = buttonsY - 16;
    if (presetMode_) {
        drawPresetBody(pen, t, lang, ix, bodyTop, innerW, bodyBottom - bodyTop);
        drawPresetButtons(pen, t, lang, ix, buttonsY, innerW);
    } else {
        drawSheetFields(pen, t, ix, bodyTop, innerW);
        drawSheetButtons(pen, t, ix, buttonsY, innerW);
    }
}

void SettingsPanel::drawBrightnessTab(const Pen& pen, const UiText& t, const Config& config, const PanelView& view) {
    using K = PanelAction::Kind;
    cairo_t* cr = pen.cr;
    const Language lang = config.language;
    fillRounded(pen, kPad, kContentY, kContentW, kContentBottom - kContentY, 20, kCard);
    const double innerX = kPad + 26;
    const double headBase = lineBaseline(kContentY + 24, 22);
    const double headW = pen.text(innerX, headBase, t.brightHeading, 22, kText, true);
    std::string where;
    for (int slot = 0; slot < kSlotCount; ++slot) {
        if (config.slot(slot).type != ActionType::BrightnessCycle) continue;
        where += (where.empty() ? "" : t.listSep) + slotLabel(lang, slot);
    }
    where = where.empty() ? std::string(t.brightNowhere) : formatText(t.brightWhereFormat, where);
    pen.text(innerX + headW + 12, headBase, where, 15, kTextMuted);

    // いまの明るさに合う段（±2.5% 以内で一番近いもの）
    const std::vector<double>& levels = config.brightnessLevels;
    int nowIndex = -1;
    if (view.haveStatus && view.status.brightnessPercent >= 0) {
        double best = 2.5;
        for (size_t i = 0; i < levels.size(); ++i) {
            const double d = std::fabs(levels[i] - view.status.brightnessPercent);
            if (d <= best) {
                best = d;
                nowIndex = static_cast<int>(i);
            }
        }
    }

    const double rowY = kContentY + 24 + 22 * 1.45 + 18;
    const double cardW = 170;
    const double inner = cardW - 4 - 28;
    const double cardH = 4 + 14 + 26 + 10 + 56 + 10 + 34 * 1.45 + 10 + 52 + 10 + 44 + 14;
    const bool canRemove = static_cast<int>(levels.size()) > kMinBrightnessLevels;
    double x = innerX;
    for (size_t i = 0; i < levels.size(); ++i) {
        const int idx = static_cast<int>(i);
        const bool now = idx == nowIndex;
        fillRounded(pen, x, rowY, cardW, cardH, 20, kControl);
        strokeRounded(pen, x, rowY, cardW, cardH, 20, now ? kAccent : kBorderDisabled, 2);
        const double ix = x + 2 + 14;
        double y = rowY + 2 + 14;
        pen.text(ix, centerBaseline(y, 26, 15), formatText(t.brightStepFormat, std::to_string(idx + 1)), 15, kTextMuted,
                 true);
        if (now) {
            const double bw = pen.measure(t.brightNow, 13, true) + 20;
            fillRounded(pen, ix + inner - bw, y, bw, 26, 13, kAccent);
            pen.text(ix + inner - bw + 10, centerBaseline(y, 26, 13), t.brightNow, 13, kOnAccent, true);
        }
        y += 26 + 10;
        const int gray = static_cast<int>(std::lround(40 + (levels[i] / 125.0) * 200));
        fillRounded(pen, ix, y, inner, 56, 12, {gray / 255.0, gray / 255.0, std::min(255, gray + 6) / 255.0});
        y += 56 + 10;
        textCentered(pen, ix + inner / 2, lineBaseline(y, 34), percentText(levels[i]), 34, kText, true);
        y += 34 * 1.45 + 10;
        const double bw = (inner - 8) / 2;
        const PanelAction dec {K::LevelDec, idx, 0};
        const PanelAction inc {K::LevelInc, idx, 0};
        for (int k = 0; k < 2; ++k) {
            const PanelAction& a = k == 0 ? dec : inc;
            const double bx = ix + k * (bw + 8);
            const bool enabled = k == 0 ? levels[i] > kMinBrightnessPct : levels[i] < kMaxBrightnessPct;
            fillRounded(pen, bx, y, bw, 52, 16, enabled && isHot(a) ? kControlHover : kControl);
            strokeRounded(pen, bx, y, bw, 52, 16, enabled ? (pressed_ == a ? kAccent : kBorder) : kBorderDisabled, 2);
            textCentered(pen, bx + bw / 2, centerBaseline(y, 52, 24), k == 0 ? "−" : "＋", 24,
                         enabled ? kText : kTextDisabled, false);
            addButton(a, bx, y, bw, 52, enabled);
        }
        y += 52 + 10;
        const PanelAction remove {K::LevelRemove, idx, 0};
        if (canRemove && isHot(remove)) fillRounded(pen, ix, y, inner, 44, 14, kControlHover);
        strokeRounded(pen, ix, y, inner, 44, 14, canRemove ? kBorder : kBorderDisabled, 2);
        const double removeSize = fitSize(pen, t.brightRemove, 15, 11, inner - 12, true);
        textCentered(pen, ix + inner / 2, centerBaseline(y, 44, removeSize), t.brightRemove, removeSize,
                     canRemove ? kTextLabel : kTextDisabled, true);
        addButton(remove, ix, y, inner, 44, canRemove);
        // 次の段への →
        const double arrowX = x + cardW + 10;
        const double arrowW = pen.measure("→", 26, false);
        pen.text(arrowX, centerBaseline(rowY, cardH, 26), "→", 26, kArrow, false);
        x = arrowX + arrowW + 10;
    }
    if (static_cast<int>(levels.size()) < kMaxBrightnessLevels) {
        const PanelAction add {K::LevelAdd, 0, 0};
        const double aw = 150;
        if (isHot(add)) fillRounded(pen, x, rowY, aw, cardH, 20, kAccentTint);
        strokeDashed(pen, x, rowY, aw, cardH, 20, kAccent, 2);
        const double blockH = 34 + 6 + 18 * 1.45;
        const double top = rowY + (cardH - blockH) / 2;
        textCentered(pen, x + aw / 2, centerBaseline(top, 34, 34), "＋", 34, kAccent, false);
        const double addSize = fitSize(pen, t.brightAdd, 18, 12, aw - 16, true);
        textCentered(pen, x + aw / 2, lineBaseline(top + 34 + 6, 18), t.brightAdd, addSize, kAccent, true);
        addButton(add, x, rowY, aw, cardH);
        x += aw + 10;
    }
    // 1 段目に戻る
    {
        const double lw = 120;
        const double blockH = 34 + 6 + 15 * 1.45;
        const double top = rowY + (cardH - blockH) / 2;
        drawIcon(cr, Icon::Loop, x + lw / 2, top + 17, 34, kTextMuted);
        const double loopSize = fitSize(pen, t.brightLoop, 15, 11, lw, false);
        textCentered(pen, x + lw / 2, lineBaseline(top + 34 + 6, 15), t.brightLoop, loopSize, kTextMuted, false);
    }
    const double noteSize = fitSize(pen, t.brightNote, 14, 11, kContentW - 52, false);
    pen.text(innerX, lineBaseline(kContentBottom - 24 - 20, 14), t.brightNote, noteSize, kTextMuted);
}

void SettingsPanel::drawPressTab(const Pen& pen, const UiText& t, const Config& config, const PanelView& /*view*/) {
    using K = PanelAction::Kind;
    cairo_t* cr = pen.cr;
    // 区画の余白 20 / 26・間 12。見出しの下に「長押し」「回数の区切り」のカードを縦に 2 つ、最後に 1 行の補足
    fillRounded(pen, kPad, kContentY, kContentW, kContentBottom - kContentY, 20, kCard);
    const double innerX = kPad + 26;
    const double innerW = kContentW - 52;
    double y = kContentY + 20;
    pen.text(innerX, lineBaseline(y, 22), t.pressHeading, 22, kText, true);
    y += 22 * 1.45 + 12;

    // 図の目盛り: 0〜1.5 秒
    constexpr double kScaleMs = 1500;
    const double cardH = 14 + 56 + 6 + 78 + 14;
    const double trackX = innerX + 20;
    const double trackW = innerW - 40;
    /**
     * 時間（ms）を図の x にする（0〜1.5 秒に収める）。
     */
    const auto posX = [&](double ms) { return trackX + trackW * std::clamp(ms / kScaleMs, 0.0, 1.0); };
    /**
     * 図の文字（CSS の top の位置に 13px の行を置く）。
     */
    const auto smallText = [&](double x, double top, const std::string& text, Color c, bool bold, bool alignRight = false) {
        return pen.text(x, lineBaseline(top, 13), text, 13, c, bold, alignRight);
    };
    /**
     * 図の下の線と、0・0.5・1.0・1.5 秒の目盛り。
     */
    const auto drawAxis = [&](double diagramTop) {
        pen.color(kDivider);
        cairo_rectangle(cr, trackX, diagramTop + 56, trackW, 1);
        cairo_fill(cr);
        for (int ms = 0; ms <= kScaleMs; ms += 500) {
            char number[16];
            std::snprintf(number, sizeof(number), "%.1f", ms / 1000.0);
            const std::string label = formatText(t.secondsFormat, number);
            const double w = pen.measure(label, 12, false);
            const double x = ms == 0 ? posX(ms) : (ms == kScaleMs ? posX(ms) - w : posX(ms) - w / 2);
            pen.text(x, lineBaseline(diagramTop + 60, 12), label, 12, kArrow, false);
        }
    };
    /**
     * ピンクの縦線（決まる時点）。
     */
    const auto drawMarker = [&](double x, double diagramTop) {
        fillRounded(pen, x - 1, diagramTop + 16, 3, 40, 1.5, kAccent);
    };

    for (int card = 0; card < 2; ++card) {
        const bool isLong = card == 0;
        fillRounded(pen, innerX, y, innerW, cardH, 16, kDiagram);
        // 上の行: 名前・説明（左）と − 値 ＋（右）
        const double rowY = y + 14;
        const double stepperW = 220;
        const double sx = innerX + innerW - 20 - stepperW;
        const double textW = sx - 14 - trackX;
        const double blockTop = rowY + (56 - (19 * 1.45 + 14 * 1.45)) / 2;
        pen.text(trackX, lineBaseline(blockTop, 19), isLong ? t.longTitle : t.gapTitle, 19, kText, true);
        const char* hint = isLong ? t.longHint : t.gapHint;
        pen.text(trackX, lineBaseline(blockTop + 19 * 1.45, 14), hint, fitSize(pen, hint, 14, 11, textW, false), kTextMuted);
        strokeRounded(pen, sx, rowY, stepperW, 56, 30, kBorder, 2);
        const PanelAction dec {isLong ? K::LongDec : K::GapDec, 0, 0};
        const PanelAction inc {isLong ? K::LongInc : K::GapInc, 0, 0};
        const double value = isLong ? config.longPressMs : config.multiPressGapMs;
        const bool decOk = value > (isLong ? kLongMinMs : kGapMinMs);
        const bool incOk = value < (isLong ? kLongMaxMs : kGapMaxMs);
        if (decOk && isHot(dec)) fillRounded(pen, sx + 2, rowY + 2, 56, 52, 26, kControlHover);
        if (incOk && isHot(inc)) fillRounded(pen, sx + stepperW - 58, rowY + 2, 56, 52, 26, kControlHover);
        textCentered(pen, sx + 2 + 28, centerBaseline(rowY, 56, 26), "−", 26, decOk ? kText : kTextDisabled, false);
        textCentered(pen, sx + stepperW - 2 - 28, centerBaseline(rowY, 56, 26), "＋", 26, incOk ? kText : kTextDisabled,
                     false);
        pen.color(kDivider);
        cairo_rectangle(cr, sx + 2 + 56, rowY + 2, 2, 52);
        cairo_rectangle(cr, sx + 2 + 56 + 102, rowY + 2, 2, 52);
        cairo_fill(cr);
        textCentered(pen, sx + 2 + 56 + 52, centerBaseline(rowY, 56, 21), secondsText(t, value, isLong ? 1 : 2), 21,
                     kText, true);
        addButton(dec, sx, rowY, 58, 56, decOk);
        addButton(inc, sx + stepperW - 58, rowY, 58, 56, incOk);

        // 下の図（高さ 78）
        const double top = rowY + 56 + 6;
        if (isLong) {
            // 押し続ける棒。長押しの秒数より後ろはピンク。長押しを長くしても棒が線まで届くよう、
            // 終わりは min(1.5 秒, max(1.2 秒, 長押し + 0.25 秒))（1.5 秒のときは線が右端なので、ピンクの部分は無い）
            const double barEnd = posX(std::min(kScaleMs, std::max(1200.0, value + 250.0)));
            const double lx = posX(value);
            fillRounded(pen, trackX, top + 26, barEnd - trackX, 22, 6, kTextLabel);
            if (lx < barEnd) {
                cairo_save(cr);
                cairo_rectangle(cr, lx, top + 26, barEnd - lx, 22);
                cairo_clip(cr);
                fillRounded(pen, trackX, top + 26, barEnd - trackX, 22, 6, kAccent);
                cairo_restore(cr);
            }
            smallText(trackX + 10, top + 28, t.pressHold, kOnAccent, true);
            drawMarker(lx, top);
            // 「ここで離せば 1 回押し」は、線が左に寄りすぎて重なるときは出さない（25% 未満で隠す）
            const double ratio = (lx - trackX) / trackW;
            if (ratio >= 0.25) smallText(trackX, top + 2, t.pressReleaseHere, kTextMuted, false);
            char number[16];
            std::snprintf(number, sizeof(number), "%.1f", value / 1000.0);
            const std::string chip = formatText(t.pressLongChipFormat, number);
            const double chipW = pen.measure(chip, 13, true) + 20;
            const double chipX = ratio > 0.65 ? lx - chipW : lx + 6;
            fillRounded(pen, chipX, top, chipW, 22, 11, kAccent);
            pen.text(chipX + 10, centerBaseline(top, 22, 13), chip, 13, kOnAccent, true);
        } else {
            // 1 回目の棒（0〜0.15 秒）と、区切りの秒数ぶんの点線の枠
            constexpr double kTapEndMs = 150;
            const double gx1 = posX(kTapEndMs);
            const double gx2 = posX(kTapEndMs + value);
            fillRounded(pen, trackX, top + 26, gx1 - trackX, 22, 6, kTextLabel);
            smallText(trackX, top + 4, t.pressFirst, kText, true);
            fillRounded(pen, gx1, top + 26, gx2 - gx1, 22, 6, kAccent, 0.18);
            strokeDashed(pen, gx1, top + 26, gx2 - gx1, 22, 6, kAccent, 2);
            smallText(gx1, top + 2, t.pressGapBox, kAccent, true);
            drawMarker(gx2, top);
            smallText(gx2 + 10, top + 28, t.pressGapChip, kText, true);
        }
        drawAxis(top);
        y += cardH + 12;
    }
    pen.text(innerX, lineBaseline(y, 14), t.pressNote, 14, kTextMuted);
}

void SettingsPanel::drawDisplayTab(const Pen& pen, const UiText& t, const Config& config) {
    using K = PanelAction::Kind;
    const double colW = (kContentW - 16) / 2;
    const double h = kContentBottom - kContentY;
    fillRounded(pen, kPad, kContentY, colW, h, 20, kCard);
    struct Row {
        const char* title;
        const char* hint;
        K on;
        K off;
        bool value;
    };
    const Row rows[3] = {
        {t.soundTitle, t.soundHint, K::SoundOn, K::SoundOff, config.soundEnabled},
        {t.hudTitle, t.hudHint, K::HudOn, K::HudOff, config.hudEnabled},
        {t.badgeTitle, t.badgeHint, K::BadgeOn, K::BadgeOff, config.muteBadge},
    };
    double y = kContentY + 24;
    const double x = kPad + 26;
    for (const Row& row : rows) {
        pen.text(x, lineBaseline(y, 19), row.title, 19, kText, true);
        y += 19 * 1.45 + 10;
        pen.text(x, lineBaseline(y, 14), row.hint, fitSize(pen, row.hint, 14, 11, colW - 52, false), kTextMuted);
        y += 14 * 1.45 + 10;
        drawToggle(pen, x, y, {row.on, 0, 0}, {row.off, 0, 0}, t.on, t.off, row.value ? 0 : 1, true, false);
        y += 56 + 22;
    }

    // 視界のイメージ（オフにした表示は薄く）
    const double px = kPad + colW + 16;
    fillRounded(pen, px, kContentY, colW, h, 20, kPreview);
    strokeRounded(pen, px, kContentY, colW, h, 20, kDivider, 2);
    cairo_t* cr = pen.cr;
    const double centerX = px + colW / 2;
    {
        BadgeStyle style;
        style.height = 40;
        style.iconSize = 18;
        style.gap = 8;
        style.padLeft = 12;
        style.padRight = 16;
        style.textSize = 17;
        const double bw = measureBadge(pen, t.badgeText, style);
        cairo_push_group(cr);
        drawBadge(pen, t.badgeText, style, centerX - bw / 2, kContentY + 2 + 24);
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, config.muteBadge ? 1.0 : 0.3);
    }
    textCentered(pen, centerX, centerBaseline(kContentY, h, 14), t.previewCaption, 14, kTextFaint, false);
    {
        ToastStyle style;
        style.height = 60;
        style.iconDiameter = 44;
        style.iconSize = 22;
        style.gap = 12;
        style.padLeft = 8;
        style.padRight = 22;
        style.titleSize = 20;
        HudToast toast;
        toast.icon = Icon::MicOff;
        toast.tone = HudToast::Tone::Danger;
        toast.title = t.hudMicMuted;
        const double tw = measureToast(pen, toast, style, colW - 48);
        cairo_push_group(cr);
        drawToast(pen, toast, style, centerX - tw / 2, kContentY + h - 2 - 24 - 60, colW - 48);
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, config.hudEnabled ? 1.0 : 0.3);
    }
}

double SettingsPanel::drawUpdateBox(const Pen& pen, const UiText& t, const Config& config, const PanelView& view) {
    using K = PanelAction::Kind;
    using frame_updater::UpdateState;
    const frame_updater::UpdateStatus& u = view.update;
    const Language lang = config.language;
    const bool confirming = updateConfirmArmed_ && u.state == UpdateState::Available;

    std::string text;
    std::string sub;
    Color textColor = kTextLabel;
    Color fill = kCard;
    Color border = kCard;
    struct UpdateButton {
        K kind;
        const char* label;
        bool primary;
        bool enabled;
    };
    std::vector<UpdateButton> buttons;
    if (confirming) {
        text = formatText(t.updateConfirmFormat, u.latest);
        sub = t.updateConfirmHint;
        textColor = kText;
        border = kAccent;
        buttons.push_back(UpdateButton{K::UpdateConfirmNo, t.updateConfirmNo, false, true});
        buttons.push_back(UpdateButton{K::UpdateConfirmYes, t.updateConfirmYes, true, true});
    } else {
        switch (u.state) {
            case UpdateState::Unknown:
                text = u.checking ? t.updateChecking : formatText(t.updateVersionFormat, u.current);
                buttons.push_back(UpdateButton{K::UpdateCheckNow, t.updateCheckNow, false, !u.checking});
                break;
            case UpdateState::UpToDate:
                text = u.checking ? t.updateChecking : formatText(t.updateUpToDateFormat, u.current);
                buttons.push_back(UpdateButton{K::UpdateCheckNow, t.updateCheckNow, false, !u.checking});
                break;
            case UpdateState::Available: {
                text = formatText(t.updateAvailableFormat, u.latest);
                textColor = kText;
                border = kAccent;
                // 新しい版の要約（日本語の画面で日本語の要約があればそれ）。無ければ何も出さない
                sub = lang == Language::Ja && !u.notesJa.empty() ? u.notesJa : u.notes;
                if (!u.installable) {
                    sub = t.updateManual;
                    buttons.push_back(UpdateButton{K::UpdateCheckNow, t.updateCheckNow, false, !u.checking});
                } else {
                    buttons.push_back(UpdateButton{K::UpdateInstall, t.updateButton, true, true});
                }
                break;
            }
            case UpdateState::Installing:
                text = formatText(t.updateInstallingFormat, updateStepText(lang, u.step));
                sub = t.updateConfirmHint;
                textColor = kText;
                break;
            case UpdateState::Installed:
                text = formatText(t.updateInstalledFormat, u.version);
                textColor = kSuccess;
                buttons.push_back(UpdateButton{K::UpdateDismiss, t.updateDismiss, false, true});
                break;
            case UpdateState::CheckFailed:
                text = std::string(t.updateCheckFailed) + " " + updateErrorText(lang, u.error);
                textColor = kDangerText;
                fill = kDangerFill;
                border = kDangerBorder;
                buttons.push_back(UpdateButton{K::UpdateCheckNow, t.updateCheckNow, false, !u.checking});
                break;
            case UpdateState::InstallFailed:
                text = std::string(t.updateInstallFailed) + " " + updateErrorText(lang, u.error);
                sub = t.updateLogHint;
                textColor = kDangerText;
                fill = kDangerFill;
                border = kDangerBorder;
                buttons.push_back(UpdateButton{K::UpdateRetry, t.updateRetry, false, true});
                buttons.push_back(UpdateButton{K::UpdateDismiss, t.updateDismiss, false, true});
                break;
        }
    }

    // ボタンの幅（高さ 56・角 16・18px 太字、ふつうは左右 22、強調は 26）
    double buttonsW = 0;
    std::vector<double> widths;
    for (const auto& b : buttons) {
        widths.push_back(pen.measure(b.label, 18, true) + (b.primary ? 52 : 44));
        buttonsW += widths.back();
    }
    buttonsW += buttons.empty() ? 0 : 12.0 * (buttons.size() - 1);
    const double textX = kPad + 22;
    const double textRight = kPad + kContentW - 12 - (buttons.empty() ? 10 : buttonsW + 16);
    const double textW = textRight - textX;
    const double textSize = fitSize(pen, text, 19, 14, textW, true);
    const std::vector<std::string> subLines = sub.empty() ? std::vector<std::string>() : wrapText(pen, sub, 14, textW, 3);
    const double blockH = 19 * 1.45 + (subLines.empty() ? 0 : 2 + subLines.size() * 14 * 1.45);
    const double boxH = std::max(76.0, blockH + 20);
    fillRounded(pen, kPad, kContentY, kContentW, boxH, 20, fill);
    if (border.r != fill.r || border.g != fill.g || border.b != fill.b) {
        strokeRounded(pen, kPad, kContentY, kContentW, boxH, 20, border, 2);
    }
    double ty = kContentY + (boxH - blockH) / 2;
    pen.text(textX, lineBaseline(ty, 19), text, textSize, textColor, true);
    ty += 19 * 1.45 + 2;
    for (const auto& line : subLines) {
        pen.text(textX, lineBaseline(ty, 14), line, 14, kTextMuted);
        ty += 14 * 1.45;
    }
    double bx = kPad + kContentW - 12 - buttonsW;
    const double by = kContentY + (boxH - 56) / 2;
    for (size_t i = 0; i < buttons.size(); ++i) {
        const auto& b = buttons[i];
        const PanelAction action {b.kind, 0, 0};
        const double w = widths[i];
        const double alpha = b.enabled ? 1.0 : 0.45;
        if (b.primary) {
            fillRounded(pen, bx, by, w, 56, 16, pressed_ == action ? kAccentPressed : kAccent, alpha);
        } else {
            fillRounded(pen, bx, by, w, 56, 16, b.enabled && isHot(action) ? kControlHover : kControl, alpha);
            pen.color(kBorder, alpha);
            cairo_set_line_width(pen.cr, 2);
            pen.roundedRect(bx + 1, by + 1, w - 2, 54, 15);
            cairo_stroke(pen.cr);
        }
        const Color fg = b.primary ? kOnAccent : kText;
        pen.color(fg, alpha);
        const double lw = pen.measure(b.label, 18, true);
        cairo_move_to(pen.cr, bx + (w - lw) / 2, centerBaseline(by, 56, 18));
        cairo_show_text(pen.cr, b.label);
        addButton(action, bx, by, w, 56, b.enabled);
        bx += w + 12;
    }
    return kContentY + boxH;
}

void SettingsPanel::drawAppTab(const Pen& pen, const UiText& t, const Config& config, const PanelView& view) {
    using K = PanelAction::Kind;
    const size_t buttonsBefore = buttons_.size();
    const Language lang = config.language;
    const double boxBottom = drawUpdateBox(pen, t, config, view);
    const double top = boxBottom + 14;
    const double h = kContentBottom - top;
    const double colW = (kContentW - 14) / 2;

    // 左: パネルを閉じても aux を使う・コントローラーが接続されていないときは aux を従来の動きに戻す・新しい版の確認・SteamVR と一緒に起動・言語、
    // 下にすべて既定に戻す・終了
    fillRounded(pen, kPad, top, colW, h, 20, kCard);
    const double x = kPad + 22;
    const double right = kPad + colW - 22;
    const AutostartStatus& autostart = view.autostart;
    const bool autoInstalled = autostart.state != AutostartStatus::State::NotInstalled;
    const char* autoHint = !autoInstalled     ? t.autoNotInstalled
                           : autostart.failed ? t.autoFailed
                           : autostart.busy   ? t.autoBusy
                                              : t.autoHint;
    struct Row {
        const char* title;
        const char* hint;
        K left;
        K right;
        std::string leftLabel;
        std::string rightLabel;
        int selected;
        bool enabled;
    };
    const int autoSelected = autostart.state == AutostartStatus::State::Enabled    ? 0
                             : autostart.state == AutostartStatus::State::Disabled ? 1
                                                                                   : -1;
    const Row rows[] = {
        {t.bgTitle, config.runInBackground ? t.bgHintOn : t.bgHintOff, K::BackgroundOn, K::BackgroundOff, t.on, t.off,
         config.runInBackground ? 0 : 1, true},
        {t.ptrTitle, config.originalAuxOnHeadPointer ? t.ptrHintOn : t.ptrHintOff, K::HeadPointerOn, K::HeadPointerOff,
         t.on, t.off, config.originalAuxOnHeadPointer ? 0 : 1, true},
        {t.checkTitle, t.checkHint, K::CheckOn, K::CheckOff, t.on, t.off, config.updateCheck ? 0 : 1, true},
        {t.autoTitle, autoHint, K::AutostartOn, K::AutostartOff, t.on, t.off, autoSelected, autoInstalled && !autostart.busy},
        // 言語の名前は、どちらの言語で表示していてもその言語自身の書き方にする
        {t.langTitle, nullptr, K::LanguageJa, K::LanguageEn, "日本語", "English", config.language == Language::Ja ? 0 : 1,
         true},
    };
    // 下の 2 つのボタンの上に収まるよう、行の間を詰める（更新の帯が高いとき）
    const double by = top + h - 16 - 56;
    const double rowH = 64;
    constexpr int rowCount = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
    const double rowGap = std::clamp((by - 12 - (top + 14) - rowCount * rowH) / (rowCount - 1), 0.0, 10.0);
    double y = top + 14;
    for (const Row& row : rows) {
        // 右の切り替えを先に置いて、残りの幅に見出しを入れる
        const double toggleW = drawToggle(pen, right, y + (rowH - 56) / 2, {row.left, 0, 0}, {row.right, 0, 0},
                                          row.leftLabel, row.rightLabel, row.selected, row.enabled, true);
        const double textW = right - toggleW - 14 - x;
        const double titleSize = fitSize(pen, row.title, 18, 13, textW, true);
        if (row.hint == nullptr) {
            pen.text(x, centerBaseline(y, rowH, 18), row.title, titleSize, kText, true);
        } else {
            const double blockTop = y + (rowH - (18 * 1.45 + 13 * 1.45)) / 2;
            pen.text(x, lineBaseline(blockTop, 18), row.title, titleSize, kText, true);
            const bool alert = row.left == K::AutostartOn && autostart.failed;
            // 入らなければ少し小さくし（11px まで）、それでも入らなければ後ろを … にする
            const double hintSize = fitSize(pen, row.hint, 13, 11, textW, alert);
            const std::string hint = ellipsize(pen, row.hint, hintSize, alert, textW);
            pen.text(x, lineBaseline(blockTop + 18 * 1.45, 13), hint, hintSize, alert ? kDanger : kTextMuted, alert);
        }
        y += rowH + rowGap;
    }
    {
        // すべて既定に戻す・終了（赤い枠。確認中は赤い塗り）
        const double bw = (right - x - 12) / 2;
        struct Danger {
            K kind;
            const char* label;
            bool armed;
        };
        const Danger dangers[2] = {
            {K::ResetAll, resetArmed_ ? t.resetConfirm : t.resetAll, resetArmed_},
            {K::Quit, quitArmed_ ? t.quitConfirm : (config.runInBackground ? t.quitBoth : t.quitApp), quitArmed_},
        };
        for (int i = 0; i < 2; ++i) {
            const PanelAction action {dangers[i].kind, 0, 0};
            const double bx = x + i * (bw + 12);
            fillRounded(pen, bx, by, bw, 56, 28,
                        dangers[i].armed ? kDangerBorder : (isHot(action) ? kControlHover : kDangerFill));
            strokeRounded(pen, bx, by, bw, 56, 28, kDangerBorder, 2);
            const double size = fitSize(pen, dangers[i].label, 16, 11, bw - 20, true);
            textCentered(pen, bx + bw / 2, centerBaseline(by, 56, size), dangers[i].label, size,
                         dangers[i].armed ? kOnAccent : kText, true);
            addButton(action, bx, by, bw, 56);
        }
    }

    // 右: いっしょに使うアプリ（このアプリで使うものだけ）と、ささけんのアプリの一覧を開く行
    const double rx = kPad + colW + 14;
    fillRounded(pen, rx, top, colW, h, 20, kCard);
    const double ix = rx + 18;
    const double iw = colW - 36;
    double ry = top + 16;
    pen.text(ix, lineBaseline(ry, 18), t.appsTitle, 18, kText, true);
    ry += 18 * 1.45 + 2;
    pen.text(ix, lineBaseline(ry, 13), ellipsize(pen, t.appsRelatedHint, 13, false, iw), 13, kTextMuted);
    ry += 13 * 1.45 + 10;
    const double listRowY = top + h - 16 - 64;
    for (const frame_apps::Entry& entry : view.apps) {
        if (!entry.related || ry + 72 > listRowY - 10) continue;
        drawAppRow(pen, t, lang, entry, ix, ry, iw, 72, true, false, 0, 0);
        ry += 72 + 10;
    }
    {
        // 「ささけんの Frame アプリ　N 個のうち M 個が入っていません ›」（枠だけの行。押すと一覧）
        const PanelAction open {K::AppsOpenList, 0, 0, ""};
        if (isHot(open)) fillRounded(pen, ix, listRowY, iw, 64, 14, kControlHover);
        strokeRounded(pen, ix, listRowY, iw, 64, 14, kDivider, 2);
        int missing = 0;
        for (const frame_apps::Entry& entry : view.apps) missing += entry.state == frame_apps::AppState::Missing ? 1 : 0;
        char count[160];
        if (missing > 0) {
            std::snprintf(count, sizeof(count), t.appsAllCountFormat, static_cast<int>(view.apps.size()), missing);
        } else {
            std::snprintf(count, sizeof(count), t.appsAllInstalledFormat, static_cast<int>(view.apps.size()));
        }
        const double textW = iw - 18 - 40;
        const double blockTop = listRowY + (64 - (16 * 1.45 + 2 + 13 * 1.45)) / 2;
        pen.text(ix + 18, lineBaseline(blockTop, 16), ellipsize(pen, t.appsAllTitle, 16, true, textW), 16, kText, true);
        pen.text(ix + 18, lineBaseline(blockTop + 16 * 1.45 + 2, 13), ellipsize(pen, count, 13, false, textW), 13, kTextMuted);
        // ›（線で描く）
        cairo_t* cr = pen.cr;
        pen.color(kTextMuted);
        cairo_set_line_width(cr, 2.5);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        const double ax = ix + iw - 24;
        const double ay = listRowY + 32;
        cairo_move_to(cr, ax - 5, ay - 9);
        cairo_line_to(cr, ax + 3, ay);
        cairo_line_to(cr, ax - 5, ay + 9);
        cairo_stroke(cr);
        addButton(open, ix, listRowY, iw, 64);
    }

    if (appsListOpen_ || appsConfirmOpen_) {
        // シートの下のボタンは押せないようにする（見出しのタブは残す）
        buttons_.resize(buttonsBefore);
        fillRounded(pen, kPad, kContentY, kContentW, kContentBottom - kContentY, 20, kBg, 0.85);
        if (appsListOpen_) drawAppsList(pen, t, lang, view);
        if (appsConfirmOpen_) {
            if (appsListOpen_) {
                // 一覧の上に確認を重ねる: 一覧のボタンも押せないようにして、もう一段暗くする
                buttons_.resize(buttonsBefore);
                fillRounded(pen, kPad, kContentY, kContentW, kContentBottom - kContentY, 20, kBg, 0.7);
            }
            drawAppsConfirm(pen, t, view);
        }
    }
}

void SettingsPanel::drawAppRow(const Pen& pen, const UiText& t, Language lang, const frame_apps::Entry& entry, double x,
                               double y, double w, double h, bool showReason, bool inList, double clipTop,
                               double clipBottom) {
    using K = PanelAction::Kind;
    using frame_apps::AppState;
    const frame_apps::Lang alang = lang == Language::Ja ? frame_apps::Lang::Ja : frame_apps::Lang::En;
    fillRounded(pen, x, y, w, h, 14, kControl);
    // 左: アプリのアイコン（角丸のタイルごと。取ってきたもの → 同梱のもの。どちらも無ければ代わりに Perf などの文字の札）
    const double tile = h >= 76 ? 52 : 48;
    const double tx = x + 12;
    if (!frame_apps::drawIcon(pen.cr, entry, tx, y + (h - tile) / 2, tile)) {
        fillRounded(pen, tx, y + (h - tile) / 2, tile, tile, 14, kIconBox);
        const double monoSize = fitSize(pen, entry.app.mono, 15, 11, tile - 8, true);
        textCentered(pen, tx + tile / 2, centerBaseline(y + (h - tile) / 2, tile, monoSize), entry.app.mono, monoSize,
                     kAccent, true);
    }
    // 右: 状態の札か［入れる］
    double rightEdge = x + w - 10;
    if (entry.state == AppState::Missing && !entry.busy) {
        const PanelAction install {K::AppInstall, 0, 0, entry.app.key};
        const double bw = pen.measure(t.appsInstall, 16, true) + 36;
        const double bx = rightEdge - bw;
        const double by = y + (h - 48) / 2;
        fillRounded(pen, bx, by, bw, 48, 16, isHot(install) ? kAccentPressed : kAccent);
        textCentered(pen, bx + bw / 2, centerBaseline(by, 48, 16), t.appsInstall, 16, kOnAccent, true);
        if (inList) {
            const double top = std::max(by, clipTop);
            const double bottom = std::min(by + 48, clipBottom);
            if (bottom - top > 4) addButton(install, bx, top, bw, bottom - top);
        } else {
            addButton(install, bx, by, bw, 48);
        }
        rightEdge = bx - 12;
    } else if (entry.state != AppState::Unknown || entry.busy) {
        const char* label = entry.busy                         ? t.appsChipBusy
                            : entry.state == AppState::Running ? t.chipRunning
                                                               : t.appsChipInstalled;
        const Color fill = entry.busy ? kInfoTint : (entry.state == AppState::Running ? kSuccessTint : kStoppedTint);
        const Color fg = entry.busy ? kInfo : (entry.state == AppState::Running ? kSuccess : kTextLabel);
        const double cw = 12 + 8 + 8 + pen.measure(label, 14, true) + 12;
        const double cx = rightEdge - cw;
        const double cy = y + (h - 32) / 2;
        fillRounded(pen, cx, cy, cw, 32, 16, fill);
        drawDisc(pen.cr, cx + 12 + 4, cy + 16, 4, fg);
        pen.text(cx + 12 + 8 + 8, centerBaseline(cy, 32, 14), label, 14, fg, true);
        rightEdge = cx - 12;
    }
    // 真ん中: 名前（一覧では「このアプリで使う」の札も）と、説明か理由
    const double textX = tx + tile + 12;
    const double textW = std::max(40.0, rightEdge - textX);
    const double nameSize = inList ? 17 : 16;
    const double subSize = inList ? 14 : 13;
    const double blockTop = y + (h - (nameSize * 1.45 + 2 + subSize * 1.45)) / 2;
    std::string tag;
    double tagW = 0;
    if (inList && entry.related) {
        tag = t.appsRelatedTag;
        tagW = pen.measure(tag, 12, true) + 16;
    }
    const double nameW = pen.measure(entry.app.name, nameSize, true);
    const bool tagFits = !tag.empty() && nameW + 8 + tagW <= textW;
    const std::string name = ellipsize(pen, entry.app.name, nameSize, true, tagFits ? textW - 8 - tagW : textW);
    const double drawnW = pen.text(textX, lineBaseline(blockTop, nameSize), name, nameSize, kText, true);
    if (tagFits) {
        const double gx = textX + drawnW + 8;
        const double gy = blockTop + (nameSize * 1.45 - 22) / 2;
        fillRounded(pen, gx, gy, tagW, 22, 11, kAccentTint);
        pen.text(gx + 8, centerBaseline(gy, 22, 12), tag, 12, kAccent, true);
    }
    const std::string reason = entry.reason(alang);
    const bool pink = showReason && !reason.empty();
    const std::string sub = pink ? reason : entry.app.desc(alang);
    pen.text(textX, lineBaseline(blockTop + nameSize * 1.45 + 2, subSize), ellipsize(pen, sub, subSize, false, textW), subSize,
             pink ? kAccent : kTextMuted);
}

void SettingsPanel::drawAppsList(const Pen& pen, const UiText& t, Language lang, const PanelView& view) {
    using K = PanelAction::Kind;
    cairo_t* cr = pen.cr;
    const double secH = kContentBottom - kContentY;
    const double w = 900;
    const double h = secH - 16;
    const double x = kPad + (kContentW - w) / 2;
    const double y = kContentY + 8;
    fillRounded(pen, x, y, w, h, 20, kCard);
    strokeRounded(pen, x, y, w, h, 20, kBorder, 2);
    const double ix = x + 24;
    const double innerW = w - 48;

    // 見出し（左）と ✕（右）
    const double headY = y + 20;
    const PanelAction close {K::AppsCloseList, 0, 0, ""};
    const double closeX = ix + innerW - 52;
    fillRounded(pen, closeX, headY + 2, 52, 52, 16, isHot(close) ? kControlHover : kControl);
    strokeRounded(pen, closeX, headY + 2, 52, 52, 16, kBorder, 2);
    pen.color(kText);
    cairo_set_line_width(cr, 2.5);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, closeX + 26 - 8, headY + 28 - 8);
    cairo_line_to(cr, closeX + 26 + 8, headY + 28 + 8);
    cairo_move_to(cr, closeX + 26 + 8, headY + 28 - 8);
    cairo_line_to(cr, closeX + 26 - 8, headY + 28 + 8);
    cairo_stroke(cr);
    addButton(close, closeX, headY + 2, 52, 52);
    const double headW = closeX - 14 - ix;
    pen.text(ix, lineBaseline(headY, 22), t.appsAllTitle, 22, kText, true);
    pen.text(ix, lineBaseline(headY + 22 * 1.45 + 2, 14), ellipsize(pen, t.appsListSub, 14, false, headW), 14, kTextMuted);

    // 下: 補足（2 行まで）と［インストーラーを開く］
    const double footY = y + h - 20 - 52;
    const PanelAction menu {K::AppsOpenMenu, 0, 0, ""};
    const char* menuLabel = view.menuBusy ? t.appsChipBusy : t.appsOpenInstaller;
    const double menuW = pen.measure(menuLabel, 16, true) + 40;
    const double menuX = ix + innerW - menuW;
    fillRounded(pen, menuX, footY, menuW, 52, 16, !view.menuBusy && isHot(menu) ? kControlHover : kControl);
    strokeRounded(pen, menuX, footY, menuW, 52, 16, view.menuBusy ? kBorderDisabled : kBorder, 2);
    textCentered(pen, menuX + menuW / 2, centerBaseline(footY, 52, 16), menuLabel, 16, view.menuBusy ? kInfo : kText, true);
    if (!view.menuBusy) addButton(menu, menuX, footY, menuW, 52);
    const std::vector<std::string> noteLines = wrapText(pen, t.appsListNote, 13, menuX - 16 - ix, 2);
    double noteY = footY + (52 - noteLines.size() * 13 * 1.5) / 2;
    for (const std::string& line : noteLines) {
        pen.text(ix, centerBaseline(noteY, 13 * 1.5, 13), line, 13, kTextMuted);
        noteY += 13 * 1.5;
    }

    // 一覧の箱（スクロール。入りきらないときは右に 8px あけて細いスクロールバー）
    const double boxTop = headY + 56 + 14;
    const double boxBottom = footY - 14;
    const double boxH = boxBottom - boxTop;
    const double rowH = 76;
    const double pitch = rowH + 8;
    const double contentH = view.apps.empty() ? 0 : view.apps.size() * pitch - 8;
    appsMaxScroll_ = std::max(0.0, contentH - boxH);
    appsScroll_ = std::clamp(appsScroll_, 0.0, appsMaxScroll_);
    const double rowW = appsMaxScroll_ > 0 ? innerW - 16 : innerW;
    // 地を押したまま上下に動かすとスクロール（行の［入れる］は後から足すので、そちらが先に当たる）
    if (appsMaxScroll_ > 0) addButton({K::AppsListDrag, 0, 0, ""}, ix, boxTop, rowW, boxH);
    cairo_save(cr);
    cairo_rectangle(cr, ix - 4, boxTop, innerW + 8, boxH);
    cairo_clip(cr);
    for (size_t i = 0; i < view.apps.size(); ++i) {
        const double ry = boxTop + i * pitch - appsScroll_;
        if (ry + rowH < boxTop || ry > boxBottom) continue;
        drawAppRow(pen, t, lang, view.apps[i], ix, ry, rowW, rowH, false, true, boxTop, boxBottom);
    }
    cairo_restore(cr);
    if (appsMaxScroll_ > 0) {
        const double barX = ix + innerW - 8;
        fillRounded(pen, barX, boxTop, 8, boxH, 4, kDiagram);
        const double thumbH = std::max(40.0, boxH * boxH / contentH);
        const double thumbY = boxTop + (boxH - thumbH) * (appsScroll_ / appsMaxScroll_);
        fillRounded(pen, barX, thumbY, 8, thumbH, 4, kBorder);
    }
}

void SettingsPanel::drawAppsConfirm(const Pen& pen, const UiText& t, const PanelView& view) {
    using K = PanelAction::Kind;
    using frame_apps::LaunchResult;
    const double w = 800;
    const double innerW = w - 52;
    // アプリ名とコマンドは、今の一覧（取ってきたものかもしれない）から引く。Konsole に渡すのと同じ関数で作る
    const frame_apps::Entry* entry = appsConfirmEntry(view);
    const frame_apps::AppInfo* app = entry != nullptr ? &entry->app : nullptr;
    const bool unknownKey = !appsConfirmKey_.empty() && app == nullptr;  // 一覧が変わって消えた: 開かせない
    char title[200];
    if (app != nullptr) {
        std::snprintf(title, sizeof(title), t.appsConfirmTitleFormat, app->name.c_str());
    } else {
        std::snprintf(title, sizeof(title), "%s", t.appsConfirmMenuTitle);
    }
    const std::string command = appsConfirmCommand(view);
    const char* error = unknownKey ? t.appsErrorFailed : nullptr;
    switch (appsLaunchResult_) {
        case LaunchResult::Started: break;
        case LaunchResult::Busy: error = t.appsErrorBusy; break;
        case LaunchResult::NoDisplay: error = t.appsErrorNoDisplay; break;
        case LaunchResult::NoKonsole: error = t.appsErrorNoKonsole; break;
        case LaunchResult::UnknownApp:
        case LaunchResult::Failed: error = t.appsErrorFailed; break;
    }
    const char* bullets[3] = {app != nullptr ? t.appsConfirmProgress : t.appsConfirmMenu, t.appsConfirmNoSudo,
                              t.appsConfirmClose};
    const double h = 24 + 22 * 1.45 + 16 + 16 * 1.6 + 12 + 56 + 16 + 3 * 15 * 1.7 + (error != nullptr ? 8 + 15 * 1.6 : 0) +
                     20 + 56 + 24;
    const double x = kPad + (kContentW - w) / 2;
    double y = kContentY + (kContentBottom - kContentY - h) / 2;
    fillRounded(pen, x, y, w, h, 20, kCard);
    strokeRounded(pen, x, y, w, h, 20, kBorder, 2);
    const double ix = x + 26;
    y += 24;
    pen.text(ix, lineBaseline(y, 22), ellipsize(pen, title, 22, true, innerW), 22, kText, true);
    y += 22 * 1.45 + 16;
    pen.text(ix, centerBaseline(y, 16 * 1.6, 16), t.appsConfirmLead, 16, kTextLabel);
    y += 16 * 1.6 + 12;
    // 実行するコマンド（等幅。入りきらなければ後ろを …）
    fillRounded(pen, ix, y, innerW, 56, 14, kDiagram);
    strokeRounded(pen, ix, y, innerW, 56, 14, kDivider, 2);
    pen.textMono(ix + 18, centerBaseline(y, 56, 17), ellipsizeMono(pen, command, 17, innerW - 36), 17, kText);
    y += 56 + 16;
    for (const char* line : bullets) {
        drawDisc(pen.cr, ix + 4, y + 15 * 1.7 / 2, 4, kAccent);
        pen.text(ix + 18, centerBaseline(y, 15 * 1.7, 15), ellipsize(pen, line, 15, false, innerW - 18), 15, kTextLabel);
        y += 15 * 1.7;
    }
    if (error != nullptr) {
        y += 8;
        pen.text(ix, centerBaseline(y, 15 * 1.6, 15), ellipsize(pen, error, 15, true, innerW), 15, kDanger, true);
        y += 15 * 1.6;
    }
    y += 20;
    // ［やめる］［Konsole で開く］（右寄せ）。メニューの Konsole がまだ開いていれば開けない
    const PanelAction launch {K::AppsConfirmLaunch, 0, 0, ""};
    const PanelAction cancel {K::AppsConfirmCancel, 0, 0, ""};
    const bool busy = unknownKey || (app == nullptr && view.menuBusy);
    const double lw = pen.measure(t.appsConfirmLaunch, 18, true) + 52;
    const double lx = ix + innerW - lw;
    fillRounded(pen, lx, y, lw, 56, 16, busy ? kControl : (isHot(launch) ? kAccentPressed : kAccent));
    textCentered(pen, lx + lw / 2, centerBaseline(y, 56, 18), t.appsConfirmLaunch, 18, busy ? kTextDisabled : kOnAccent, true);
    addButton(launch, lx, y, lw, 56, !busy);
    const double cw = pen.measure(t.appsConfirmCancel, 18, true) + 44;
    const double cx = lx - 12 - cw;
    fillRounded(pen, cx, y, cw, 56, 16, isHot(cancel) ? kControlHover : kControl);
    strokeRounded(pen, cx, y, cw, 56, 16, kBorder, 2);
    textCentered(pen, cx + cw / 2, centerBaseline(y, 56, 18), t.appsConfirmCancel, 18, kText, true);
    addButton(cancel, cx, y, cw, 56);
}

void SettingsPanel::render(const Config& config, const PanelView& view) {
    const Pen pen {cr_, &fonts_};
    const UiText& t = uiText(config.language);
    buttons_.clear();
    cairo_save(cr_);
    cairo_set_operator(cr_, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr_);
    cairo_restore(cr_);
    fillRounded(pen, 0, 0, kW, kH, 24, kBg);

    lastCommands_ = allCommands(config, view);
    drawHeader(pen, t, config, view);
    switch (tab_) {
        case PanelTab::Assign: drawAssignTab(pen, t, config, view); break;
        case PanelTab::Brightness: drawBrightnessTab(pen, t, config, view); break;
        case PanelTab::Press: drawPressTab(pen, t, config, view); break;
        case PanelTab::Commands: drawCommandsTab(pen, t, config); break;
        case PanelTab::Display: drawDisplayTab(pen, t, config); break;
        case PanelTab::App: drawAppTab(pen, t, config, view); break;
    }
    // いちばん下の 1 行
    const std::string footer = std::string(t.footerSaved) + (config.runInBackground ? t.footerBgOn : t.footerBgOff);
    pen.text(kPad, lineBaseline(kH - kPad - kFooterLineH, 14), footer, fitSize(pen, footer, 14, 11, kContentW, false),
             kTextMuted);
    cairo_surface_flush(surface_);
}

const std::vector<uint8_t>& SettingsPanel::toRgba() {
    surfaceToRgba(surface_, rgba_);
    return rgba_;
}

bool SettingsPanel::writePng(const std::string& path) const {
    return cairo_surface_write_to_png(surface_, path.c_str()) == CAIRO_STATUS_SUCCESS;
}

void renderThumbnail(const FontSet& fonts, int size, std::vector<uint8_t>& rgba, const std::string& pngPath) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t* cr = cairo_create(surface);
    const Pen pen {cr, &fonts};
    const double s = size / 256.0;

    // 地と内側の縁
    pen.color(kBg);
    pen.roundedRect(8 * s, 8 * s, 240 * s, 240 * s, 48 * s);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
    cairo_set_line_width(cr, 2 * s);
    pen.roundedRect(9 * s, 9 * s, 238 * s, 238 * s, 47 * s);
    cairo_stroke(cr);

    // ボタン（カードの地にピンクの枠）と、押した回数の丸 3 つ
    pen.color(kCard);
    pen.roundedRect(52 * s, 44 * s, 152 * s, 104 * s, 52 * s);
    cairo_fill(cr);
    strokeRounded(pen, 52 * s, 44 * s, 152 * s, 104 * s, 52 * s, kAccent, 8 * s);
    for (int i = 0; i < 3; ++i) drawDisc(cr, (98 + i * 30) * s, 96 * s, 11 * s, kAccent);

    const double w = pen.measure("aux", 64 * s, true);
    pen.text((size - w) / 2, 222 * s, "aux", 64 * s, kText, true);

    cairo_surface_flush(surface);
    surfaceToRgba(surface, rgba);
    if (!pngPath.empty()) cairo_surface_write_to_png(surface, pngPath.c_str());
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}
