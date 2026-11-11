// 設定の読み書きと変更の監視の実装。
#include "config.h"

#include "commands.h"
#include "i18n.h"
#include "json.h"
#include "runtime.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

/**
 * アクションの種類の一覧（設定パネルの選択肢の順）。種類を足すときはここに 1 行足す。
 * 古い echo_cancel_toggle（version 1）はここに無い（読むときに none にする）。
 */
const std::vector<ActionSpec> kActionSpecs = {
    {ActionType::MicMuteToggle, "mic_mute_toggle", false},
    {ActionType::BrightnessCycle, "brightness_cycle", true},
    {ActionType::RoomViewToggle, "room_view_toggle", true},
    {ActionType::Screenshot, "screenshot", true},
    {ActionType::PerfOverlayToggle, "perf_overlay_toggle", false},
    {ActionType::DashboardOpen, "dashboard_open", true},
    {ActionType::Recenter, "recenter", true},
    {ActionType::OriginalAux, "original_aux", false},
    {ActionType::MediaPlayPause, "media_play_pause", false},
    {ActionType::MediaNext, "media_next", false},
    {ActionType::MediaPrevious, "media_previous", false},
    {ActionType::EyeRecenter, "eye_recenter", false},
    {ActionType::EyeFit, "eye_fit", false},
    {ActionType::EyeRelearn, "eye_relearn", false},
    {ActionType::Command, "command", false},
    {ActionType::None, "none", false},
};

/**
 * タップの割り当ての設定ファイルでのキー。
 * @param index 0 が 2 回タップ、1 が 3 回タップ
 * @return "tap_2" / "tap_3"
 */
std::string tapKey(int index) {
    return "tap_" + std::to_string(index + 2);
}

/** version 1 にあって、もう無いアクションの名前。読むときは none にする。 */
constexpr const char* kRemovedEchoCancel = "echo_cancel_toggle";

/**
 * オブジェクトのメンバーのうち、知っているキー以外を warnings に入れる。
 * @param object 調べるオブジェクト
 * @param known 知っているキーの一覧
 * @param prefix 表示用のキーの前置き（"sound." など）
 * @param warnings 追加先
 */
void warnUnknownKeys(const JsonValue& object, const std::vector<std::string>& known, const std::string& prefix,
                     std::vector<std::string>& warnings) {
    for (const auto& member : object.members) {
        if (std::find(known.begin(), known.end(), member.first) == known.end()) {
            warnings.push_back("知らないキーです: " + prefix + member.first);
        }
    }
}

/**
 * 整数のキーを読んで範囲に収める。キーが無ければ何もしない。
 * @param object 読むオブジェクト
 * @param key キー
 * @param minValue 最小値
 * @param maxValue 最大値
 * @param target 書き込み先
 * @param warnings 型違い・範囲外のときの追加先
 */
void readInt(const JsonValue& object, const char* key, int minValue, int maxValue, int& target,
             std::vector<std::string>& warnings) {
    const JsonValue* value = object.get(key);
    if (value == nullptr) return;
    if (!value->isNumber()) {
        warnings.push_back(std::string(key) + " は数値で書いてください");
        return;
    }
    const double clamped = std::clamp(value->number, static_cast<double>(minValue), static_cast<double>(maxValue));
    if (clamped != value->number) {
        warnings.push_back(std::string(key) + " は範囲外なので " + std::to_string(static_cast<int>(clamped)) +
                           " にしました");
    }
    target = static_cast<int>(std::lround(clamped));
}

/**
 * 真偽値のキーを読む。キーが無ければ何もしない。
 * @param object 読むオブジェクト
 * @param key キー
 * @param target 書き込み先
 * @param warnings 型違いのときの追加先
 * @param prefix 表示用のキーの前置き
 */
void readBool(const JsonValue& object, const char* key, bool& target, std::vector<std::string>& warnings,
              const std::string& prefix = "") {
    const JsonValue* value = object.get(key);
    if (value == nullptr) return;
    if (!value->isBool()) {
        warnings.push_back(prefix + key + " は true か false で書いてください");
        return;
    }
    target = value->boolean;
}

/**
 * 文字列のキーを読む。キーが無ければ何もしない。
 * @param object 読むオブジェクト
 * @param key キー
 * @param target 書き込み先
 * @param warnings 型違いのときの追加先
 * @param prefix 表示用のキーの前置き
 */
void readString(const JsonValue& object, const char* key, std::string& target, std::vector<std::string>& warnings,
                const std::string& prefix = "") {
    const JsonValue* value = object.get(key);
    if (value == nullptr) return;
    if (!value->isString()) {
        warnings.push_back(prefix + key + " は文字列で書いてください");
        return;
    }
    target = value->text;
}

/**
 * 数値の配列を読む。
 * @param value 配列の値
 * @param out 読めたときの書き込み先
 * @return 空でない数値の配列なら true
 */
bool readNumberArray(const JsonValue& value, std::vector<double>& out) {
    if (value.type != JsonValue::Type::Array || value.items.empty()) return false;
    std::vector<double> read;
    for (const auto& item : value.items) {
        if (!item.isNumber()) return false;
        read.push_back(item.number);
    }
    out = read;
    return true;
}

/**
 * 1 つの割り当てを読む。"mic_mute_toggle" のような文字列だけでも、{"type": ..., ...} でも書ける。
 * 読めなければ warnings に入れて、前の値（既定の割り当て）のままにする。
 * @param value 割り当ての値
 * @param key 表示用のキー（"actions.press_1" など）
 * @param action 書き込み先
 * @param oldLevels version 1 の levels（brightness_cycle に書いてあれば、最初のものを入れる）
 * @param warnings 追加先
 */
void readAction(const JsonValue& value, const std::string& key, Action& action, std::vector<double>& oldLevels,
                std::vector<CommandDef>& commands, std::vector<std::string>& warnings) {
    Action parsed;
    std::string typeName;
    const JsonValue* levels = nullptr;
    std::vector<std::string> oldArgv;  // version 1・2 の command の argv
    bool haveOldArgv = false;
    if (value.isString()) {
        typeName = value.text;
    } else if (value.isObject()) {
        warnUnknownKeys(value, {"type", "levels", "argv", "command"}, key + ".", warnings);
        const JsonValue* type = value.get("type");
        if (type == nullptr || !type->isString()) {
            warnings.push_back(key + ".type を文字列で書いてください（前の割り当てのままにします）");
            return;
        }
        typeName = type->text;
        levels = value.get("levels");
        if (const JsonValue* command = value.get("command")) {
            if (command->isString()) {
                parsed.commandId = command->text;
            } else {
                warnings.push_back(key + ".command はコマンドの id（文字列）で書いてください");
            }
        }
        if (const JsonValue* argv = value.get("argv")) {
            bool ok = argv->type == JsonValue::Type::Array;
            for (const auto& item : argv->items) {
                if (!item.isString()) {
                    ok = false;
                    break;
                }
                oldArgv.push_back(item.text);
            }
            if (!ok) {
                oldArgv.clear();
                warnings.push_back(key + ".argv は [\"コマンド\", \"引数\", ...] のような文字列の配列で書いてください");
            }
            haveOldArgv = true;
        }
    } else {
        warnings.push_back(key + " は \"mic_mute_toggle\" のような文字列か {\"type\": ...} で書いてください");
        return;
    }
    if (typeName == kRemovedEchoCancel) {
        warnings.push_back(key + " の echo_cancel_toggle は無くなったので none（何もしない）として読みます");
        action = Action();
        return;
    }
    if (!parseActionType(typeName, parsed.type)) {
        warnings.push_back(key + " の知らない種類です: " + typeName + "（前の割り当てのままにします）");
        return;
    }
    if (levels != nullptr) {
        // version 1 の書き方。全体の brightness_levels が無いときだけ使う（呼び出し側で決める）
        std::vector<double> read;
        if (parsed.type != ActionType::BrightnessCycle) {
            warnings.push_back(key + ".levels は brightness_cycle のときだけ使います");
        } else if (!readNumberArray(*levels, read)) {
            warnings.push_back(key + ".levels は [100, 60] のような数値（%）の配列で書いてください");
        } else if (oldLevels.empty()) {
            oldLevels = read;
        }
    }
    if (parsed.type == ActionType::Command && parsed.commandId.empty() && haveOldArgv && !oldArgv.empty() &&
        !oldArgv[0].empty()) {
        // version 1・2 の書き方: argv を commands に移して id で参照する（同じ argv がもうあればそれを使う）
        const auto same = std::find_if(commands.begin(), commands.end(),
                                       [&](const CommandDef& c) { return c.argv == oldArgv; });
        if (same != commands.end()) {
            parsed.commandId = same->id;
        } else {
            CommandDef command;
            command.id = nextCommandId(commands);
            const size_t slash = oldArgv[0].find_last_of('/');
            command.name = slash == std::string::npos ? oldArgv[0] : oldArgv[0].substr(slash + 1);
            command.argv = oldArgv;
            commands.push_back(command);
            parsed.commandId = command.id;
        }
        warnings.push_back(key + " の argv は commands に移しました（id " + parsed.commandId + "）");
    }
    if (parsed.type == ActionType::Command && parsed.commandId.empty()) {
        warnings.push_back(key + " は command なので、command にコマンドの id を書いてください（何もしない割り当てにします）");
        parsed.type = ActionType::None;
    }
    action = parsed;
}

/**
 * 数値を JSON 用の短い文字列にする。
 * @param value 値
 * @return 文字列（例: 100、62.5）
 */
std::string jsonNumber(double value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.6g", value);
    return text;
}

/**
 * 文字列を JSON の "..." にする（制御文字は \uXXXX）。
 * @param text 文字列
 * @return エスケープ済みの "..."
 */
std::string jsonString(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (u < 0x20) {
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "\\u%04x", u);
            out += buffer;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

/**
 * 1 つの割り当てを JSON にする。
 * @param action 割り当て
 * @return { "type": ... }（command なら argv も）
 */
std::string actionJson(const Action& action) {
    std::string out = "{ \"type\": " + jsonString(actionTypeName(action.type));
    if (action.type == ActionType::Command) out += ", \"command\": " + jsonString(action.commandId);
    return out + " }";
}

/**
 * 登録したコマンドの一覧を読む（id が無い・重なる・フォルダの id の形・argv が空のものは読み飛ばす）。
 * @param value commands の値
 * @param commands 書き込み先
 * @param warnings 知らせの追加先
 */
void readCommands(const JsonValue& value, std::vector<CommandDef>& commands, std::vector<std::string>& warnings) {
    if (value.type != JsonValue::Type::Array) {
        warnings.push_back("commands は [{\"id\": ..., \"name\": ..., \"argv\": [...]}, ...] で書いてください");
        return;
    }
    for (size_t i = 0; i < value.items.size(); ++i) {
        const JsonValue& item = value.items[i];
        const std::string key = "commands[" + std::to_string(i) + "]";
        if (!item.isObject()) {
            warnings.push_back(key + " は {\"id\": ..., \"name\": ..., \"argv\": [...]} で書いてください");
            continue;
        }
        warnUnknownKeys(item, {"id", "name", "argv"}, key + ".", warnings);
        CommandDef command;
        const JsonValue* id = item.get("id");
        const JsonValue* name = item.get("name");
        const JsonValue* argv = item.get("argv");
        bool ok = id != nullptr && id->isString() && !id->text.empty() &&
                  id->text.rfind(kFolderCommandPrefix, 0) != 0 && argv != nullptr &&
                  argv->type == JsonValue::Type::Array && !argv->items.empty();
        if (ok) {
            command.id = id->text;
            command.name = name != nullptr && name->isString() ? name->text : id->text;
            for (const auto& arg : argv->items) {
                if (!arg.isString()) ok = false;
                command.argv.push_back(arg.text);
            }
            ok = ok && !command.argv[0].empty();
        }
        const bool duplicate = ok && std::any_of(commands.begin(), commands.end(),
                                                 [&](const CommandDef& c) { return c.id == command.id; });
        if (!ok || duplicate) {
            warnings.push_back(key + (duplicate ? " の id が重なっているので読み飛ばします"
                                                 : " は id（\"dir:\" で始まらない文字列）と、空でない argv が要るので読み飛ばします"));
            continue;
        }
        commands.push_back(command);
    }
}

/**
 * フォルダを（親も含めて）作る。新しく作るフォルダは 0700（設定にはコマンドの引数が入るので、ほかのユーザーから見せない）。
 * 元からあるフォルダの権限は変えない。
 * @param dir 作るフォルダ
 * @return できた（元からあった）なら true
 */
bool makeDirectories(const std::string& dir) {
    for (size_t pos = 1; pos <= dir.size(); ++pos) {
        if (pos != dir.size() && dir[pos] != '/') continue;
        const std::string part = dir.substr(0, pos);
        if (::mkdir(part.c_str(), 0700) != 0 && errno != EEXIST) return false;
    }
    return true;
}

}  // namespace

std::string expandHome(const std::string& path) {
    if (path.empty() || path[0] != '~' || (path.size() > 1 && path[1] != '/')) return path;
    const char* home = std::getenv("HOME");
    return std::string(home != nullptr ? home : "") + path.substr(1);
}

Config::Config() : language(systemLanguage()), commandFolder(commandFolderFor(defaultConfigPath())) {
    press[0].type = ActionType::MicMuteToggle;
    press[1].type = ActionType::BrightnessCycle;
    press[2].type = ActionType::RoomViewToggle;
    press[3].type = ActionType::Screenshot;
    press[4].type = ActionType::PerfOverlayToggle;
    longPress.type = ActionType::DashboardOpen;
}

const std::vector<ActionSpec>& actionSpecs() {
    return kActionSpecs;
}

const ActionSpec& actionSpec(ActionType type) {
    for (const auto& spec : kActionSpecs) {
        if (spec.type == type) return spec;
    }
    return kActionSpecs.back();
}

const char* actionTypeName(ActionType type) {
    return actionSpec(type).name;
}

bool parseActionType(const std::string& name, ActionType& type) {
    for (const auto& spec : kActionSpecs) {
        if (name == spec.name) {
            type = spec.type;
            return true;
        }
    }
    return false;
}

bool normalizeBrightnessLevels(std::vector<double>& levels, std::vector<std::string>* warnings) {
    std::vector<double> fixed;
    bool changed = false;
    for (const double value : levels) {
        const double clamped = std::clamp(value, kMinBrightnessPct, kMaxBrightnessPct);
        const double rounded = std::round(clamped / kBrightnessStepPct) * kBrightnessStepPct;
        if (rounded != value) changed = true;
        fixed.push_back(rounded);
    }
    if (static_cast<int>(fixed.size()) < kMinBrightnessLevels) {
        if (warnings != nullptr) warnings->push_back("brightness_levels は 2〜4 段で書いてください（既定の段を使います）");
        return false;
    }
    if (static_cast<int>(fixed.size()) > kMaxBrightnessLevels) {
        fixed.resize(kMaxBrightnessLevels);
        if (warnings != nullptr) warnings->push_back("brightness_levels は 4 段までなので、5 段目からは使いません");
    }
    if (changed && warnings != nullptr) {
        warnings->push_back("brightness_levels は 10〜125 の 5 刻みに直しました");
    }
    levels = fixed;
    return true;
}

std::string defaultConfigPath() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xdg != nullptr && xdg[0] != '\0') {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        base = std::string(home != nullptr ? home : ".") + "/.config";
    }
    return base + "/frame-aux-shortcuts/config.json";
}

bool parseConfigText(const std::string& text, Config& out, std::vector<std::string>& warnings, std::string& error) {
    JsonValue root;
    if (!parseJson(text, root, error)) return false;
    if (!root.isObject()) {
        error = "一番外側は { ... } にしてください";
        return false;
    }

    Config config;  // 書かれていない項目は既定値
    warnUnknownKeys(root,
                    {"version", "long_press_ms", "multi_press_gap_ms", "brightness_levels", "sound", "hud",
                     "screenshot_dir", "screenshot_to_steam", "run_in_background", "original_aux_on_head_pointer", "language",
                     "update_check", "commands", "command_order", "actions"},
                    "", warnings);
    if (const JsonValue* version = root.get("version")) {
        if (!version->isNumber()) {
            warnings.push_back("version は数値で書いてください");
        } else if (version->number > kConfigVersion) {
            warnings.push_back("version " + std::to_string(static_cast<int>(version->number)) +
                               " はこの版より新しい形です。分かる項目だけ読みます");
        }
    }
    readInt(root, "long_press_ms", kMinLongPressMs, kMaxLongPressMs, config.longPressMs, warnings);
    readInt(root, "multi_press_gap_ms", kMinMultiPressGapMs, kMaxMultiPressGapMs, config.multiPressGapMs, warnings);

    bool haveLevels = false;
    if (const JsonValue* levels = root.get("brightness_levels")) {
        std::vector<double> read;
        if (!readNumberArray(*levels, read)) {
            warnings.push_back("brightness_levels は [100, 60, 40] のような数値（%）の配列で書いてください");
        } else if (normalizeBrightnessLevels(read, &warnings)) {
            config.brightnessLevels = read;
            haveLevels = true;
        }
    }

    if (const JsonValue* sound = root.get("sound")) {
        if (sound->isObject()) {
            warnUnknownKeys(*sound, {"enabled", "success", "failure"}, "sound.", warnings);
            readBool(*sound, "enabled", config.soundEnabled, warnings, "sound.");
            readString(*sound, "success", config.successSound, warnings, "sound.");
            readString(*sound, "failure", config.failureSound, warnings, "sound.");
        } else {
            warnings.push_back("sound は {\"enabled\": ..., \"success\": ..., \"failure\": ...} で書いてください");
        }
    }
    if (const JsonValue* hud = root.get("hud")) {
        if (hud->isObject()) {
            warnUnknownKeys(*hud, {"enabled", "mute_badge"}, "hud.", warnings);
            readBool(*hud, "enabled", config.hudEnabled, warnings, "hud.");
            readBool(*hud, "mute_badge", config.muteBadge, warnings, "hud.");
        } else {
            warnings.push_back("hud は {\"enabled\": ..., \"mute_badge\": ...} で書いてください");
        }
    }
    readString(root, "screenshot_dir", config.screenshotDir, warnings);
    if (config.screenshotDir.empty()) config.screenshotDir = kDefaultScreenshotDir;
    readBool(root, "screenshot_to_steam", config.screenshotToSteam, warnings);
    readBool(root, "run_in_background", config.runInBackground, warnings);
    readBool(root, "original_aux_on_head_pointer", config.originalAuxOnHeadPointer, warnings);
    readBool(root, "update_check", config.updateCheck, warnings);
    if (const JsonValue* language = root.get("language")) {
        if (!language->isString() || !parseLanguage(language->text, config.language)) {
            warnings.push_back("language は \"ja\" か \"en\" で書いてください（システムの言語にします）");
        }
    }

    if (const JsonValue* commands = root.get("commands")) readCommands(*commands, config.commands, warnings);
    if (const JsonValue* order = root.get("command_order")) {
        bool ok = order->type == JsonValue::Type::Array;
        for (const auto& item : order->items) {
            if (!item.isString()) {
                ok = false;
                break;
            }
            config.commandOrder.push_back(item.text);
        }
        if (!ok) {
            config.commandOrder.clear();
            warnings.push_back("command_order はコマンドの id（文字列）の配列で書いてください");
        }
    }

    std::vector<double> oldLevels;  // version 1 の、アクションの中の levels
    if (const JsonValue* actions = root.get("actions")) {
        if (actions->isObject()) {
            std::vector<std::string> known = {"long_press"};
            for (int i = 1; i <= kMaxPressCount; ++i) known.push_back("press_" + std::to_string(i));
            for (int i = 0; i < kTapSlotCount; ++i) known.push_back(tapKey(i));
            warnUnknownKeys(*actions, known, "actions.", warnings);
            for (int i = 1; i <= kMaxPressCount; ++i) {
                const std::string key = "press_" + std::to_string(i);
                if (const JsonValue* value = actions->get(key)) {
                    readAction(*value, "actions." + key, config.press[i - 1], oldLevels, config.commands, warnings);
                }
            }
            if (const JsonValue* value = actions->get("long_press")) {
                readAction(*value, "actions.long_press", config.longPress, oldLevels, config.commands, warnings);
            }
            // ヘッドセットのタップ（0.2.1 までの設定には無い。無ければ none のまま）
            for (int i = 0; i < kTapSlotCount; ++i) {
                if (const JsonValue* value = actions->get(tapKey(i))) {
                    readAction(*value, "actions." + tapKey(i), config.tap[i], oldLevels, config.commands, warnings);
                }
            }
        } else {
            warnings.push_back("actions は {\"press_1\": ..., \"long_press\": ...} で書いてください");
        }
    }
    if (!oldLevels.empty()) {
        if (haveLevels) {
            warnings.push_back("アクションの中の levels は、brightness_levels があるので使いません");
        } else if (normalizeBrightnessLevels(oldLevels, &warnings)) {
            config.brightnessLevels = oldLevels;
        }
    }
    out = config;
    return true;
}

std::string configToJson(const Config& config) {
    std::ostringstream out;
    out << "{\n"
        << "  \"version\": " << kConfigVersion << ",\n"
        << "  \"long_press_ms\": " << config.longPressMs << ",\n"
        << "  \"multi_press_gap_ms\": " << config.multiPressGapMs << ",\n"
        << "  \"brightness_levels\": [";
    for (size_t i = 0; i < config.brightnessLevels.size(); ++i) {
        out << (i == 0 ? "" : ", ") << jsonNumber(config.brightnessLevels[i]);
    }
    out << "],\n"
        << "  \"sound\": { \"enabled\": " << (config.soundEnabled ? "true" : "false")
        << ", \"success\": " << jsonString(config.successSound) << ", \"failure\": " << jsonString(config.failureSound)
        << " },\n"
        << "  \"hud\": { \"enabled\": " << (config.hudEnabled ? "true" : "false")
        << ", \"mute_badge\": " << (config.muteBadge ? "true" : "false") << " },\n"
        << "  \"screenshot_dir\": " << jsonString(config.screenshotDir) << ",\n"
        << "  \"screenshot_to_steam\": " << (config.screenshotToSteam ? "true" : "false") << ",\n"
        << "  \"run_in_background\": " << (config.runInBackground ? "true" : "false") << ",\n"
        << "  \"original_aux_on_head_pointer\": " << (config.originalAuxOnHeadPointer ? "true" : "false") << ",\n"
        << "  \"language\": \"" << languageCode(config.language) << "\",\n"
        << "  \"update_check\": " << (config.updateCheck ? "true" : "false") << ",\n"
        << "  \"commands\": [";
    for (size_t i = 0; i < config.commands.size(); ++i) {
        const CommandDef& c = config.commands[i];
        out << (i == 0 ? "\n" : ",\n") << "    { \"id\": " << jsonString(c.id) << ", \"name\": " << jsonString(c.name)
            << ", \"argv\": [";
        for (size_t k = 0; k < c.argv.size(); ++k) out << (k == 0 ? "" : ", ") << jsonString(c.argv[k]);
        out << "] }";
    }
    out << (config.commands.empty() ? "],\n" : "\n  ],\n") << "  \"command_order\": [";
    for (size_t i = 0; i < config.commandOrder.size(); ++i) {
        out << (i == 0 ? "" : ", ") << jsonString(config.commandOrder[i]);
    }
    out << "],\n"
        << "  \"actions\": {\n";
    for (int i = 0; i < kMaxPressCount; ++i) {
        out << "    \"press_" << (i + 1) << "\": " << actionJson(config.press[i]) << ",\n";
    }
    out << "    \"long_press\": " << actionJson(config.longPress);
    for (int i = 0; i < kTapSlotCount; ++i) out << ",\n    \"" << tapKey(i) << "\": " << actionJson(config.tap[i]);
    out << "\n"
        << "  }\n"
        << "}\n";
    return out.str();
}

bool loadConfig(const std::string& path, Config& out, std::vector<std::string>& warnings, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        out = Config();  // ファイルが無いときは既定値
        out.commandFolder = commandFolderFor(path);
        return true;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    if (!parseConfigText(buffer.str(), out, warnings, error)) return false;
    out.commandFolder = commandFolderFor(path);
    return true;
}

std::vector<CommandDef> orderedCommands(const Config& config, const std::vector<CommandDef>& folderCommands) {
    std::vector<CommandDef> rest = config.commands;
    rest.insert(rest.end(), folderCommands.begin(), folderCommands.end());
    std::vector<CommandDef> out;
    for (const std::string& id : config.commandOrder) {
        const auto it = std::find_if(rest.begin(), rest.end(), [&](const CommandDef& c) { return c.id == id; });
        if (it == rest.end()) continue;
        out.push_back(*it);
        rest.erase(it);
    }
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
}

bool setCommandOrder(Config& config, const std::vector<CommandDef>& folderCommands, const std::vector<std::string>& ids) {
    const std::vector<CommandDef> before = orderedCommands(config, folderCommands);
    Config next = config;
    next.commandOrder = ids;
    const std::vector<CommandDef> list = orderedCommands(next, folderCommands);
    if (list == before && config.commandOrder == ids) return false;
    config.commandOrder.clear();
    std::vector<CommandDef> commands;
    for (const CommandDef& command : list) {
        config.commandOrder.push_back(command.id);
        if (!command.fromFolder) commands.push_back(command);
    }
    config.commands = commands;
    return true;
}

bool removeCommand(Config& config, const std::string& id) {
    const auto it = std::find_if(config.commands.begin(), config.commands.end(),
                                 [&](const CommandDef& c) { return c.id == id; });
    if (it == config.commands.end()) return false;
    config.commands.erase(it);
    config.commandOrder.erase(std::remove(config.commandOrder.begin(), config.commandOrder.end(), id),
                              config.commandOrder.end());
    for (int slot = 0; slot < kSlotCount; ++slot) {
        Action& action = config.slot(slot);
        if (action.type == ActionType::Command && action.commandId == id) action = Action();
    }
    return true;
}

bool saveCommand(Config& config, const std::string& id, const std::string& name, const std::string& commandLine,
                 std::string& savedId, std::string& error) {
    const auto trim = [](const std::string& text) {
        const size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return std::string();
        return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    };
    const std::string trimmedName = trim(name);
    std::vector<std::string> argv;
    if (trimmedName.empty()) {
        error = "名前が空です";
        return false;
    }
    if (!splitCommandLine(commandLine, argv, error)) return false;
    if (argv.empty()) {
        error = "コマンドが空です";
        return false;
    }
    if (id.empty()) {
        CommandDef command;
        command.id = nextCommandId(config.commands);
        command.name = trimmedName;
        command.argv = argv;
        config.commands.push_back(command);
        savedId = command.id;
        return true;
    }
    for (CommandDef& command : config.commands) {
        if (command.id != id) continue;
        command.name = trimmedName;
        command.argv = argv;
        savedId = id;
        return true;
    }
    error = "コマンド " + id + " がありません";
    return false;
}

bool saveConfig(const std::string& path, const Config& config, std::string& error) {
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0 && !makeDirectories(path.substr(0, slash))) {
        error = "フォルダを作れません: " + std::string(std::strerror(errno));
        return false;
    }
    // 途中で止まっても壊れたファイルが残らないよう、一時ファイル（0600・O_EXCL・O_NOFOLLOW）に書いて fsync してから置き換える。
    // コマンドの引数（秘密の値が入りうる）をほかのユーザーから読ませないため、置き換わった設定ファイルは 0600 になる
    return writeFilePrivately(path, configToJson(config), error);
}

ConfigWatcher::ConfigWatcher(std::string path) : path_(std::move(path)) {}

void ConfigWatcher::statFile(bool& exists, struct timespec& mtime, off_t& size) const {
    struct stat st {};
    exists = ::stat(path_.c_str(), &st) == 0;
    mtime = exists ? st.st_mtim : timespec{};
    size = exists ? st.st_size : 0;
}

bool ConfigWatcher::loadAndLog(Config& config) const {
    std::vector<std::string> warnings;
    std::string error;
    Config loaded;
    if (!loadConfig(path_, loaded, warnings, error)) {
        std::fprintf(stderr, "[設定] %s を読めませんでした: %s（前の設定のまま）\n", path_.c_str(), error.c_str());
        return false;
    }
    for (const auto& warning : warnings) std::fprintf(stderr, "[設定] %s\n", warning.c_str());
    config = loaded;
    return true;
}

Config ConfigWatcher::loadInitial() {
    statFile(existed_, mtime_, size_);
    Config config;
    if (existed_) {
        if (loadAndLog(config)) std::fprintf(stderr, "[設定] %s を読みました\n", path_.c_str());
    } else {
        std::fprintf(stderr, "[設定] %s が無いので既定値で動きます\n", path_.c_str());
    }
    return config;
}

bool ConfigWatcher::reloadIfChanged(Config& config) {
    bool exists = false;
    struct timespec mtime {};
    off_t size = 0;
    statFile(exists, mtime, size);
    const bool changed = exists != existed_ || size != size_ || mtime.tv_sec != mtime_.tv_sec ||
                         mtime.tv_nsec != mtime_.tv_nsec;
    if (!changed) return false;
    existed_ = exists;
    mtime_ = mtime;
    size_ = size;
    if (!exists) {
        std::fprintf(stderr, "[設定] %s が消えたので既定値に戻します\n", path_.c_str());
        config = Config();
        return true;
    }
    if (!loadAndLog(config)) return false;
    std::fprintf(stderr, "[設定] 変更を読み直しました\n");
    return true;
}

void ConfigWatcher::noteSaved() {
    statFile(existed_, mtime_, size_);
}
