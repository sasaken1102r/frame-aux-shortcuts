// 設定（~/.config/frame-aux-shortcuts/config.json）の読み書きと変更の監視。
// 本体（frame-aux-shortcuts）は読むだけ。書くのは設定パネル（frame-aux-shortcuts-panel）だけ。
#pragma once

#include "commands.h"
#include "config_types.h"

#include <sys/types.h>

#include <ctime>
#include <string>
#include <vector>

/** 1 つの押し方（aux の押し方かヘッドセットのタップ）に割り当てるアクション。 */
struct Action {
    ActionType type = ActionType::None;
    std::string commandId;  ///< command のとき、実行するコマンドの id（設定の commands か、フォルダの "dir:<ファイル名>"）
};

/** アクションの種類の決まりごと（設定ファイルでの名前など）。 */
struct ActionSpec {
    ActionType type;
    const char* name;     ///< 設定ファイルでの名前（"mic_mute_toggle" など）
    bool needsVr;         ///< SteamVR が要る（つながっていなければ失敗）
};

/**
 * アクションの種類の一覧。設定パネルの選択肢もこの順に並ぶ（none は最後）。
 * @return 一覧
 */
const std::vector<ActionSpec>& actionSpecs();

/**
 * 種類の決まりごとを探す。
 * @param type 種類
 * @return 決まりごと（知らない種類なら none のもの）
 */
const ActionSpec& actionSpec(ActionType type);

/** 回数押しの最大（これを超えたら無視する）。 */
constexpr int kMaxPressCount = 5;
/** 長押しの番号。 */
constexpr int kLongPressSlot = kMaxPressCount;
/** ヘッドセットの 2 回タップの番号。 */
constexpr int kTap2Slot = kLongPressSlot + 1;
/** ヘッドセットの 3 回タップの番号。 */
constexpr int kTap3Slot = kTap2Slot + 1;
/** タップの種類の数（2 回・3 回）。 */
constexpr int kTapSlotCount = 2;
/** aux の押し方の数（1〜5 回押しと長押し）。 */
constexpr int kAuxSlotCount = kMaxPressCount + 1;
/**
 * 割り当ての数（aux の押し方とヘッドセットのタップ）。設定パネルでは 0〜4 が回数押し、5 が長押し、
 * 6 が 2 回タップ、7 が 3 回タップ。
 */
constexpr int kSlotCount = kAuxSlotCount + kTapSlotCount;

/**
 * タップの番号か。
 * @param slot 番号
 * @return 2 回・3 回タップなら true
 */
constexpr bool isTapSlot(int slot) {
    return slot == kTap2Slot || slot == kTap3Slot;
}

/** 設定ファイルの形の版（3 からコマンドは commands に登録して id で割り当てる）。 */
constexpr int kConfigVersion = 3;

/**
 * 明るさの段の数と範囲（スライダーの %）。上は SteamVR の設定画面と同じ 125%。
 * 下は設定画面の 40%（DisplayMinUIAnalogGain）ではなく、Frame のドライバーの下限（DisplayMinAnalogGain 0.005、約 9%）の上の 10%。
 */
constexpr int kMinBrightnessLevels = 2;
constexpr int kMaxBrightnessLevels = 4;
constexpr double kMinBrightnessPct = 10.0;
constexpr double kMaxBrightnessPct = 125.0;
constexpr double kBrightnessStepPct = 5.0;

/** 押し方の時間の範囲（ms）。 */
constexpr int kMinLongPressMs = 100;
constexpr int kMaxLongPressMs = 5000;
constexpr int kMinMultiPressGapMs = 100;
constexpr int kMaxMultiPressGapMs = 2000;

/** 既定の成功の音（SteamVR のダッシュボードの音）。 */
constexpr const char* kDefaultSuccessSound = "/opt/steamvr/resources/webinterface/dashboard/sounds/deck_ui_misc_10.wav";
/** 既定の失敗の音。 */
constexpr const char* kDefaultFailureSound =
    "/opt/steamvr/resources/webinterface/dashboard/sounds/activation_change_fail.wav";

/** 既定のスクリーンショットの保存先。 */
constexpr const char* kDefaultScreenshotDir = "~/Pictures/frame-aux-shortcuts";

/**
 * 先頭の ~ をホームフォルダにする（~ で始まらなければそのまま）。
 * @param path パス
 * @return 広げたパス
 */
std::string expandHome(const std::string& path);

/**
 * アプリの設定一式。ファイルが無いときはこの既定値で動く。
 */
struct Config {
    int longPressMs = 600;        ///< 長押しとみなすまでの時間（ms、100〜5000）
    int multiPressGapMs = 350;    ///< 最後に離してから回数を確定するまでの時間（ms、100〜2000）
    std::vector<double> brightnessLevels = {100.0, 60.0, 40.0};  ///< 明るさの段（スライダーの %。2〜4 段、10〜125、5 刻み）
    bool soundEnabled = true;     ///< false で手応えの音を鳴らさない
    std::string successSound = kDefaultSuccessSound;  ///< 成功の音（空なら鳴らさない）
    std::string failureSound = kDefaultFailureSound;  ///< 失敗の音（空なら鳴らさない）
    bool hudEnabled = true;       ///< 押したときに視界にお知らせを出す
    bool muteBadge = true;        ///< ミュート中は視界の上に「マイク ミュート中」を出し続ける
    std::string screenshotDir = kDefaultScreenshotDir;  ///< スクリーンショットの保存先（~ で始めてよい）
    bool screenshotToSteam = true;  ///< 撮れたスクリーンショットを Steam のライブラリにも入れる（SubmitScreenshot）
    bool runInBackground = true;  ///< 設定パネルを閉じても aux を置き換えたままにする（false なら元の動きに戻す）
    /** SteamVR がヘッドセットのカーソルで操作している間（コントローラーが無い間）は、置き換えをやめて aux を SteamVR に返す */
    bool originalAuxOnHeadPointer = false;
    Language language = Language::Ja;  ///< 画面の言語（ファイルに無ければシステムの言語）
    bool updateCheck = true;      ///< 設定パネルが新しい版を確かめる
    std::vector<CommandDef> commands;  ///< 登録したコマンド（フォルダのものは入れない）。並びは commandOrder に合わせる
    std::vector<std::string> commandOrder;  ///< コマンドの並び（id。フォルダのコマンドも入る。無い id は無視）
    std::string commandFolder;    ///< コマンドのフォルダ（設定ファイルと同じフォルダの commands/。ファイルには書かない）
    Action longPress;             ///< 長押し
    Action press[kMaxPressCount]; ///< press[0] が 1 回押し … press[4] が 5 回押し
    Action tap[kTapSlotCount];    ///< tap[0] がヘッドセットの 2 回タップ、tap[1] が 3 回タップ（既定はどちらも none）

    /** 既定の割り当てを入れる（言語はシステムの言語）。 */
    Config();

    /**
     * 押し方の番号から割り当てを引く。
     * @param slot 0〜4 が 1〜5 回押し、5 が長押し、6 が 2 回タップ、7 が 3 回タップ
     * @return 割り当て
     */
    Action& slot(int slot) {
        if (slot >= kTap2Slot) return tap[slot >= kTap3Slot ? 1 : 0];
        return slot >= kLongPressSlot ? longPress : press[slot < 0 ? 0 : slot];
    }
    /** @copydoc slot */
    const Action& slot(int slot) const {
        if (slot >= kTap2Slot) return tap[slot >= kTap3Slot ? 1 : 0];
        return slot >= kLongPressSlot ? longPress : press[slot < 0 ? 0 : slot];
    }

    /** @return 2 回・3 回タップのどちらかに割り当てがあれば true（無ければヘッドセットの姿勢を読まない） */
    bool tapsAssigned() const { return tap[0].type != ActionType::None || tap[1].type != ActionType::None; }
};

/**
 * アクションの種類を設定ファイルでの名前にする。
 * @param type 種類
 * @return "mic_mute_toggle" などの名前
 */
const char* actionTypeName(ActionType type);

/**
 * 設定ファイルでの名前からアクションの種類を探す。
 * @param name "mic_mute_toggle" などの名前
 * @param type 見つかったときの書き込み先
 * @return 知っている名前なら true
 */
bool parseActionType(const std::string& name, ActionType& type);

/**
 * 明るさの段を決まりに合わせる（10〜125 に収めて 5 刻みに丸め、2〜4 段にする）。
 * @param levels 段（書き換える）
 * @param warnings 直したときの知らせの追加先（nullptr なら知らせない）
 * @return 使える段になったら true（足りないときは false で、levels は変えない）
 */
bool normalizeBrightnessLevels(std::vector<double>& levels, std::vector<std::string>* warnings);

/**
 * 既定の設定ファイルのパス（$XDG_CONFIG_HOME か ~/.config の下）。
 * @return パス
 */
std::string defaultConfigPath();

/**
 * 設定の JSON 文字列を読む（テスト用にファイルと分けてある）。version 1（明るさの段がアクションの中）・
 * version 2（command の argv がアクションの中。読むときに commands に移して id で参照する形に直す）も読める。
 * @param text JSON
 * @param out 読めたときの書き込み先（書かれていない項目は既定値）
 * @param warnings 知らないキー・型違い・範囲外・古い書き方の知らせの追加先
 * @param error 読めなかったときの理由
 * @return 読めたら true
 */
bool parseConfigText(const std::string& text, Config& out, std::vector<std::string>& warnings, std::string& error);

/**
 * コマンドを消す。消したコマンドを割り当てていた押し方は none にする。
 * @param config 設定
 * @param id 消すコマンドの id
 * @return 消したら true
 */
bool removeCommand(Config& config, const std::string& id);

/**
 * 設定のコマンドとフォルダのコマンドを、並び（commandOrder）の順に 1 つにまとめる。並びに無いものは、設定のコマンド
 * （設定の順）、フォルダのコマンド（名前の順）の順で後ろに足す（フォルダに新しく置いたファイルは最後に来る）。
 * @param config 設定
 * @param folderCommands フォルダのコマンド
 * @return 並べた一覧
 */
std::vector<CommandDef> orderedCommands(const Config& config, const std::vector<CommandDef>& folderCommands);

/**
 * コマンドの並びを決める（設定パネルでドラッグして並び替えたとき）。並びは commandOrder に id で書き、設定の
 * commands もその順に並べ直す。ids に無いコマンドは、orderedCommands と同じく後ろに続く。
 * @param config 設定
 * @param folderCommands フォルダのコマンド
 * @param ids 新しい並び（id）
 * @return 並びが変わったら true
 */
bool setCommandOrder(Config& config, const std::vector<CommandDef>& folderCommands, const std::vector<std::string>& ids);

/**
 * コマンドを足す・書き換える（設定パネルのシートの「保存する」）。
 * @param config 設定
 * @param id 書き換えるコマンドの id（空なら新しく足す）
 * @param name 名前（前後の空白は除く）
 * @param commandLine 実行するコマンドの文字列（splitCommandLine で分ける）
 * @param savedId 保存したコマンドの id の書き込み先
 * @param error 保存できなかった（名前かコマンドが空・" が閉じていない・id が無い）ときの理由
 * @return 保存できたら true
 */
bool saveCommand(Config& config, const std::string& id, const std::string& name, const std::string& commandLine,
                 std::string& savedId, std::string& error);

/**
 * 設定を version 3 の JSON にする（設定パネルが保存するときの形）。
 * @param config 設定
 * @return JSON の文字列（最後に改行）
 */
std::string configToJson(const Config& config);

/**
 * 設定ファイルを読む。ファイルが無ければ既定値で true。
 * @param path ファイルのパス
 * @param out 書き込み先
 * @param warnings 知らせの追加先
 * @param error 読めなかったときの理由
 * @return 読めた（または無かった）なら true
 */
bool loadConfig(const std::string& path, Config& out, std::vector<std::string>& warnings, std::string& error);

/**
 * 設定ファイルに書く（フォルダが無ければ作り、一時ファイルに書いてから置き換える）。
 * @param path ファイルのパス
 * @param config 設定
 * @param error 書けなかったときの理由
 * @return 書けたら true
 */
bool saveConfig(const std::string& path, const Config& config, std::string& error);

/**
 * 設定ファイルの変更を見張って読み直す役（更新時刻と大きさで判断する）。
 */
class ConfigWatcher {
public:
    /** @param path 設定ファイルのパス */
    explicit ConfigWatcher(std::string path);

    /**
     * 最初の読み込み。無ければ既定値、読めなければ既定値でログを出す。
     * @return 設定
     */
    Config loadInitial();

    /**
     * 変わっていれば読み直す。読めなければ前の設定のまま。
     * @param config 今の設定（変わったら書き換える）
     * @return 書き換えたら true
     */
    bool reloadIfChanged(Config& config);

    /** 自分で保存した直後に呼ぶ（自分の書き込みを「変わった」と読み直さない）。 */
    void noteSaved();

    /** @return 設定ファイルのパス */
    const std::string& path() const { return path_; }

private:
    /**
     * ファイルの有無・更新時刻・大きさを調べる。
     * @param exists 有無の書き込み先
     * @param mtime 更新時刻の書き込み先
     * @param size 大きさの書き込み先
     */
    void statFile(bool& exists, struct timespec& mtime, off_t& size) const;

    /**
     * 読んで、知らせをログに出す。
     * @param config 読めたときの書き込み先
     * @return 読めたら true
     */
    bool loadAndLog(Config& config) const;

    std::string path_;
    bool existed_ = false;
    struct timespec mtime_ {};
    off_t size_ = 0;
};
