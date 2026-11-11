// 実機（gpio-keys・SteamVR・PipeWire）無しで動く単体テスト。ctest --test-dir build で動かす。
// 押し方の判定・明るさの計算・設定の読み書きと version 1 からの移行・状態ファイル・外部の出力の読み取り・
// WebSocket のフレーム・スクリーンショットの保存先・文言・視界のお知らせ・設定パネルの操作と当たり判定・
// カーソルの判定（aux を SteamVR に返すか）・ヘッドセットのタップの判定・音楽の操作のアプリの選び方・
// 視点のリセットの判断・目のアクション（frameeyeosc-panel の終了コードの読み方とフィットの切り替え）を確かめる。
#include "brightness.h"
#include "config.h"
#include "draw.h"
#include "eye_actions.h"
#include "head_pointer.h"
#include "hud_message.h"
#include "i18n.h"
#include "keyboard_text.h"
#include "media.h"
#include "osc.h"
#include "panel_state.h"
#include "press_detector.h"
#include "recenter.h"
#include "room_view.h"
#include "runtime.h"
#include "screenshot.h"
#include "settings_panel.h"
#include "status.h"
#include "tap_detector.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace {

int gTotal = 0;
int gFailures = 0;

/**
 * 1 つの確かめの結果を出す。
 * @param what 何を確かめたか
 * @param ok 合っていれば true
 */
void expect(const std::string& what, bool ok) {
    ++gTotal;
    if (!ok) ++gFailures;
    std::printf("%s  %s\n", ok ? "合格  " : "不合格", what.c_str());
}

/**
 * 結果が期待どおりか。
 * @param result 判定の結果
 * @param kind 期待する種類
 * @param count 期待する回数（Count・TooMany のとき）
 * @return 合っていれば true
 */
bool is(const PressResult& result, PressResult::Kind kind, int count = 0) {
    if (result.kind != kind) return false;
    return kind == PressResult::Kind::Count || kind == PressResult::Kind::TooMany ? result.count == count : true;
}

/**
 * 押して離す（押している時間 holdMs）。離したときの結果を返す。
 * @param d 判定
 * @param t 押す時刻（ms）。離した時刻に進める
 * @param holdMs 押している時間
 * @return 押した・離したときに確定した結果（どちらかで確定すればそれ）
 */
PressResult tap(PressDetector& d, int64_t& t, int64_t holdMs = 80) {
    PressResult down = d.onKey(true, t);
    t += holdMs;
    PressResult up = d.onKey(false, t);
    return down.decided() ? down : up;
}

/** 押し方の判定。既定（長押し 600 ms・区切り 350 ms・最大 5 回）。 */
void testPressDetector() {
    {
        PressDetector d;
        int64_t t = 1000;
        expect("1 回押し: 離した直後はまだ確定しない", !tap(d, t).decided());
        expect("1 回押し: 締め切りは離してから 350 ms 後", d.nextDeadlineMs() == t + 350);
        expect("1 回押し: 349 ms では確定しない", !d.onTick(t + 349).decided());
        expect("1 回押し: 350 ms で 1 回として確定", is(d.onTick(t + 350), PressResult::Kind::Count, 1));
        expect("1 回押し: 確定した後は締め切りなし", d.nextDeadlineMs() == -1);
        expect("1 回押し: 確定は 1 回だけ", !d.onTick(t + 2000).decided());
    }
    for (int n = 2; n <= 5; ++n) {
        PressDetector d;
        int64_t t = 0;
        for (int i = 0; i < n; ++i) {
            const bool decided = tap(d, t).decided();
            if (decided) expect(std::to_string(n) + " 回押し: 途中で確定してしまった", false);
            t += 200;  // 区切りより短い間隔で次を押す
        }
        expect(std::to_string(n) + " 回押し: " + std::to_string(n) + " 回として確定",
               is(d.onTick(t - 200 + 350), PressResult::Kind::Count, n));
    }
    {
        PressDetector d;
        int64_t t = 0;
        for (int i = 0; i < 6; ++i) {
            tap(d, t);
            t += 200;
        }
        expect("6 回押し: 無視（TooMany）として確定", is(d.onTick(t + 1000), PressResult::Kind::TooMany, 6));
    }
    {
        PressDetector d;
        int64_t t = 0;
        expect("長押し: 押した瞬間は確定しない", !d.onKey(true, t).decided());
        expect("長押し: 締め切りは押してから 600 ms 後", d.nextDeadlineMs() == 600);
        expect("長押し: 599 ms では確定しない", !d.onTick(599).decided());
        expect("長押し: 600 ms で、離す前に確定", is(d.onTick(600), PressResult::Kind::LongPress));
        expect("長押し: 押している間は締め切りなし", d.nextDeadlineMs() == -1);
        expect("長押し: 押し続けても 2 回目は出ない", !d.onTick(5000).decided());
        expect("長押し: 確定後の離しは数えない", !d.onKey(false, 5100).decided());
        expect("長押し: 離した後も回数は確定しない", !d.onTick(9000).decided() && d.nextDeadlineMs() == -1);
    }
    {
        PressDetector d;
        int64_t t = 0;
        tap(d, t);  // 1 回押して
        t += 100;
        d.onKey(true, t);  // 2 回目を長押し
        expect("押してから長押し: 長押しとして確定（それまでの回数は捨てる）",
               is(d.onTick(t + 600), PressResult::Kind::LongPress));
        d.onKey(false, t + 700);
        expect("押してから長押し: その後に回数は出ない", !d.onTick(t + 5000).decided());
    }
    {
        PressDetector d;
        int64_t t = 0;
        d.onKey(true, 0);
        expect("599 ms で離すと長押しにならない", !d.onKey(false, 599).decided());
        expect("599 ms で離すと 1 回押し", is(d.onTick(599 + 350), PressResult::Kind::Count, 1));
        (void)t;
    }
    {
        // イベントがまとめて届いた（ループが遅れた）ときも、時刻どおりに判定する
        PressDetector d;
        d.onKey(true, 0);
        d.onKey(false, 50);
        const PressResult late = d.onKey(true, 500);  // 区切り（400）を過ぎてから次の押し
        expect("遅れて届いた押し: 前の 1 回が先に確定する", is(late, PressResult::Kind::Count, 1));
        d.onKey(false, 550);
        expect("遅れて届いた押し: 次の押しは新しい 1 回", is(d.onTick(900), PressResult::Kind::Count, 1));
    }
    {
        PressDetector d;
        int64_t t = 0;
        d.onKey(true, 0);
        expect("押したままの重複（value 1 が 2 回）は無視", !d.onKey(true, 100).decided());
        expect("押したままの重複: 長押しの締め切りは最初の押しから", d.nextDeadlineMs() == 600);
        expect("押した記録の無い離しは無視", [] {
            PressDetector e;
            return !e.onKey(false, 10).decided() && e.nextDeadlineMs() == -1;
        }());
        (void)t;
    }
    {
        PressDetector d;
        int64_t t = 0;
        tap(d, t);
        d.reset();
        expect("reset: 途中の回数を捨てる", d.nextDeadlineMs() == -1 && !d.onTick(10000).decided());
    }
    {
        PressDetector::Params params;
        params.longPressMs = 1000;
        params.multiPressGapMs = 200;
        PressDetector d(params);
        int64_t t = 0;
        tap(d, t, 900);
        expect("時間を変えた判定: 900 ms 押しでも長押しにならない（1000 ms）", d.nextDeadlineMs() == t + 200);
        expect("時間を変えた判定: 区切り 200 ms で 1 回", is(d.onTick(t + 200), PressResult::Kind::Count, 1));
    }
}

/** 明るさの計算。 */
void testBrightness() {
    expect("明るさ: 100% → 1.0", std::fabs(brightnessPercentToGain(100) - 1.0) < 1e-9);
    expect("明るさ: 125% → 1.25", std::fabs(brightnessPercentToGain(125) - 1.25) < 1e-9);
    expect("明るさ: 60% → 0.6^2.2", std::fabs(brightnessPercentToGain(60) - std::pow(0.6, 2.2)) < 1e-9);
    expect("明るさ: 40% → 約 0.134", std::fabs(brightnessPercentToGain(40) - 0.1334) < 0.001);
    const std::vector<double> levels = {100, 60};
    expect("明るさ: 今 1.001078（ほぼ 100%）→ 次は 60%", nextBrightnessIndex(levels, 1.001078) == 1);
    expect("明るさ: 今 60% → 次は 100%", nextBrightnessIndex(levels, std::pow(0.6, 2.2)) == 0);
    expect("明るさ: 今 0.5（どちらでもない、60% に近い）→ 次は 100%", nextBrightnessIndex(levels, 0.5) == 0);
    expect("明るさ: 今 0.8（100% に近い）→ 次は 60%", nextBrightnessIndex(levels, 0.8) == 1);
    const std::vector<double> three = {100, 70, 40};
    expect("明るさ: 3 段で今 40% → 最初の 100% に戻る", nextBrightnessIndex(three, brightnessPercentToGain(40)) == 0);
    expect("明るさ: 3 段で今 70% → 40%", nextBrightnessIndex(three, brightnessPercentToGain(70)) == 2);
    expect("明るさ: 1 段だけなら常にその段", nextBrightnessIndex({80}, 1.0) == 0);
    expect("明るさ: 値から % に戻す（60% → 60）", std::fabs(brightnessGainToPercent(std::pow(0.6, 2.2)) - 60) < 1e-6);
    expect("明るさ: 値から % に戻す（1.25 → 125、1.001078 → 約 100）",
           std::fabs(brightnessGainToPercent(1.25) - 125) < 1e-9 && std::lround(brightnessGainToPercent(1.001078)) == 100);
}

/** 設定の読み込み・書き出し・version 1 からの移行。 */
void testConfig() {
    {
        const Config config;
        expect("設定の既定: 1 回 mic_mute_toggle", config.press[0].type == ActionType::MicMuteToggle);
        expect("設定の既定: 2 回 brightness_cycle", config.press[1].type == ActionType::BrightnessCycle);
        expect("設定の既定: 3 回 room_view_toggle", config.press[2].type == ActionType::RoomViewToggle);
        expect("設定の既定: 4 回 screenshot", config.press[3].type == ActionType::Screenshot);
        expect("設定の既定: 5 回 perf_overlay_toggle", config.press[4].type == ActionType::PerfOverlayToggle);
        expect("設定の既定: 長押し dashboard_open", config.longPress.type == ActionType::DashboardOpen);
        expect("設定の既定: 明るさの段 [100, 60, 40]", config.brightnessLevels == std::vector<double>({100, 60, 40}));
        expect("設定の既定: 600 ms / 350 ms", config.longPressMs == 600 && config.multiPressGapMs == 350);
        expect("設定の既定: 音・お知らせ・ミュート中の表示・裏で動く・新しい版の確認がオン",
               config.soundEnabled && config.hudEnabled && config.muteBadge && config.runInBackground &&
                   config.updateCheck);
        expect("設定の既定: スクリーンショットは ~/Pictures/frame-aux-shortcuts",
               config.screenshotDir == "~/Pictures/frame-aux-shortcuts" && config.screenshotToSteam);
        expect("設定の既定: カーソルのときに aux を元に戻すのはオフ", !config.originalAuxOnHeadPointer);
    }
    auto has = [](const std::vector<std::string>& warnings, const std::string& part) {
        for (const auto& w : warnings) {
            if (w.find(part) != std::string::npos) return true;
        }
        return false;
    };
    {
        // version 2
        Config config;
        std::vector<std::string> warnings;
        std::string error;
        const bool ok = parseConfigText(R"({
            "version": 2,
            "long_press_ms": 800,
            "multi_press_gap_ms": 50,
            "brightness_levels": [100, 72, 5, 200, 90],
            "sound": { "enabled": false, "success": "/tmp/a.wav" },
            "hud": { "enabled": false, "mute_badge": false },
            "screenshot_dir": "/tmp/shots",
            "screenshot_to_steam": false,
            "run_in_background": false,
            "language": "en",
            "update_check": false,
            "actions": {
                "press_1": "none",
                "press_2": { "type": "brightness_cycle" },
                "press_3": { "type": "command", "argv": ["notify-send", "やあ"] },
                "press_4": { "type": "command" },
                "press_5": "no_such_action",
                "long_press": { "type": "mic_mute_toggle" },
                "press_6": "none"
            },
            "extra": 1
        })",
                                        config, warnings, error);
        expect("設定 v2: 読める", ok);
        expect("設定 v2: long_press_ms 800", config.longPressMs == 800);
        expect("設定 v2: multi_press_gap_ms は 100 に収める", config.multiPressGapMs == 100);
        expect("設定 v2: 明るさの段は 5 刻み・10〜125 に直して 4 段まで",
               config.brightnessLevels == std::vector<double>({100, 70, 10, 125}));
        expect("設定 v2: 5 段目を使わないことを知らせる", has(warnings, "4 段まで"));
        expect("設定 v2: 音を消せる・パスを変えられる、書いていない failure は既定のまま",
               !config.soundEnabled && config.successSound == "/tmp/a.wav" && config.failureSound == kDefaultFailureSound);
        expect("設定 v2: hud の 2 つ", !config.hudEnabled && !config.muteBadge);
        expect("設定 v2: screenshot_dir・run_in_background・language・update_check",
               config.screenshotDir == "/tmp/shots" && !config.screenshotToSteam && !config.runInBackground && config.language == Language::En &&
                   !config.updateCheck);
        expect("設定 v2: 文字列だけで none", config.press[0].type == ActionType::None);
        expect("設定 v2→3: command の argv を commands に移して id で参照する",
               config.press[2].type == ActionType::Command && config.press[2].commandId == "c1" &&
                   config.commands.size() == 1 && config.commands[0].id == "c1" && config.commands[0].name == "notify-send" &&
                   config.commands[0].argv == std::vector<std::string>({"notify-send", "やあ"}) && has(warnings, "commands に移しました"));
        expect("設定 v2: argv の無い command は none にする", config.press[3].type == ActionType::None);
        expect("設定 v2: 知らない種類は既定の割り当てのまま", config.press[4].type == ActionType::PerfOverlayToggle);
        expect("設定 v2: 長押しを変えられる", config.longPress.type == ActionType::MicMuteToggle);
        expect("設定 v2: 知らないキー（extra・press_6）を知らせる", has(warnings, "extra") && has(warnings, "press_6"));
        expect("設定 v2: 知らない種類を知らせる", has(warnings, "no_such_action"));
    }
    {
        // version 1（今までの形）: 明るさの段はアクションの中、echo_cancel_toggle がある
        Config config;
        std::vector<std::string> warnings;
        std::string error;
        const bool ok = parseConfigText(R"({
            "long_press_ms": 600,
            "multi_press_gap_ms": 350,
            "sound": { "enabled": true },
            "actions": {
                "press_1": { "type": "mic_mute_toggle" },
                "press_2": { "type": "brightness_cycle", "levels": [100, 60] },
                "press_3": "room_view_toggle",
                "press_4": { "type": "echo_cancel_toggle" },
                "press_5": "none",
                "long_press": { "type": "dashboard_open" }
            }
        })",
                                        config, warnings, error);
        expect("設定 v1: 読める", ok);
        expect("設定 v1: アクションの中の levels を明るさの段にする", config.brightnessLevels == std::vector<double>({100, 60}));
        expect("設定 v1: echo_cancel_toggle は none として読む", config.press[3].type == ActionType::None);
        expect("設定 v1: echo_cancel_toggle を知らせる", has(warnings, "echo_cancel_toggle"));
        expect("設定 v1: ほかの割り当てはそのまま",
               config.press[0].type == ActionType::MicMuteToggle && config.press[1].type == ActionType::BrightnessCycle &&
                   config.press[2].type == ActionType::RoomViewToggle && config.press[4].type == ActionType::None &&
                   config.longPress.type == ActionType::DashboardOpen);
        expect("設定 v1: 書いていない新しい項目は既定値",
               config.hudEnabled && config.muteBadge && config.runInBackground && !config.originalAuxOnHeadPointer);
    }
    {
        Config config;
        std::vector<std::string> warnings;
        std::string error;
        parseConfigText(R"({ "brightness_levels": [80, 60],
                             "actions": { "press_2": { "type": "brightness_cycle", "levels": [100, 50] } } })",
                        config, warnings, error);
        expect("設定: brightness_levels があればアクションの levels は使わない",
               config.brightnessLevels == std::vector<double>({80, 60}) && has(warnings, "levels"));
        parseConfigText(R"({ "brightness_levels": [80] })", config, warnings, error);
        expect("設定: 1 段だけなら既定の段", config.brightnessLevels == std::vector<double>({100, 60, 40}));
    }
    {
        // 書き出して読み直すと同じになる（設定パネルの保存）
        Config config;
        config.longPressMs = 900;
        config.multiPressGapMs = 250;
        config.brightnessLevels = {125, 80, 45, 40};
        config.soundEnabled = false;
        config.successSound = "/tmp/a \"b\".wav";
        config.hudEnabled = false;
        config.muteBadge = false;
        config.screenshotDir = "~/Pictures/x";
        config.screenshotToSteam = false;
        config.runInBackground = false;
        config.originalAuxOnHeadPointer = true;
        config.language = Language::En;
        config.updateCheck = false;
        config.commands = {{"c1", "通知 \"テスト\"", {"notify-send", "a\\b", "空白 のある 引数"}, false},
                           {"c7", "Second", {"/bin/true"}, false}};
        config.commandOrder = {"c7", "dir:x.sh", "c1"};
        config.press[0].type = ActionType::Command;
        config.press[0].commandId = "c1";
        config.press[3].type = ActionType::None;
        config.press[4].type = ActionType::OriginalAux;
        config.longPress.type = ActionType::Screenshot;
        const std::string json = configToJson(config);
        Config read;
        std::vector<std::string> warnings;
        std::string error;
        const bool ok = parseConfigText(json, read, warnings, error);
        expect("設定の書き出し: 読み直せる・知らせなし", ok && warnings.empty());
        expect("設定の書き出し: version 3", json.find("\"version\": 3") != std::string::npos);
        expect("設定の書き出し: 読み直すと同じ", configToJson(read) == json);
        expect("設定の書き出し: 中身も同じ",
               read.longPressMs == 900 && read.multiPressGapMs == 250 && read.brightnessLevels == config.brightnessLevels &&
                   !read.soundEnabled && read.successSound == config.successSound && !read.hudEnabled && !read.muteBadge &&
                   read.screenshotDir == "~/Pictures/x" && !read.screenshotToSteam && !read.runInBackground && read.language == Language::En &&
                   !read.updateCheck && read.press[0].type == ActionType::Command && read.press[0].commandId == "c1" &&
                   read.commands == config.commands && read.commandOrder == config.commandOrder &&
                   read.longPress.type == ActionType::Screenshot);
        expect("設定の書き出し: aux の元の動きとカーソルのときの設定",
               read.press[4].type == ActionType::OriginalAux && read.originalAuxOnHeadPointer &&
                   json.find("\"press_5\": { \"type\": \"original_aux\" }") != std::string::npos &&
                   json.find("\"original_aux_on_head_pointer\": true") != std::string::npos);
        // 割り当ては id だけ（argv はアクションに書かない）
        expect("設定の書き出し: 割り当ては { type, command }",
               json.find("\"press_1\": { \"type\": \"command\", \"command\": \"c1\" }") != std::string::npos);
    }
    {
        Config config;
        std::vector<std::string> warnings;
        std::string error;
        expect("設定: 壊れた JSON は読めない", !parseConfigText("{ \"long_press_ms\": ", config, warnings, error) &&
                                                  !error.empty());
        expect("設定: 一番外側が配列なら読めない", !parseConfigText("[1]", config, warnings, error));
    }
    expect("設定: ~ をホームフォルダにする", expandHome("~/a") == std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/a" &&
                                              expandHome("/x/~") == "/x/~" && expandHome("~user/a") == "~user/a");
    expect("設定: 種類の一覧の名前と読み方が対になっている", [] {
        for (const auto& spec : actionSpecs()) {
            ActionType type = ActionType::None;
            if (!parseActionType(spec.name, type) || type != spec.type || std::string(actionTypeName(type)) != spec.name) {
                return false;
            }
        }
        return actionSpecs().size() == 16 && actionSpecs().back().type == ActionType::None;
    }());
    {
        // 0.2.1 で足したもの（version は 3 のまま。書いていなければ既定値）
        Config config;
        std::vector<std::string> warnings;
        std::string error;
        const bool ok = parseConfigText(R"({ "version": 3, "original_aux_on_head_pointer": true,
                                             "actions": { "press_1": "original_aux" } })",
                                        config, warnings, error);
        expect("設定: original_aux と original_aux_on_head_pointer を読める・知らせなし",
               ok && warnings.empty() && config.press[0].type == ActionType::OriginalAux && config.originalAuxOnHeadPointer);
        parseConfigText(R"({ "original_aux_on_head_pointer": "yes" })", config, warnings, error);
        expect("設定: original_aux_on_head_pointer の型違いは既定値（オフ）で知らせる",
               !config.originalAuxOnHeadPointer && has(warnings, "original_aux_on_head_pointer"));
    }
}

/** カーソルの判定と、aux を SteamVR に返すかの決め方。 */
void testHeadPointer() {
    PointerSample sample;
    sample.controllers = 0;
    expect("カーソル: コントローラーが 1 本も無ければカーソル", isHeadPointer(sample));
    sample.controllers = 2;
    sample.dashboardVisible = true;
    sample.dashboardPointer = 1;
    expect("カーソル: コントローラーがポインターならカーソルでない", !isHeadPointer(sample));
    sample.controllers = 1;
    sample.dashboardPointer = 1;  // 電源を切ったコントローラーの番号が残っていても、数で決める
    expect("カーソル: 1 本でもつながっていればカーソルでない", !isHeadPointer(sample));
    sample.controllers = 0;
    expect("カーソル: ポインターの番号が残っていても、0 本ならカーソル", isHeadPointer(sample));

    expect("aux の扱い: パネルを閉じたら戻す設定でパネルが無ければ元のまま",
           decideAuxMode(false, true, true) == AuxMode::Original && decideAuxMode(false, false, false) == AuxMode::Original);
    expect("aux の扱い: 設定がオフならカーソルでもショートカット", decideAuxMode(true, false, true) == AuxMode::Shortcuts);
    expect("aux の扱い: 設定がオンでカーソルなら返す", decideAuxMode(true, true, true) == AuxMode::HandedBack);
    expect("aux の扱い: 設定がオンでもコントローラーならショートカット", decideAuxMode(true, true, false) == AuxMode::Shortcuts);

    HeadPointerTracker tracker;  // 既定: カーソルまで 2 秒・戻るまで 0.5 秒・つないでから 5 秒は待つ
    tracker.reset(0);
    expect("カーソルの落ち着き: つないだ直後はカーソルでない", !tracker.update(true, 0));
    expect("カーソルの落ち着き: つないでから 5 秒はカーソルにしない", !tracker.update(true, 4900));
    expect("カーソルの落ち着き: 5 秒を過ぎてカーソルが続いていればカーソル", tracker.update(true, 5000));
    expect("カーソルの落ち着き: コントローラーが戻ってもすぐは戻さない", tracker.update(false, 6000));
    expect("カーソルの落ち着き: 0.5 秒続いたら戻す", !tracker.update(false, 6500));
    expect("カーソルの落ち着き: 一瞬のカーソルでは切り替えない", !tracker.update(true, 10000) && !tracker.update(false, 11000) &&
                                                                     !tracker.update(true, 11500) && !tracker.update(true, 13400));
    expect("カーソルの落ち着き: 2 秒続いたらカーソル", tracker.update(true, 13500) && tracker.headPointer());
    tracker.reset(20000);
    expect("カーソルの落ち着き: reset で最初からにする", !tracker.headPointer() && !tracker.update(true, 20000));
}

/** 状態ファイル。 */
void testStatus() {
    Status s;
    s.version = "0.2.0";
    s.pid = 1234;
    s.active = true;
    s.vrConnected = true;
    s.lastPress.kind = PressRecord::Kind::Count;
    s.lastPress.count = 2;
    s.lastPress.action = ActionType::BrightnessCycle;
    s.lastPress.ok = true;
    s.lastPress.value = "60";
    s.lastPress.detail = "明るさ \"1.000\" → 60%";
    s.lastPress.at = 1791290640;
    s.micMuted = 1;
    s.brightnessPercent = 60;
    s.roomView = 0;
    s.perfVisible = -1;
    s.perfOverlay = AppState::Stopped;
    s.updatedAt = 1791290641;
    const std::string json = statusToJson(s);
    Status read;
    std::string error;
    expect("状態: 読み直せる", parseStatusText(json, read, error));
    expect("状態: 中身が同じ", statusToJson(read) == json);
    expect("状態: 値", read.pid == 1234 && read.active && read.lastPress.slot() == 1 &&
                           read.lastPress.action == ActionType::BrightnessCycle && read.micMuted == 1 &&
                           read.brightnessPercent == 60 && read.roomView == 0 && read.perfVisible == -1 &&
                           read.perfOverlay == AppState::Stopped);
    {
        // コントローラーが無いので返している（handed_back）も読み書きできる。書いていない古いファイルは false
        Status back;
        back.handedBack = true;
        Status backRead;
        expect("状態: handed_back を読み直せる", parseStatusText(statusToJson(back), backRead, error) && backRead.handedBack &&
                                                    !backRead.active);
        expect("状態: handed_back が無ければ false", !read.handedBack);
    }
    expect("状態: ファイルの形（apps・mic_muted・last_press.kind）",
           json.find("\"apps\": { \"frame-perf-overlay\": \"stopped\" }") != std::string::npos &&
               json.find("\"kind\": \"count\"") != std::string::npos && json.find("\"mic_muted\": true") != std::string::npos);
    Status empty;
    const std::string emptyJson = statusToJson(empty);
    expect("状態: まだ押していなければ last_press は null", emptyJson.find("\"last_press\": null") != std::string::npos);
    Status emptyRead;
    expect("状態: 分からない値は null → -1",
           parseStatusText(emptyJson, emptyRead, error) && emptyRead.micMuted == -1 && emptyRead.brightnessPercent == -1 &&
               emptyRead.lastPress.kind == PressRecord::Kind::None && emptyRead.lastPress.slot() == -1);
    PressRecord longPress;
    longPress.kind = PressRecord::Kind::Long;
    expect("状態: 長押しの番号は 5", longPress.slot() == kLongPressSlot);
    expect("状態: 壊れた JSON は読めない", !parseStatusText("{", read, error));
}

/** 外部の出力の読み取り・プロセスの見分け・room view の送信の形・スクリーンショットの保存先。 */
void testParsers() {
    bool muted = false;
    expect("wpctl: 「Volume: 1.00 [MUTED]」→ ミュート", parseWpctlMuted("Volume: 1.00 [MUTED]\n", muted) && muted);
    expect("wpctl: 「Volume: 0.45」→ ミュートでない", parseWpctlMuted("Volume: 0.45\n", muted) && !muted);
    expect("wpctl: 関係ない出力は読めない", !parseWpctlMuted("Translate ID error\n", muted));

    expect("プロセス: フルパスの argv[0]",
           isCmdlineOf(std::string("/home/steamos/.local/bin/frame-perf-overlay\0--verbose\0", 54), "frame-perf-overlay"));
    expect("プロセス: 名前だけの argv[0]", isCmdlineOf(std::string("frame-perf-overlay\0", 19), "frame-perf-overlay"));
    expect("プロセス: 別のプロセスは違う", !isCmdlineOf(std::string("/usr/bin/bash\0", 14), "frame-perf-overlay"));
    expect("プロセス: 似た名前は違う（本体とパネル）",
           !isCmdlineOf(std::string("frame-aux-shortcuts-panel\0", 26), "frame-aux-shortcuts") &&
               isCmdlineOf(std::string("frame-aux-shortcuts-panel\0", 26), "frame-aux-shortcuts-panel"));

    expect("room view: オンの文", roomViewMailboxMessage(true) ==
                                    "mailbox_send vrcompositor_systemlayer "
                                    "{\"type\":\"set_camera_roomview_enabled\",\"enabled\":true}");
    expect("room view: オフの文", roomViewMailboxMessage(false).find("\"enabled\":false}") != std::string::npos);
    const uint8_t mask[4] = {0x37, 0xfa, 0x21, 0x3d};
    // RFC 6455 5.7 の例: マスク付きの "Hello"
    const std::string hello = buildMaskedTextFrame("Hello", mask);
    const unsigned char expected[] = {0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58};
    expect("WebSocket: RFC 6455 の例と同じフレーム",
           hello == std::string(reinterpret_cast<const char*>(expected), sizeof(expected)));
    const std::string longFrame = buildMaskedTextFrame(std::string(200, 'a'), mask);
    expect("WebSocket: 126 バイト以上は 16 ビットの長さ",
           longFrame.size() == 2 + 2 + 4 + 200 && static_cast<uint8_t>(longFrame[1]) == (0x80 | 126) &&
               static_cast<uint8_t>(longFrame[2]) == 0 && static_cast<uint8_t>(longFrame[3]) == 200);
    expect("WebSocket: 65536 バイト以上は作らない", buildMaskedTextFrame(std::string(70000, 'a'), mask).empty());

    std::tm tm {};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 9;
    tm.tm_mday = 6;
    tm.tm_hour = 9;
    tm.tm_min = 8;
    tm.tm_sec = 1;
    expect("スクリーンショット: ファイル名", screenshotFileName(tm) == "screenshot_2026-10-06_09-08-01.png");
    {
        // 保存先とファイル名（フォルダを作り、同じ名前があれば _2。拡張子なしの名前を RequestScreenshot に渡す）
        char base[] = "/tmp/fas-shot-test-XXXXXX";
        const std::string tmp = mkdtemp(base) != nullptr ? base : "/tmp";
        const std::string dir = tmp + "/a/b";
        ScreenshotPaths paths;
        std::string error;
        const bool ok = prepareScreenshotPaths(dir + "/", tm, paths, error);
        expect("スクリーンショット: フォルダを作って名前を決める",
               ok && access(dir.c_str(), F_OK) == 0 && paths.path == dir + "/screenshot_2026-10-06_09-08-01.png" &&
                   paths.previewBase + ".png" == paths.path && paths.vrBase == dir + "/.screenshot_2026-10-06_09-08-01_vr");
        FILE* f = std::fopen(paths.path.c_str(), "w");
        if (f != nullptr) std::fclose(f);
        ScreenshotPaths second;
        expect("スクリーンショット: 同じ秒の 2 枚目は _2",
               prepareScreenshotPaths(dir, tm, second, error) &&
                   second.path == dir + "/screenshot_2026-10-06_09-08-01_2.png");
        std::remove(paths.path.c_str());
        std::remove(dir.c_str());
        std::remove((tmp + "/a").c_str());
        std::remove(tmp.c_str());
    }
}

/** 文言の表と、視界のお知らせの中身。 */
void testTextAndHud() {
    for (const Language lang : {Language::Ja, Language::En}) {
        const UiText& t = uiText(lang);
        const auto* fields = reinterpret_cast<const char* const*>(&t);
        bool complete = true;
        for (size_t i = 0; i < sizeof(UiText) / sizeof(const char*); ++i) {
            if (fields[i] == nullptr) complete = false;
        }
        expect(std::string("文言: ") + (lang == Language::Ja ? "日本語" : "英語") + "の表に抜けがない", complete);
        for (const auto& spec : actionSpecs()) {
            if (std::string(actionLabel(lang, spec.type)).empty()) complete = false;
        }
        expect(std::string("文言: ") + (lang == Language::Ja ? "日本語" : "英語") + "のアクションの名前", complete);
    }
    expect("文言: 押し方の名前", slotLabel(Language::Ja, 1) == "2 回押し" && slotLabel(Language::Ja, 5) == "長押し" &&
                                    slotLabel(Language::En, 0) == "1 press" && slotLabel(Language::En, 2) == "3 presses");
    expect("文言: スクリーンショットの名前・お知らせ",
           std::string(actionLabel(Language::Ja, ActionType::Screenshot)) == "スクリーンショット" &&
               std::string(uiText(Language::Ja).hudShot) == "スクリーンショットを保存しました" &&
               std::string(uiText(Language::Ja).failShot) == "スクリーンショットを撮れませんでした");

    PressRecord press;
    press.kind = PressRecord::Kind::Count;
    press.count = 1;
    press.action = ActionType::MicMuteToggle;
    press.value = "muted";
    HudToast toast;
    expect("お知らせ: ミュート", makeHudToast(press, Language::Ja, toast) && toast.title == "マイクをミュートしました" &&
                                   toast.sub == "1 回押し" && toast.icon == Icon::MicOff && !toast.error);
    press.value = "unmuted";
    expect("お知らせ: ミュート解除", makeHudToast(press, Language::Ja, toast) && toast.title == "マイクをオンにしました" &&
                                       toast.tone == HudToast::Tone::Success);
    press.count = 2;
    press.action = ActionType::BrightnessCycle;
    press.value = "60";
    expect("お知らせ: 明るさ", makeHudToast(press, Language::Ja, toast) && toast.title == "明るさ 60%" &&
                                 toast.sub == "2 回押し");
    press.count = 3;
    press.action = ActionType::RoomViewToggle;
    press.ok = false;
    press.reason = "room_view_api";
    expect("お知らせ: パススルーの失敗の文言",
           makeHudToast(press, Language::Ja, toast) && toast.error && toast.title == "パススルーを切り替えられません" &&
               toast.sub == "3 回押し・SteamVR の更新で使えなくなったかも");
    press.count = 4;
    press.action = ActionType::Screenshot;
    press.ok = true;
    expect("お知らせ: スクリーンショット", makeHudToast(press, Language::Ja, toast) &&
                                           toast.title == "スクリーンショットを保存しました" && toast.icon == Icon::Camera);
    press.ok = false;
    press.reason = "failed";
    expect("お知らせ: スクリーンショットの失敗",
           makeHudToast(press, Language::Ja, toast) && toast.title == "スクリーンショットを撮れませんでした");
    press.ok = true;
    press.action = ActionType::None;
    expect("お知らせ: none は出さない", !makeHudToast(press, Language::Ja, toast));
    press.kind = PressRecord::Kind::Long;
    press.action = ActionType::DashboardOpen;
    expect("お知らせ: ダッシュボードが開いたときは出さない", !makeHudToast(press, Language::Ja, toast));
    press.kind = PressRecord::Kind::Count;
    press.count = 1;
    press.action = ActionType::OriginalAux;
    expect("お知らせ: aux の元の動きが届いたときは出さない", !makeHudToast(press, Language::Ja, toast));
    press.ok = false;
    press.reason = "failed";
    expect("お知らせ: aux の元の動きの失敗（英語）",
           makeHudToast(press, Language::En, toast) && toast.error && toast.title == "Couldn't send the aux press");
    press.ok = true;
    press.reason.clear();
    {
        const HudToast back = makeHandBackToast(true, Language::Ja);
        const HudToast again = makeHandBackToast(false, Language::Ja);
        expect("お知らせ: カーソルの間 aux を返した・ショートカットに戻した",
               back.title == "aux を従来の動きに戻しました" && back.sub == "コントローラーが接続されていない間" && !back.error &&
                   again.title == "aux のショートカットに戻しました" && again.sub.empty() &&
                   makeHandBackToast(true, Language::En).title == "aux is back to normal");
    }
    press.kind = PressRecord::Kind::TooMany;
    press.count = 6;
    press.action = ActionType::MicMuteToggle;
    expect("お知らせ: 6 回以上は出さない", !makeHudToast(press, Language::Ja, toast));
    press.kind = PressRecord::Kind::Count;
    press.count = 2;
    press.action = ActionType::Command;
    press.ok = true;
    press.value = "osc:/input/Voice";
    press.reason.clear();
    expect("お知らせ: VRChat のミュートを送った",
           makeHudToast(press, Language::Ja, toast) && toast.title == "VRChat にミュートの切り替えを送りました" &&
               toast.sub == "2 回押し" && !toast.error);
    expect("お知らせ: VRChat のミュートを送った（英語）",
           makeHudToast(press, Language::En, toast) && toast.title == "Sent mute toggle to VRChat");
    press.ok = false;
    press.reason = "no_pc";
    expect("お知らせ: つないでいる PC が見つからない",
           makeHudToast(press, Language::Ja, toast) && toast.error && toast.title == "VRChat に送れません" &&
               toast.sub == "2 回押し・つないでいる PC が見つかりません");
    press.ok = true;
    press.value = "osc:/avatar/parameters/Foo";
    expect("お知らせ: ほかの OSC のアドレス", makeHudToast(press, Language::Ja, toast) &&
                                                 toast.title == "OSC を送りました: /avatar/parameters/Foo");
    press.value = "通知";
    expect("お知らせ: ふつうのコマンドは名前", makeHudToast(press, Language::Ja, toast) && toast.title == "通知");
}

/** OSC: パケットの組み方・IP の形・送り先・組み込みのコマンド・/proc/net/udp の読み方・実際に送る。 */
void testOsc() {
    const std::string voice = encodeOscInt("/input/Voice", 1);
    expect("OSC: /input/Voice に 1 は 24 バイト（アドレス 16 ＋ \",i\" 4 ＋ 値 4）",
           voice == std::string("/input/Voice\0\0\0\0,i\0\0\0\0\0\x01", 24));
    expect("OSC: アドレスが 4 の倍数でも NUL を 4 つ足す", encodeOscInt("/abc", 0).size() == 16 &&
                                                             encodeOscInt("/abc", 0).substr(4, 4) == std::string(4, '\0'));
    expect("OSC: 値はビッグエンディアン", encodeOscInt("/a", 0x01020304).substr(8) == "\x01\x02\x03\x04" &&
                                            encodeOscInt("/a", -1).substr(8) == "\xff\xff\xff\xff");

    expect("IP: 正しい形", isValidIpv4("192.168.0.12") && isValidIpv4("0.0.0.0") && isValidIpv4("255.255.255.255"));
    bool bad = false;
    for (const char* text : {"", "256.1.1.1", "1.2.3", "1.2.3.4.5", "01.2.3.4", "1.2.3.4.", ".1.2.3", "a.b.c.d", " 1.2.3.4",
                             "1..2.3", "1.2.3.-4", "1.2.3.4:9000"}) {
        if (isValidIpv4(text)) bad = true;
    }
    expect("IP: 正しくない形は通さない", !bad);

    SendOscCommand target;
    expect("送り先: auto:9000", parseOscTarget("auto:9000", target) && target.autoHost && target.port == 9000);
    expect("送り先: IP とポート", parseOscTarget("192.168.0.12:9001", target) && !target.autoHost &&
                                     target.host == "192.168.0.12" && target.port == 9001);
    expect("送り先: ポートを省くと 9000", parseOscTarget("auto", target) && target.port == 9000);
    expect("送り先: 正しくない書き方は通さない", !parseOscTarget("auto:0", target) && !parseOscTarget("auto:70000", target) &&
                                                    !parseOscTarget("pc.local:9000", target) && !parseOscTarget("auto:", target));

    SendOscCommand command;
    const std::vector<std::string> argv = makeSendOscArgv(true, "", 9000, "/input/Voice");
    expect("組み込み: プリセットの argv",
           argv == std::vector<std::string>({"frame-aux-shortcuts", "--send-osc", "auto:9000", "/input/Voice"}));
    expect("組み込み: プリセットの argv を読める", parseSendOscCommand(argv, command) && command.autoHost &&
                                                      command.port == 9000 && command.address == "/input/Voice");
    std::vector<std::string> split;
    std::string error;
    expect("組み込み: 文字列にして戻しても同じ", splitCommandLine(joinCommandLine(argv), split, error) && split == argv);
    expect("組み込み: 絶対パスでもよい",
           parseSendOscCommand({"/home/steamos/.local/bin/frame-aux-shortcuts", "--send-osc", "192.168.0.12:9000", "/input/Voice"},
                               command) &&
               !command.autoHost && command.host == "192.168.0.12");
    expect("組み込み: ほかのプログラムは組み込みにしない",
           !parseSendOscCommand({"frame-aux-shortcutsx", "--send-osc", "auto:9000", "/input/Voice"}, command) &&
               !parseSendOscCommand({"other", "--send-osc", "auto:9000", "/input/Voice"}, command));
    expect("組み込み: 引数の数・アドレスが違えば組み込みにしない",
           !parseSendOscCommand({"frame-aux-shortcuts", "--send-osc", "auto:9000"}, command) &&
               !parseSendOscCommand({"frame-aux-shortcuts", "--send-osc", "auto:9000", "input"}, command) &&
               !parseSendOscCommand({"frame-aux-shortcuts", "--send-osc", "auto:9000", "/a b"}, command) &&
               !parseSendOscCommand({"frame-aux-shortcuts", "--send-osc", "auto:9000", "/input/Voice", "1"}, command));
    expect("組み込み: アドレスに制御文字・NUL・長すぎは不可",
           !parseSendOscCommand({"frame-aux-shortcuts", "--send-osc", "auto:9000", "/in\x01put"}, command) &&
               !parseSendOscCommand({"frame-aux-shortcuts", "--send-osc", "auto:9000", std::string("/in\0put", 7)}, command) &&
               !parseSendOscCommand({"frame-aux-shortcuts", "--send-osc", "auto:9000", "/a\x7f"}, command) &&
               !parseSendOscCommand({"frame-aux-shortcuts", "--send-osc", "auto:9000", "/" + std::string(128, 'a')}, command) &&
               parseSendOscCommand({"frame-aux-shortcuts", "--send-osc", "auto:9000", "/" + std::string(127, 'a')}, command));

    // /proc/net/udp: アドレスは 32 ビットごとにマシンのバイト順の 16 進（Frame は little-endian）
    const std::string head =
        "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ref pointer drops\n";
    const std::string udp = head +
        "  100: 0100007F:A3B1 0100007F:0035 01 00000000:00000000 00:00000000 00000000  1000        0 11111 2 0000000000000000 0\n"
        "  101: 2200A8C0:C001 0C00A8C0:6987 01 00000000:00000000 00:00000000 00000000  1000        0 22222 2 0000000000000000 0\n"
        "  102: 2200A8C0:C002 0100A8C0:0035 01 00000000:00000000 00:00000000 00000000  1000        0 33333 2 0000000000000000 0\n"
        "  103: 00000000:0044 00000000:0000 07 00000000:00000000 00:00000000 00000000  1000        0 44444 2 0000000000000000 0\n";
    std::string ip;
    expect("相手: Steam Link のソケットの相手（192.168.0.12）", findConnectedUdpPeer(udp, {22222}, ip) && ip == "192.168.0.12");
    expect("相手: inode で選ぶ", findConnectedUdpPeer(udp, {33333, 44444}, ip) && ip == "192.168.0.1");
    expect("相手: ループバックは使わない", !findConnectedUdpPeer(udp, {11111}, ip));
    expect("相手: つながっていない（状態 07）は使わない", !findConnectedUdpPeer(udp, {44444}, ip));
    expect("相手: 違う候補が 2 つ以上なら見つからない", !findConnectedUdpPeer(udp, {}, ip) &&
                                                          !findConnectedUdpPeer(udp, {22222, 33333}, ip));
    {
        const std::string pub = head +
            "  110: 2200A8C0:C001 08080808:6987 01 00000000:00000000 00:00000000 00000000  1000        0 77777 2 0000000000000000 0\n"
            "  111: 2200A8C0:C003 0C00A8C0:6988 01 00000000:00000000 00:00000000 00000000  1000        0 88888 2 0000000000000000 0\n"
            "  112: 2200A8C0:C004 0C00A8C0:6989 01 00000000:00000000 00:00000000 00000000  1000        0 99999 2 0000000000000000 0\n";
        expect("相手: LAN の外（8.8.8.8）は使わない", !findConnectedUdpPeer(pub, {77777}, ip));
        expect("相手: LAN の外と私的な 1 つなら私的な方", findConnectedUdpPeer(pub, {77777, 88888}, ip) && ip == "192.168.0.12");
        expect("相手: 同じアドレスが 2 つは 1 つと数える", findConnectedUdpPeer(pub, {88888, 99999}, ip) && ip == "192.168.0.12");
    }
    for (const char* lan : {"10.0.0.2", "172.16.0.1", "172.31.255.255", "192.168.0.12", "169.254.1.2", "100.64.0.1",
                            "100.127.255.255", "fc00::1", "fd12:3456::1", "fe80::1", "febf::1", "::ffff:192.168.0.12"}) {
        expect("相手: 私的な範囲", isLanAddress(lan));
    }
    for (const char* wan : {"8.8.8.8", "172.15.0.1", "172.32.0.1", "192.169.0.1", "100.63.0.1", "100.128.0.1", "127.0.0.1",
                            "0.0.0.0", "2001:db8::1", "fec0::1", "::1", "::ffff:8.8.8.8", "", "x", "192.168.0"}) {
        expect("相手: 私的でない範囲は使わない", !isLanAddress(wan));
    }
    const std::string udp6 = head +
        "  200: 00000000000000000000000000000000:A000 0000000000000000FFFF00000C00A8C0:6987 01 00000000:00000000 00:00000000 "
        "00000000  1000        0 55555 2 0000000000000000 0\n"
        "  201: 00000000000000000000000000000000:A001 00000000000000000000000001000000:6987 01 00000000:00000000 00:00000000 "
        "00000000  1000        0 66666 2 0000000000000000 0\n";
    expect("相手: IPv4 を写した IPv6 は IPv4 にする", findConnectedUdpPeer(udp6, {55555}, ip) && ip == "192.168.0.12");
    expect("相手: IPv6 のループバック（::1）は使わない", !findConnectedUdpPeer(udp6, {66666}, ip));
    expect("相手: 壊れた行は飛ばす", !findConnectedUdpPeer(head + "  1: zz 01\n", {}, ip));

    // 実際に送る: 127.0.0.1 で待ち、1 と 0 の 2 つが届くか
    const int receiver = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t length = sizeof(addr);
    const bool bound = receiver >= 0 && ::bind(receiver, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
                       ::getsockname(receiver, reinterpret_cast<sockaddr*>(&addr), &length) == 0;
    timeval timeout {};
    timeout.tv_sec = 2;
    if (bound) ::setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    SendOscCommand local;
    local.autoHost = false;
    local.host = "127.0.0.1";
    local.port = ntohs(addr.sin_port);
    local.address = "/input/Voice";
    const SendOscResult sent = bound ? sendOscPress(local, 10) : SendOscResult();
    char buffer[64];
    const ssize_t first = bound ? ::recv(receiver, buffer, sizeof(buffer), 0) : -1;
    const std::string one = first > 0 ? std::string(buffer, static_cast<size_t>(first)) : std::string();
    const ssize_t second = bound ? ::recv(receiver, buffer, sizeof(buffer), 0) : -1;
    const std::string zero = second > 0 ? std::string(buffer, static_cast<size_t>(second)) : std::string();
    expect("送る: 1 を送ってから 0 を送る", sent.ok && one == encodeOscInt("/input/Voice", 1) &&
                                              zero == encodeOscInt("/input/Voice", 0));
    if (receiver >= 0) ::close(receiver);
}

/** コマンド: argv の分け方・フォルダ・消す・保存・並び。 */
void testCommands() {
    const std::string home = std::getenv("HOME") != nullptr ? std::getenv("HOME") : "";
    std::vector<std::string> argv;
    std::string error;
    expect("コマンド: 空白で区切る", splitCommandLine("  wpctl  set-mute\t@DEFAULT_AUDIO_SINK@ toggle ", argv, error) &&
                                       argv == std::vector<std::string>({"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"}));
    expect("コマンド: \"…\" でまとめる（中の \\\" と \\\\）",
           splitCommandLine(R"(notify-send "aux の ボタン" "a\"b\\c" x"y z"w)", argv, error) &&
               argv == std::vector<std::string>({"notify-send", "aux の ボタン", "a\"b\\c", "xy zw"}));
    expect("コマンド: 空の \"\" も 1 つの引数", splitCommandLine("echo \"\" x", argv, error) &&
                                                  argv == std::vector<std::string>({"echo", "", "x"}));
    expect("コマンド: 先頭の ~/ はホームに広げる（\"…\" の中は広げない）",
           splitCommandLine("~/bin/a.sh ~/x \"~/y\" a~/b ~", argv, error) &&
               argv == std::vector<std::string>({home + "/bin/a.sh", home + "/x", "~/y", "a~/b", "~"}));
    expect("コマンド: \" が閉じていなければ読めない", !splitCommandLine("echo \"abc", argv, error) && !error.empty());
    expect("コマンド: 空なら argv も空", splitCommandLine("   ", argv, error) && argv.empty());
    const std::vector<std::string> tricky = {"/bin/echo", "空白 あり", "", "q\"uote", "back\\slash", "~/literal", "plain"};
    expect("コマンド: 文字列にして戻すと同じ",
           splitCommandLine(joinCommandLine(tricky), argv, error) && argv == tricky);
    expect("コマンド: 文字列の形", joinCommandLine({"wpctl", "set-volume", "a b"}) == "wpctl set-volume \"a b\"");

    // ふだんのログ（ジャーナル・状態ファイル）にコマンドの中身を残さない（FRAME_AUX_DEBUG=1 のときだけ argv）
    const std::vector<std::string> secretArgv = {"curl", "-H", "Authorization: Bearer SECRET123"};
    const std::string quietDetail = commandLogDetail("通知", "c1", secretArgv, true, "", false);
    expect("コマンドのログ: ふだんは名前と id と引数の数だけ",
           quietDetail.find("SECRET123") == std::string::npos && quietDetail.find("curl") == std::string::npos &&
               quietDetail.find("id c1") != std::string::npos && quietDetail.find("引数 3 個") != std::string::npos);
    expect("コマンドのログ: debug のときだけ argv を足す",
           commandLogDetail("通知", "c1", secretArgv, true, "", true).find("SECRET123") != std::string::npos);
    expect("コマンドのログ: 起動できないときも中身は出さない",
           commandLogDetail("通知", "c1", secretArgv, false, "exec: No such file or directory", false).find("SECRET123") ==
               std::string::npos);
    const std::string quietTry = tryCommandLogLine("id c1", secretArgv, true, "", false);
    expect("［試す］のログ: ふだんは中身を出さない",
           quietTry.find("SECRET123") == std::string::npos && quietTry.find("id c1") != std::string::npos);
    expect("［試す］のログ: debug のときだけ argv を足す",
           tryCommandLogLine("id c1", secretArgv, true, "", true).find("SECRET123") != std::string::npos);

    // フォルダの実行ファイル
    char base[] = "/tmp/fas-cmd-test-XXXXXX";
    const std::string dir = mkdtemp(base) != nullptr ? base : "/tmp/fas-cmd-test";
    const auto touch = [&](const std::string& name, mode_t mode) {
        const std::string path = dir + "/" + name;
        FILE* f = std::fopen(path.c_str(), "w");
        if (f != nullptr) {
            std::fputs("#!/bin/sh\n", f);
            std::fclose(f);
        }
        chmod(path.c_str(), mode);
    };
    touch("zeta.sh", 0755);
    touch("alpha", 0755);
    touch("notexec.sh", 0644);
    touch(".hidden.sh", 0755);
    mkdir((dir + "/subdir").c_str(), 0755);
    const std::vector<CommandDef> folder = scanCommandFolder(dir);
    expect("フォルダ: 実行できる通常のファイルだけ（隠しファイル・フォルダ・実行できないものは除く）、名前の順",
           folder.size() == 2 && folder[0].name == "alpha" && folder[1].name == "zeta");
    expect("フォルダ: id は dir:<ファイル名>、名前は拡張子を除く、argv はフルパス",
           folder.size() == 2 && folder[1].id == "dir:zeta.sh" && folder[1].argv == std::vector<std::string>({dir + "/zeta.sh"}) &&
               folder[1].fromFolder);
    expect("フォルダ: 無いフォルダなら空", scanCommandFolder(dir + "/nothing").empty());
    expect("フォルダ: 設定ファイルの隣の commands/", commandFolderFor("/home/u/.config/frame-aux-shortcuts/config.json") ==
                                                       "/home/u/.config/frame-aux-shortcuts/commands");
    for (const char* name : {"zeta.sh", "alpha", "notexec.sh", ".hidden.sh"}) std::remove((dir + "/" + name).c_str());
    rmdir((dir + "/subdir").c_str());
    rmdir(dir.c_str());

    // version 2 から: 同じ argv は 1 つのコマンドにまとめる
    {
        Config config;
        std::vector<std::string> warnings;
        parseConfigText(R"({ "version": 2, "actions": {
            "press_1": { "type": "command", "argv": ["/usr/bin/a", "x"] },
            "press_2": { "type": "command", "argv": ["/usr/bin/a", "x"] },
            "long_press": { "type": "command", "argv": ["b"] } } })", config, warnings, error);
        expect("設定 v2→3: 同じ argv は同じコマンド、違うものは別のコマンド",
               config.commands.size() == 2 && config.press[0].commandId == config.press[1].commandId &&
                   config.longPress.commandId != config.press[0].commandId && config.commands[0].name == "a");
    }
    {
        Config config;
        std::vector<std::string> warnings;
        parseConfigText(R"({ "version": 3, "commands": [ { "id": "c1", "name": "A", "argv": ["a"] },
            { "id": "c1", "name": "dup", "argv": ["b"] }, { "id": "dir:x", "name": "bad", "argv": ["c"] },
            { "id": "c2", "name": "empty", "argv": [] } ],
            "actions": { "press_1": { "type": "command", "command": "c1" }, "press_2": { "type": "command" } } })",
                        config, warnings, error);
        expect("設定 v3: 重なる id・dir: の id・空の argv は読み飛ばす", config.commands.size() == 1 && warnings.size() >= 3);
        expect("設定 v3: command の id で割り当て、id の無いものは none",
               config.press[0].commandId == "c1" && config.press[1].type == ActionType::None);
    }

    // 消す・保存する
    {
        Config config;
        config.commands = {{"c1", "A", {"a"}, false}, {"c2", "B", {"b"}, false}};
        config.commandOrder = {"c2", "c1"};
        config.press[0] = {ActionType::Command, "c1"};
        config.longPress = {ActionType::Command, "c1"};
        config.press[1] = {ActionType::Command, "c2"};
        expect("消す: 消したコマンドを割り当てていた押し方は none",
               removeCommand(config, "c1") && config.press[0].type == ActionType::None &&
                   config.longPress.type == ActionType::None && config.press[1].commandId == "c2" &&
                   config.commands.size() == 1 && config.commandOrder == std::vector<std::string>({"c2"}));
        expect("消す: 無い id は何もしない", !removeCommand(config, "c9"));
        std::string id;
        expect("保存: 足すと空いている一番小さい id", saveCommand(config, "", "  新しい  ", "echo \"a b\"", id, error) &&
                                                        id == "c1" && config.commands.back().name == "新しい" &&
                                                        config.commands.back().argv == std::vector<std::string>({"echo", "a b"}));
        expect("保存: 書き換え", saveCommand(config, "c2", "B2", "true", id, error) && id == "c2" &&
                                   config.commands[0].name == "B2" && config.commands[0].argv == std::vector<std::string>({"true"}));
        expect("保存: 名前が空なら保存しない", !saveCommand(config, "", "  ", "true", id, error));
        expect("保存: コマンドが空なら保存しない", !saveCommand(config, "", "x", "   ", id, error));
        expect("保存: \" が閉じていなければ保存しない", !saveCommand(config, "", "x", "echo \"a", id, error));
        expect("保存: 無い id は書き換えない", !saveCommand(config, "c9", "x", "true", id, error));
        expect("id: 使っていない一番小さい番号", nextCommandId({{"c1", "", {"a"}, false}, {"c3", "", {"a"}, false}}) == "c2");
    }

    // 並び（フォルダの分も id で）
    {
        Config config;
        config.commands = {{"c1", "A", {"a"}, false}, {"c2", "B", {"b"}, false}, {"c3", "C", {"c"}, false}};
        const std::vector<CommandDef> folder = {{"dir:f1", "f1", {"/x/f1"}, true}, {"dir:f2", "f2", {"/x/f2"}, true}};
        const auto ids = [](const std::vector<CommandDef>& list) {
            std::string out;
            for (const auto& c : list) out += c.id + " ";
            return out;
        };
        expect("並び: 並びが無ければ設定の順、続けてフォルダ", ids(orderedCommands(config, folder)) == "c1 c2 c3 dir:f1 dir:f2 ");
        config.commandOrder = {"dir:f2", "c3", "gone", "c1"};
        expect("並び: 並びの順、無い id は飛ばし、並びに無いものは後ろ（フォルダの新しいものは最後）",
               ids(orderedCommands(config, folder)) == "dir:f2 c3 c1 c2 dir:f1 ");
        expect("並び替え: 新しい並びを id で書き、設定の commands も並べ直す",
               setCommandOrder(config, folder, {"c2", "dir:f1", "c1"}) &&
                   config.commandOrder == std::vector<std::string>({"c2", "dir:f1", "c1", "c3", "dir:f2"}) &&
                   config.commands[0].id == "c2" && config.commands[1].id == "c1" && config.commands[2].id == "c3");
        expect("並び替え: 同じ並びなら変わらない", !setCommandOrder(config, folder, config.commandOrder));
    }
}

/** SteamVR のキーボード（Minimal）から 1 文字ずつ届く入力の組み立て。 */
void testKeyboardText() {
    KeyboardText k;
    k.reset("ab");
    expect("キーボード: 初期値から始め、入力位置は末尾", k.text() == "ab" && k.cursor() == 2);
    k.feed("c");
    expect("キーボード: ASCII を足す", k.text() == "abc");
    const std::string a = "あ";  // e3 81 82
    k.feed(a.substr(0, 1).c_str());
    expect("キーボード: UTF-8 の途中のバイトはまだ出さない", k.text() == "abc");
    k.feed(a.substr(1).c_str());
    expect("キーボード: イベントをまたいだ UTF-8 を組み立てる", k.text() == "abcあ");
    k.feed("\xe3\x81\x84\xe3\x81");  // い と、う の途中
    expect("キーボード: 1 つのイベントに 1 文字半", k.text() == "abcあい");
    k.feed("\x86x");
    expect("キーボード: 残りのバイトで完成させて続ける", k.text() == "abcあいうx");
    k.feed("\b");
    k.feed("\b");
    expect("キーボード: \\b は UTF-8 の 1 文字ぶん消す", k.text() == "abcあい" && k.cursor() == k.text().size());
    k.feed("\xf0\x9f");  // 絵文字の途中
    k.feed("\b");
    expect("キーボード: 組み立て途中の \\b は途中のバイトだけ捨てる", k.text() == "abcあい");
    k.feed("\x1b[D");
    expect("キーボード: 左の矢印で 3 バイトの文字を 1 つ戻る", k.text() == "abcあい" && k.cursor() == 6);
    k.feed("\x1b[");
    k.feed("Cz");
    expect("キーボード: イベントをまたいだ右の矢印で末尾に戻って足す", k.text() == "abcあいz");
    expect("キーボード: \\n は完了（文字は入れない）", k.feed("\n") && k.text() == "abcあいz");
    expect("キーボード: 完了でなければ false", !k.feed("q"));

    KeyboardText m;
    m.reset("aあいb");  // a(1) あ(3) い(3) b(1) = 8 バイト
    m.feed("\x1b[D\x1b[D");
    expect("入力位置: 左を 2 回で い の前", m.cursor() == 4);
    m.feed("X");
    expect("入力位置: 文字は入力位置に差し込む", m.text() == "aあXいb" && m.cursor() == 5);
    m.feed("\b");
    m.feed("\b");
    expect("入力位置: 途中の \\b は入力位置の前の 1 文字を消す（3 バイトの文字も）", m.text() == "aいb" && m.cursor() == 1);
    m.feed("\x1b[3~");
    expect("入力位置: Delete（ESC [ 3 ~）は後ろの 1 文字を消す", m.text() == "ab" && m.cursor() == 1);
    m.feed("\x1b[H");
    expect("入力位置: Home（ESC [ H）で先頭", m.cursor() == 0);
    m.feed("\x1b[D\b");
    expect("入力位置: 先頭では左も \\b も何もしない", m.cursor() == 0 && m.text() == "ab");
    m.feed("\x1b[F");
    expect("入力位置: End（ESC [ F）で末尾", m.cursor() == 2);
    m.feed("\x1b[C\x1b[3~");
    expect("入力位置: 末尾では右も Delete も何もしない", m.cursor() == 2 && m.text() == "ab");
    m.feed("\x1b[1~");
    expect("入力位置: ESC [ 1 ~ も先頭", m.cursor() == 0);
    m.feed("\x1b[4~");
    expect("入力位置: ESC [ 4 ~ も末尾", m.cursor() == 2);
    m.feed("\x1b");
    m.feed("[");
    m.feed("3");
    m.feed("~");
    expect("入力位置: 1 バイトずつ届いたエスケープも組み立てる（ここは末尾なので何も消えない）", m.text() == "ab");
    m.feed("\x1b[A\x1b[B");
    expect("入力位置: 上下の矢印は無視", m.cursor() == 2 && m.text() == "ab");
    m.feed("\x1b" "c");
    expect("入力位置: ESC だけのあとの文字はふつうに入る", m.text() == "abc");
    m.reset("日本語");
    m.setCursor(4);  // 本 の途中
    expect("入力位置: 文字の途中を指したら、その文字の先頭に寄せる", m.cursor() == 3);
    m.setCursor(100);
    expect("入力位置: 末尾より後ろは末尾", m.cursor() == 9);
    m.setCursor(0);
    m.feed("\x1b[C\x1b[C");
    expect("入力位置: 右で 3 バイトの文字を 1 つずつ進む", m.cursor() == 6);

    KeyboardText reopen;
    reopen.reset("abcdef");  // 直前の Closed で確定した文字から開き直す
    reopen.setCursor(3);       // 押した位置
    reopen.feed("X");
    expect("開き直し: 前の文字を初期値にして、押した位置から差し込む", reopen.text() == "abcXdef" && reopen.cursor() == 4);

    KeyboardText e;
    e.reset("");
    e.feed("\b");
    expect("キーボード: 空で \\b しても空のまま", e.text().empty());
    const char eight[8] = {'1', '2', '3', '4', '5', '6', '7', '8'};
    e.feed(eight, 8);
    expect("キーボード: NUL が無くても 8 バイトまでしか読まない", e.text() == "12345678");
    KeyboardText broken;
    broken.reset("");
    broken.feed("\xe3\x81" "A");
    expect("キーボード: 途中で新しい文字が来たら、途中のバイトは捨てる", broken.text() == "A");
    expect("キーボード: 16 進のログ", hexBytes("\xe3\x81") == "e3 81" && hexBytes("") == "(空)");

    // ふだんのログに打った中身を残さない（FRAME_AUX_KEYBOARD_DEBUG=1 のときだけ 16 進）
    const char secretInput[8] = {'p', 'w', '9', 0, 0, 0, 0, 0};
    const std::string quietLine =
        keyboardEventLogLine("オーバーレイ", "KeyboardCharInput", 1201, 0, 5, 256, secretInput, sizeof(secretInput), false);
    const std::string debugLine =
        keyboardEventLogLine("オーバーレイ", "KeyboardCharInput", 1201, 0, 5, 256, secretInput, sizeof(secretInput), true);
    expect("キーボードのログ: ふだんはバイト数だけで、中身（文字も 16 進も）は出さない",
           quietLine.find("入力 3 バイト") != std::string::npos && quietLine.find("pw9") == std::string::npos &&
               quietLine.find("70 77 39") == std::string::npos && quietLine.find("cNewInput") == std::string::npos);
    expect("キーボードのログ: debug のときだけ 16 進を足す", debugLine.find("cNewInput=70 77 39") != std::string::npos);
    const char fullInput[8] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};  // NUL で終わらない 8 バイト
    expect("キーボードのログ: 8 バイトいっぱいでも読みすぎない",
           keyboardEventLogLine("全体", "KeyboardCharInput", 1201, 0, 5, 0, fullInput, sizeof(fullInput), false)
                   .find("入力 8 バイト") != std::string::npos);
}

/** 設定パネル: 操作の反映と、描いた位置での当たり判定。 */
void testPanel() {
    using K = PanelAction::Kind;
    {
        Config config;
        expect("パネル: 4 回押しに none を割り当てる",
               applyPanelAction({K::Assign, static_cast<int>(ActionType::None), 3}, config) &&
                   config.press[3].type == ActionType::None);
        expect("パネル: どのコマンドかが無ければ command は割り当てない",
               !applyPanelAction({K::Assign, static_cast<int>(ActionType::Command), 3}, config));
        expect("パネル: コマンドを割り当てる",
               applyPanelAction({K::Assign, static_cast<int>(ActionType::Command), 0, "c2"}, config) &&
                   config.press[0].type == ActionType::Command && config.press[0].commandId == "c2");
        applyPanelAction({K::Assign, static_cast<int>(ActionType::MicMuteToggle), 0}, config);
        expect("パネル: ほかのアクションにすると id は消える", config.press[0].commandId.empty());
        expect("パネル: 長押しにも割り当てられる",
               applyPanelAction({K::Assign, static_cast<int>(ActionType::Screenshot), kLongPressSlot}, config) &&
                   config.longPress.type == ActionType::Screenshot);
    }
    {
        Config config;
        applyPanelAction({K::LevelInc, 0, 0}, config);
        applyPanelAction({K::LevelInc, 0, 0}, config);
        expect("パネル: 明るさの段を ＋（5 刻み）", config.brightnessLevels[0] == 110);
        for (int i = 0; i < 10; ++i) applyPanelAction({K::LevelInc, 0, 0}, config);
        expect("パネル: 明るさは 125 まで", config.brightnessLevels[0] == 125);
        for (int i = 0; i < 10; ++i) applyPanelAction({K::LevelDec, 2, 0}, config);
        expect("パネル: 明るさは 10 から", config.brightnessLevels[2] == 10);
        expect("パネル: 段を足す（最後の段 −20、10 より下にしない）",
               applyPanelAction({K::LevelAdd, 0, 0}, config) && config.brightnessLevels.size() == 4 &&
                   config.brightnessLevels[3] == 10);
        expect("パネル: 4 段より多くは足さない", !applyPanelAction({K::LevelAdd, 0, 0}, config));
        applyPanelAction({K::LevelRemove, 3, 0}, config);
        applyPanelAction({K::LevelRemove, 2, 0}, config);
        expect("パネル: 2 段より少なくは消さない",
               config.brightnessLevels.size() == 2 && !applyPanelAction({K::LevelRemove, 0, 0}, config));
    }
    {
        Config config;
        applyPanelAction({K::LongInc, 0, 0}, config);
        expect("パネル: 長押しは 0.1 秒刻み", config.longPressMs == 700);
        for (int i = 0; i < 20; ++i) applyPanelAction({K::LongInc, 0, 0}, config);
        expect("パネル: 長押しは 1.5 秒まで", config.longPressMs == 1500);
        applyPanelAction({K::GapDec, 0, 0}, config);
        expect("パネル: 回数の区切りは 0.05 秒刻み", config.multiPressGapMs == 300);
        for (int i = 0; i < 20; ++i) applyPanelAction({K::GapDec, 0, 0}, config);
        expect("パネル: 回数の区切りは 0.2 秒から", config.multiPressGapMs == 200);
        expect("パネル: コントローラーが接続されていないときは aux を従来の動きに戻すをオン",
               applyPanelAction({K::HeadPointerOn, 0, 0}, config) && config.originalAuxOnHeadPointer);
        expect("パネル: もう一度オンにしても変わらない", !applyPanelAction({K::HeadPointerOn, 0, 0}, config));
        expect("パネル: aux の元の動きを割り当てる",
               applyPanelAction({K::Assign, static_cast<int>(ActionType::OriginalAux), 0}, config) &&
                   config.press[0].type == ActionType::OriginalAux);
        config.language = Language::En;
        config.runInBackground = false;
        expect("パネル: すべて既定に戻す（言語は残す）",
               applyPanelAction({K::ResetAll, 0, 0}, config) && config.longPressMs == 600 && config.runInBackground &&
                   !config.originalAuxOnHeadPointer && config.language == Language::En);
        expect("パネル: コントローラーが接続されていないときは aux を従来の動きに戻すをオフ（既定のまま）",
               !applyPanelAction({K::HeadPointerOff, 0, 0}, config) && !config.originalAuxOnHeadPointer);
    }

    // 描いた位置での当たり判定
    FontSet fonts;
    fonts.load("/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc");
    SettingsPanel panel(fonts);
    Config config;
    config.language = Language::Ja;
    PanelView view;
    view.daemon = PanelView::Daemon::Running;
    panel.render(config, view);
    double x = 0;
    double y = 0;
    expect("パネル: 3 回押しのカードがある", panel.buttonCenter({K::SelectSlot, 2, 0}, x, y));
    panel.pointerDown(x, y, 0.0);
    expect("パネル: カードを押すと選ばれる（呼び出し側には返さない）", panel.selectedSlot() == 2);
    panel.render(config, view);
    expect("パネル: 選んだカードに割り当てる選択肢がある",
           panel.buttonCenter({K::Assign, static_cast<int>(ActionType::Screenshot), 2}, x, y));
    {
        double ox = 0;
        double oy = 0;
        expect("パネル: aux の元の動きの選択肢が区画の中にある",
               panel.buttonCenter({K::Assign, static_cast<int>(ActionType::OriginalAux), 2}, ox, oy) &&
                   oy < SettingsPanel::kHeight - 40);
    }
    const PanelAction assign = panel.pointerDown(x, y, 0.1);
    expect("パネル: 選択肢を押すと割り当ての操作が返る",
           assign == PanelAction({K::Assign, static_cast<int>(ActionType::Screenshot), 2}));
    expect("パネル: 明るさのタブがある", panel.buttonCenter({K::Tab, static_cast<int>(PanelTab::Brightness), 0}, x, y));
    panel.pointerDown(x, y, 0.2);
    expect("パネル: タブを押すと切り替わる", panel.tab() == PanelTab::Brightness);
    // すべてのタブ・日英で描ける（ボタンが画像の中にある）
    bool inside = true;
    for (const Language lang : {Language::Ja, Language::En}) {
        config.language = lang;
        for (int tab = 0; tab < kPanelTabCount; ++tab) {
            panel.setTab(static_cast<PanelTab>(tab));
            panel.render(config, view);
        }
    }
    panel.setTab(PanelTab::App);
    panel.render(config, view);
    expect("パネル: 終了のボタンがある", panel.buttonCenter({K::Quit, 0, 0}, x, y) && x < SettingsPanel::kWidth &&
                                          y < SettingsPanel::kHeight);
    {
        // コントローラーが接続されていないときは aux を従来の動きに戻すの切り替えは、パネルを閉じても aux を使うの下で、終了のボタンより上
        double bgX = 0;
        double bgY = 0;
        double ptrX = 0;
        double ptrY = 0;
        double checkX = 0;
        double checkY = 0;
        const bool found = panel.buttonCenter({K::BackgroundOn, 0, 0}, bgX, bgY) &&
                           panel.buttonCenter({K::HeadPointerOn, 0, 0}, ptrX, ptrY) &&
                           panel.buttonCenter({K::CheckOn, 0, 0}, checkX, checkY);
        expect("パネル: コントローラーが接続されていないときは aux を従来の動きに戻すの切り替えがある",
               found && bgY < ptrY && ptrY < checkY && ptrY + 28 < y - 28);
        const PanelAction on = panel.pointerDown(ptrX, ptrY, 20.0);
        expect("パネル: 切り替えを押すとオンの操作が返る", on.kind == K::HeadPointerOn);
    }
    expect("パネル: 終了は 1 回目では返らない", panel.pointerDown(x, y, 1.0).kind == K::None);
    expect("パネル: 3 秒以内の 2 回目で終了", panel.pointerDown(x, y, 2.0).kind == K::Quit);
    expect("パネル: 3 秒を過ぎた 2 回目は確認からやり直し", [&] {
        panel.pointerDown(x, y, 10.0);
        return panel.pointerDown(x, y, 14.0).kind == K::None;
    }());
    expect("パネル: すべてのタブを日英で描ける", inside);

    // アプリタブ: いっしょに使うアプリ・ささけんのアプリの一覧・Konsole で開く前の確認
    {
        using frame_apps::AppState;
        frame_apps::ManagerConfig appsConfig;
        appsConfig.self = "frame-aux-shortcuts";
        appsConfig.related = {{"frame-perf-overlay", "パフォーマンス表示に使う", "perf"}, {"frame-jp-keyboard", "日本語", "jp"}};
        appsConfig.fetch.cacheDir.clear();  // 取ってきた一覧は使わない（同梱の一覧で確かめる）
        frame_apps::AppsManager apps(appsConfig);
        apps.setForPreview({{"frame-perf-overlay", AppState::Missing},
                            {"frame-jp-keyboard", AppState::Installed},
                            {"frameeyeosc", AppState::Running},
                            {"frame-mic-tuner", AppState::Installed}},
                           {});
        view.apps = apps.entries();
        expect("アプリ: 自分は一覧に入らず、使うものが先", view.apps.size() == 4 && view.apps[0].app.key == "perf" &&
                                                               view.apps[1].app.key == "keyboard" && view.apps[0].related);
        config.language = Language::Ja;
        panel.setTab(PanelTab::App);
        panel.render(config, view);
        expect("アプリ: 入っていない perf に［入れる］", panel.buttonCenter({K::AppInstall, 0, 0, "perf"}, x, y));
        expect("アプリ: 入っている keyboard には［入れる］が無い", !panel.buttonCenter({K::AppInstall, 0, 0, "keyboard"}, x, y));
        panel.buttonCenter({K::AppInstall, 0, 0, "perf"}, x, y);
        expect("アプリ: ［入れる］はまず確認（呼び出し側には返さない）", panel.pointerDown(x, y, 30.0).kind == K::None);
        panel.pointerUp();
        panel.render(config, view);
        expect("アプリ: 確認の下のボタンは押せない", !panel.buttonCenter({K::AppsOpenList, 0, 0, ""}, x, y) &&
                                                         !panel.buttonCenter({K::Quit, 0, 0}, x, y));
        expect("アプリ: 確認の［Konsole で開く］で呼び名が返る", panel.buttonCenter({K::AppsConfirmLaunch, 0, 0, ""}, x, y) &&
                                                                  panel.pointerDown(x, y, 30.1) ==
                                                                      PanelAction(K::AppsConfirmLaunch, 0, 0, "perf"));
        panel.pointerUp();
        panel.setAppsLaunchResult(frame_apps::LaunchResult::NoKonsole);
        panel.render(config, view);
        expect("アプリ: 開けなければ確認のまま", panel.buttonCenter({K::AppsConfirmLaunch, 0, 0, ""}, x, y));
        panel.setAppsLaunchResult(frame_apps::LaunchResult::Started);
        panel.render(config, view);
        expect("アプリ: 開けたら確認を閉じる", !panel.buttonCenter({K::AppsConfirmLaunch, 0, 0, ""}, x, y) &&
                                                   panel.buttonCenter({K::AppsOpenList, 0, 0, ""}, x, y));
        panel.pointerDown(x, y, 31.0);
        panel.pointerUp();
        panel.render(config, view);
        expect("アプリ: 一覧に ✕ と［インストーラーを開く］", panel.appsListOpen() &&
                                                               panel.buttonCenter({K::AppsCloseList, 0, 0, ""}, x, y) &&
                                                               panel.buttonCenter({K::AppsOpenMenu, 0, 0, ""}, x, y));
        expect("アプリ: 一覧の中の［入れる］", panel.buttonCenter({K::AppInstall, 0, 0, "perf"}, x, y));
        expect("アプリ: 4 個なら一覧はスクロールしない", !panel.scroll(-1.0));
        panel.buttonCenter({K::AppsOpenMenu, 0, 0, ""}, x, y);
        panel.pointerDown(x, y, 31.5);
        panel.pointerUp();
        panel.render(config, view);
        expect("アプリ: メニューの確認は空の呼び名", panel.buttonCenter({K::AppsConfirmLaunch, 0, 0, ""}, x, y) &&
                                                         panel.pointerDown(x, y, 31.6) == PanelAction(K::AppsConfirmLaunch, 0, 0, ""));
        panel.pointerUp();
        panel.render(config, view);
        panel.buttonCenter({K::AppsConfirmCancel, 0, 0, ""}, x, y);
        panel.pointerDown(x, y, 31.7);
        panel.pointerUp();
        panel.render(config, view);
        expect("アプリ: やめたら一覧に戻る", panel.appsListOpen() && panel.buttonCenter({K::AppsCloseList, 0, 0, ""}, x, y));
        view.menuBusy = true;
        panel.render(config, view);
        expect("アプリ: メニューの Konsole が開いている間は［インストーラーを開く］を押せない",
               !panel.buttonCenter({K::AppsOpenMenu, 0, 0, ""}, x, y));
        view.menuBusy = false;
        for (const Language lang : {Language::Ja, Language::En}) {
            config.language = lang;
            panel.render(config, view);
        }
        config.language = Language::Ja;
        panel.render(config, view);
        panel.buttonCenter({K::Tab, static_cast<int>(PanelTab::Assign), 0}, x, y);
        panel.pointerDown(x, y, 32.0);
        panel.pointerUp();
        expect("アプリ: タブを替えたら一覧を閉じる", !panel.appsListOpen());
        panel.setTab(PanelTab::App);
        panel.render(config, view);
        expect("アプリ: 戻ったときは一覧を開く行", panel.buttonCenter({K::AppsOpenList, 0, 0, ""}, x, y));

        // サイトにだけあるアプリ: 確認の画面の名前とコマンドは今の一覧から引き、Konsole に渡すのと同じ installerCommand
        {
            frame_apps::Entry site = view.apps[2];
            site.app.name = "frame-example";
            site.app.key = "example";
            site.state = AppState::Missing;
            view.apps.push_back(site);
            panel.openAppsForPreview(true, "example", 0);
            panel.render(config, view);
            expect("アプリ: サイトにだけあるアプリの確認は install example",
                   panel.appsConfirmEntry(view) != nullptr && panel.appsConfirmEntry(view)->app.name == "frame-example" &&
                       panel.appsConfirmCommand(view) == frame_apps::installerCommand("example") &&
                       panel.appsConfirmCommand(view) == "curl -fsSL https://frame.sasaken1102s.net | sh -s -- install example");
            expect("アプリ: そのまま［Konsole で開く］で example が返る",
                   panel.buttonCenter({K::AppsConfirmLaunch, 0, 0, ""}, x, y) &&
                       panel.pointerDown(x, y, 50.0) == PanelAction(K::AppsConfirmLaunch, 0, 0, "example"));
            panel.pointerUp();
            view.apps.pop_back();  // 一覧から消えた
            panel.render(config, view);
            expect("アプリ: 一覧から消えた呼び名は開けない（画面はメニューのコマンド）",
                   panel.appsConfirmEntry(view) == nullptr && panel.appsConfirmCommand(view) == frame_apps::installerCommand("") &&
                       panel.buttonCenter({K::AppsConfirmLaunch, 0, 0, ""}, x, y) && panel.pointerDown(x, y, 50.5).kind == K::None);
            panel.pointerUp();
            panel.openAppsForPreview(false, "", 0);
        }

        // アプリが増えたら一覧はスクロールする（スティックと、地を押したまま動かす）
        for (int i = 0; i < 6; ++i) {
            frame_apps::Entry extra = view.apps[2];
            extra.app.name = "frame-example-" + std::to_string(i);
            view.apps.push_back(extra);
        }
        panel.openAppsForPreview(true, "", 0);
        panel.render(config, view);
        expect("アプリ: 10 個ならスティックでスクロールする", panel.scroll(-1.0));
        expect("アプリ: 上へ戻せる", panel.scroll(5.0) && !panel.scroll(1.0));
        expect("アプリ: 地を押したまま動かすとスクロール", [&] {
            panel.render(config, view);
            double dx = 0, dy = 0, dw = 0, dh = 0;
            if (!panel.buttonBounds({K::AppsListDrag, 0, 0, ""}, dx, dy, dw, dh)) return false;
            // 行の［入れる］が無い左の端（札のあたり）を押して上へ動かす
            const double px = dx + 30;
            const double py = dy + dh - 20;
            if (panel.pointerDown(px, py, 40.0).kind != K::None) return false;
            const bool moved = panel.pointerMove(px, py - 150);
            panel.pointerUp();
            return moved;
        }());
        expect("アプリ: 一覧の外に隠れた［入れる］は押せない", [&] {
            panel.openAppsForPreview(true, "", 0);
            panel.render(config, view);
            double bx = 0, by = 0, bw = 0, bh = 0;
            bool ok = true;
            for (const frame_apps::Entry& entry : view.apps) {
                if (panel.buttonBounds({K::AppInstall, 0, 0, entry.app.key}, bx, by, bw, bh)) ok = ok && by + bh <= 740;
            }
            return ok;
        }());
        view.apps.resize(4);
        panel.openAppsForPreview(false, "", 0);
    }

    // コマンド: 割り当ての列・シート・並び替え
    for (int i = 1; i <= 10; ++i) {
        config.commands.push_back({"c" + std::to_string(i), "Cmd " + std::to_string(i), {"/bin/true"}, false});
    }
    config.press[4] = {ActionType::Command, "c9"};
    config.language = Language::Ja;
    panel.setTab(PanelTab::Assign);
    panel.setSelectedSlot(4);
    panel.render(config, view);
    expect("パネル: コマンドを割り当てたカードを選ぶと、コマンドの側のそのページ（9 個目 → 2 ページ目）",
           panel.buttonCenter({K::Assign, static_cast<int>(ActionType::Command), 4, "c9"}, x, y) &&
               !panel.buttonCenter({K::Assign, static_cast<int>(ActionType::Command), 4, "c1"}, x, y));
    expect("パネル: 最後のページの最後に「＋ コマンドを足す」", panel.buttonCenter({K::AddCommand, 0, 0, ""}, x, y));
    panel.pointerDown(x, y, 20.0);
    expect("パネル: 「＋ コマンドを足す」でコマンドのタブの追加のシート", panel.tab() == PanelTab::Commands && panel.sheetOpen() &&
                                                                     panel.sheetId().empty());
    expect("プリセット: 追加のシートは「プリセットから」・VRChat のミュートで始まる",
           panel.presetMode() && panel.preset() == CommandPreset::VrchatMute && panel.wantsPeer());
    {
        std::string name;
        std::vector<std::string> argv;
        expect("プリセット: VRChat のミュートは送り先を自動で登録する",
               panel.presetCommand(Language::Ja, name, argv) && name == "VRChat のミュート（PC）" &&
                   argv == std::vector<std::string>({"frame-aux-shortcuts", "--send-osc", "auto:9000", "/input/Voice"}));
        panel.render(config, view);
        expect("プリセット: 見つかった PC を見せる", panel.setDetectedPeer(true, "192.168.0.12") &&
                                                       !panel.setDetectedPeer(true, "192.168.0.12"));
        panel.render(config, view);
        expect("プリセット: ［追加する］が押せる", panel.buttonCenter({K::PresetAdd, 0, 0, ""}, x, y) &&
                                                     panel.pointerDown(x, y, 20.1).kind == K::PresetAdd);
        expect("プリセット: ［試す］が押せる", panel.buttonCenter({K::PresetTest, 0, 0, ""}, x, y) &&
                                                 panel.pointerDown(x, y, 20.2).kind == K::PresetTest);
        expect("プリセット: 手で入れるに切り替えられる", panel.buttonCenter({K::OscDestManual, 0, 0, ""}, x, y) &&
                                                           panel.pointerDown(x, y, 20.3).kind == K::None && !panel.wantsPeer());
        panel.render(config, view);
        expect("プリセット: IP が空なら追加できない", !panel.presetCommand(Language::Ja, name, argv) &&
                                                        panel.buttonCenter({K::PresetAdd, 0, 0, ""}, x, y) &&
                                                        panel.pointerDown(x, y, 20.4).kind == K::None);
        expect("プリセット: IP の欄が見えて、押すと呼び出し側に返る",
               panel.sheetFieldShown(SettingsPanel::kFieldIp) && !panel.sheetFieldShown(SettingsPanel::kFieldName) &&
                   panel.buttonCenter({K::SheetIp, 0, 0, ""}, x, y) && panel.pointerDown(x, y, 20.5).kind == K::SheetIp);
        panel.setSheetText(SettingsPanel::kFieldIp, "192.168.0.");
        expect("プリセット: IP の形が違えば追加できない", !panel.presetCommand(Language::Ja, name, argv));
        panel.setSheetText(SettingsPanel::kFieldIp, "192.168.0.12");
        expect("プリセット: 手で入れた IP で登録する",
               panel.presetCommand(Language::En, name, argv) && name == "VRChat mute (PC)" &&
                   argv == std::vector<std::string>({"frame-aux-shortcuts", "--send-osc", "192.168.0.12:9000", "/input/Voice"}));
        panel.render(config, view);
        expect("プリセット: スピーカーのミュートを選べる", panel.buttonCenter({K::PresetPick, 1, 0, ""}, x, y) &&
                                                             panel.pointerDown(x, y, 20.6).kind == K::None &&
                                                             panel.preset() == CommandPreset::SpeakerMute);
        expect("プリセット: スピーカーのミュートは wpctl",
               panel.presetCommand(Language::Ja, name, argv) && name == "スピーカーのミュート" &&
                   argv == std::vector<std::string>({"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"}) &&
                   !panel.sheetFieldShown(SettingsPanel::kFieldIp));
        panel.render(config, view);
        expect("プリセット: 「自分で書く」に切り替えると名前の欄", panel.buttonCenter({K::SheetModeOwn, 0, 0, ""}, x, y) &&
                                                                    panel.pointerDown(x, y, 20.7).kind == K::None &&
                                                                    !panel.presetMode() &&
                                                                    panel.sheetFieldShown(SettingsPanel::kFieldName));
        for (const Language lang : {Language::Ja, Language::En}) {
            Config c = config;
            c.language = lang;
            for (const bool presets : {true, false}) {
                for (const CommandPreset preset : {CommandPreset::VrchatMute, CommandPreset::SpeakerMute}) {
                    for (const bool autoDest : {true, false}) {
                        panel.setPresetForPreview(presets, preset, autoDest);
                        panel.render(c, view);
                    }
                }
            }
        }
        panel.setPresetForPreview(false, CommandPreset::VrchatMute, true);
    }
    panel.render(config, view);
    expect("パネル: 名前とコマンドが空なら保存できない",
           panel.buttonCenter({K::SheetSave, 0, 0, ""}, x, y) && panel.pointerDown(x, y, 21.0).kind == K::None);
    panel.setSheetText(0, "新しいコマンド");
    panel.setSheetText(1, "echo hi");
    panel.render(config, view);
    panel.buttonCenter({K::SheetSave, 0, 0, ""}, x, y);
    expect("パネル: 入れたら保存できる", panel.pointerDown(x, y, 22.0).kind == K::SheetSave);
    panel.closeSheet();
    panel.setSelectedSlot(1);
    panel.setTab(PanelTab::Assign);
    panel.render(config, view);
    expect("パネル: アクションを割り当てたカードを選ぶと、アクションの側",
           panel.buttonCenter({K::Assign, static_cast<int>(ActionType::BrightnessCycle), 1, ""}, x, y));
    panel.setTab(PanelTab::Commands);
    panel.render(config, view);
    double gx = 0;
    double gy = 0;
    expect("パネル: 一覧の行につまみがある", panel.buttonCenter({K::Grip, 0, 0, "c1"}, gx, gy));
    panel.pointerDown(gx, gy, 30.0);
    expect("パネル: つまみを押すと並び替えの途中", panel.dragging());
    panel.pointerMove(gx, gy + (SettingsPanel::kListRowH + SettingsPanel::kListRowGap) * 2);
    panel.pointerUp();
    std::vector<std::string> order;
    expect("パネル: 2 行ぶん下へ動かして離すと、その並びが返る",
           panel.takeReorder(order) && order.size() == 10 && order[0] == "c2" && order[1] == "c3" && order[2] == "c1");
    expect("パネル: 返すのは 1 回だけ", !panel.takeReorder(order));
    {
        SettingsPanel caret(fonts);
        expect("カーソル: 入力中の欄が無ければ点滅しない", !caret.caretTick(0.0));
        caret.setActiveField(1);
        caret.caretTick(10.0);
        expect("カーソル: 0.53 秒で消える", !caret.caretTick(10.5) && caret.caretTick(10.6));
        expect("カーソル: 次の 0.53 秒で点く", caret.caretTick(11.1));
        caret.caretTick(11.7);  // 消えた
        caret.setSheetText(1, "x");
        expect("カーソル: 文字が入ったら点灯から始め直す", caret.caretTick(11.8) && !caret.caretTick(12.3));
        caret.setActiveField(-1);
        expect("カーソル: 閉じたら点滅しない", caret.activeField() == -1 && !caret.caretTick(20.0));
    }
    {
        // 入力中の欄を押すと、押した位置に一番近い文字の境目へ入力位置を移す
        SettingsPanel tap(fonts);
        Config tapConfig;
        tap.setTab(PanelTab::Commands);
        tap.render(tapConfig, view);
        tap.openSheet(nullptr);
        tap.setPresetForPreview(false, CommandPreset::VrchatMute, true);  // 自分で書く
        tap.setSheetText(0, "abcdef");
        tap.setActiveField(0);
        tap.render(tapConfig, view);
        double fx = 0;
        double fy = 0;
        size_t at = 99;
        double bx = 0;
        double by = 0;
        double bw = 0;
        double bh = 0;
        tap.buttonCenter({K::SheetName, 0, 0, ""}, fx, fy);
        tap.buttonBounds({K::SheetName, 0, 0, ""}, bx, by, bw, bh);
        const PanelAction tapped = tap.pointerDown(bx + 2, fy, 30.0);  // 欄の左端の少し右
        expect("入力位置: 入力中の欄を押すと、押した位置を渡す", tapped.kind == K::SheetName && tap.takeTapCursor(at) && at == 0);
        expect("入力位置: 開いた直後は末尾", [&] {
            tap.setActiveField(0);
            return tap.sheetCursor() == 6;
        }());
        // キーボードが閉じていても（押した瞬間に SteamVR が閉じても）、押した位置を求めて、そこから開き直せる
        tap.setActiveField(-1);
        tap.render(tapConfig, view);
        expect("入力位置: 閉じている欄を押しても、押した位置を渡す（左端なら先頭）",
               tap.pointerDown(bx + 2, fy, 31.0).kind == K::SheetName && tap.takeTapCursor(at) && at == 0);
        expect("入力位置: 文字の右の空きを押したら末尾",
               tap.pointerDown(fx + 300, fy, 32.0).kind == K::SheetName && tap.takeTapCursor(at) && at == 6);
        tap.pointerDown(bx + 16 + 30, fy, 33.0);  // 先頭から 30px（abcdef の 19px の文字で 3 文字目のあたり）
        expect("入力位置: 文字の途中を押したら、その近くの境目（先頭でも末尾でもない）", tap.takeTapCursor(at) && at > 0 && at < 6);
        tap.setActiveField(0, at);
        expect("入力位置: 押した位置で開き直す", tap.activeField() == 0 && tap.sheetCursor() == at);
        tap.setActiveField(0, 100);
        expect("入力位置: 文字より後ろを指したら末尾", tap.sheetCursor() == 6);
        tap.setSheetText(1, "");
        tap.render(tapConfig, view);
        double cx = 0;
        double cy = 0;
        tap.buttonCenter({K::SheetCommand, 0, 0, ""}, cx, cy);
        expect("入力位置: 空の欄を押したら末尾（0）",
               tap.pointerDown(cx - 200, cy, 34.0).kind == K::SheetCommand && tap.takeTapCursor(at) && at == 0);
    }
    expect("パネル: スクロールで一覧が動く（10 個は入りきらない）", panel.scroll(-1.0));
    expect("パネル: 一番上より上へは動かない", [&] {
        panel.scroll(100.0);
        return !panel.scroll(1.0);
    }());
}

}  // namespace

/**
 * テストの入口。
 * @return すべて合格なら 0、1 つでも不合格なら 1
 */
/** 実行時のフォルダ・設定ファイルの権限（ほかのユーザーのフォルダやリンクに書かない。設定は 0600）。 */
void testPrivateFiles() {
    char base[] = "/tmp/fas-private-test-XXXXXX";
    if (mkdtemp(base) == nullptr) {
        expect("権限: テスト用のフォルダを作れる", false);
        return;
    }
    const std::string root = base;
    const uid_t me = ::getuid();
    std::string reason;

    const std::string good = root + "/good";
    expect("予備の置き場所: 新しく作ると 0700 で使える", ensurePrivateDir(good, reason));
    struct stat st {};
    expect("予備の置き場所: 作ったフォルダは 0700", ::lstat(good.c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
    expect("予備の置き場所: 持ち主がほかのユーザーなら使わない", !checkPrivateDir(good, me + 1, reason));

    const std::string link = root + "/link";
    const bool linked = ::symlink(good.c_str(), link.c_str()) == 0;
    expect("予備の置き場所: シンボリックリンクなら使わない", linked && !ensurePrivateDir(link, reason));
    std::string error;
    expect("予備の置き場所: リンクの先には状態ファイルを書かない",
           !writeFileAtomically(link + "/status.json", "{}", error) && ::access((good + "/status.json").c_str(), F_OK) != 0);

    const std::string openDir = root + "/open";
    ::mkdir(openDir.c_str(), 0700);
    ::chmod(openDir.c_str(), 0755);
    expect("予備の置き場所: ほかのユーザーにも権限があれば使わない", !ensurePrivateDir(openDir, reason));

    const std::string file = root + "/file";
    expect("予備の置き場所: フォルダでなければ使わない",
           writeFilePrivately(file, "x", error) && !ensurePrivateDir(file, reason));

    // ロックファイル: リンクは開かない
    const std::string lockLink = good + "/x.lock";
    const bool lockLinked = ::symlink(file.c_str(), lockLink.c_str()) == 0;
    const int lockFd = openPrivateFile(lockLink, reason);
    expect("ロック: シンボリックリンクは開かない", lockLinked && lockFd < 0);
    if (lockFd >= 0) ::close(lockFd);
    const int realLock = openPrivateFile(good + "/real.lock", reason);
    expect("ロック: 新しいロックファイルは 0600",
           realLock >= 0 && ::fstat(realLock, &st) == 0 && (st.st_mode & 0777) == 0600);
    if (realLock >= 0) ::close(realLock);

    expect("状態ファイル: 書けて 0600",
           writeFileAtomically(good + "/status.json", "{}", error) && ::stat((good + "/status.json").c_str(), &st) == 0 &&
               (st.st_mode & 0777) == 0600);

    // 設定ファイル: 0600 で保存（前が 0644 でも 0600 に）。新しく作るフォルダは 0700
    const std::string configPath = root + "/config/frame-aux-shortcuts/config.json";
    Config config;
    expect("設定: 保存できる", saveConfig(configPath, config, error));
    expect("設定: 保存したファイルは 0600", ::stat(configPath.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    expect("設定: 新しく作ったフォルダは 0700",
           ::stat((root + "/config/frame-aux-shortcuts").c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
    ::chmod(configPath.c_str(), 0644);
    expect("設定: 前が 0644 でも、保存すると 0600",
           saveConfig(configPath, config, error) && ::stat(configPath.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    std::string text;
    expect("設定: 中身が読める", readTextFile(configPath, text) && text.find("\"version\"") != std::string::npos);

    const std::string cleanup = "rm -rf '" + root + "'";
    const int rc = std::system(cleanup.c_str());
    (void)rc;
}

/** 最後に選んだタブ: 名前の読み書き・ファイルの権限・壊れたファイル・リンク。 */
void testPanelState() {
    bool roundTrip = true;
    for (int i = 0; i < kPanelTabCount; ++i) {
        PanelTab tab = PanelTab::Assign;
        if (!parsePanelTab(panelTabName(static_cast<PanelTab>(i)), tab) || tab != static_cast<PanelTab>(i)) roundTrip = false;
    }
    PanelTab tab = PanelTab::App;
    expect("タブ: 名前と行き来できる", roundTrip && parsePanelTab("commands", tab) && tab == PanelTab::Commands);
    expect("タブ: 知らない名前は読まない", !parsePanelTab("bogus", tab) && !parsePanelTab("", tab) && tab == PanelTab::Commands);

    char base[] = "/tmp/fas-state-test-XXXXXX";
    if (mkdtemp(base) == nullptr) {
        expect("タブ: テスト用のフォルダを作れる", false);
        return;
    }
    const std::string root = base;
    const std::string path = root + "/state/frame-aux-shortcuts/panel.json";
    std::string error;
    std::string name;
    struct stat st {};
    expect("タブ: 無ければ読めない", !loadPanelTab(path, name));
    expect("タブ: 書ける（親のフォルダも作る）", savePanelTab(path, "commands", error));
    expect("タブ: 書いたものを読める", loadPanelTab(path, name) && name == "commands");
    expect("タブ: ファイルは 0600", ::stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    expect("タブ: フォルダは 0700", ::stat((root + "/state/frame-aux-shortcuts").c_str(), &st) == 0 &&
                                         (st.st_mode & 0777) == 0700 && ::stat((root + "/state").c_str(), &st) == 0 &&
                                         (st.st_mode & 0777) == 0700);
    expect("タブ: 書き換えられる", savePanelTab(path, "app", error) && loadPanelTab(path, name) && name == "app");
    expect("タブ: 使えない文字の名前は書かない", !savePanelTab(path, "../x", error) && loadPanelTab(path, name) && name == "app");
    writeFilePrivately(path, "{ broken", error);
    expect("タブ: 壊れたファイルは読まない", !loadPanelTab(path, name));
    writeFilePrivately(path, "{\"tab\": \"app\"}" + std::string(kMaxPanelStateBytes, ' '), error);
    expect("タブ: 大きすぎるファイルは読まない", !loadPanelTab(path, name));
    expect("タブ: パスが空なら読まない・書かない", !loadPanelTab("", name) && !savePanelTab("", "app", error));
    {
        const char* oldHome = std::getenv("HOME");
        const std::string savedHome = oldHome != nullptr ? oldHome : "";
        const char* oldState = std::getenv("XDG_STATE_HOME");
        const std::string savedState = oldState != nullptr ? oldState : "";
        ::unsetenv("XDG_STATE_HOME");
        ::unsetenv("HOME");
        const bool emptyWithoutHome = panelStatePath().empty();
        ::setenv("HOME", "relative", 1);
        const bool emptyRelative = panelStatePath().empty();
        if (oldHome != nullptr) ::setenv("HOME", savedHome.c_str(), 1);
        if (oldState != nullptr) ::setenv("XDG_STATE_HOME", savedState.c_str(), 1);
        expect("タブ: HOME が無い・相対なら覚えない", emptyWithoutHome && emptyRelative);
    }
    const std::string target = root + "/other.json";
    writeFilePrivately(target, "{\"tab\": \"press\"}", error);
    ::unlink(path.c_str());
    const bool linked = ::symlink(target.c_str(), path.c_str()) == 0;
    expect("タブ: シンボリックリンクは読まない", linked && !loadPanelTab(path, name));
    expect("タブ: シンボリックリンクの先には書かない（置き換える）",
           savePanelTab(path, "assign", error) && ::lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
               loadPanelTab(target, name) && name == "press");
    ::mkdir((root + "/state2").c_str(), 0700);
    ::mkdir((root + "/elsewhere").c_str(), 0700);
    const bool dirLinked = ::symlink((root + "/elsewhere").c_str(), (root + "/state2/frame-aux-shortcuts").c_str()) == 0;
    expect("タブ: アプリのフォルダがシンボリックリンクなら書かない",
           dirLinked && !savePanelTab(root + "/state2/frame-aux-shortcuts/panel.json", "assign", error) &&
               ::access((root + "/elsewhere/panel.json").c_str(), F_OK) != 0);
    const std::string cleanup = "rm -rf '" + root + "'";
    const int rc = std::system(cleanup.c_str());
    (void)rc;
}

/**
 * タップの判定に、250 Hz の読み取りを流す。静止（x 軸に base の角速度）に、taps の時刻ごとに 1 回分の揺れ
 * （y 軸に 0.6 rad/s を 1 回の読み取りだけ。角加速度は 150 rad/s²）を足す。
 * @param d 判定
 * @param t 今の時刻（µs）。untilUs まで進める
 * @param untilUs 流し終える時刻（µs）
 * @param taps 揺れの時刻（µs、4000 の倍数）
 * @param base 静止の |ω|（頭を回し続けているなら大きくする）
 * @param fireAt 確定したときの時刻の書き込み先（nullptr なら書かない）
 * @return 確定した組の回数（1 や 4 以上も入る）
 */
std::vector<int> feedTaps(TapDetector& d, int64_t& t, int64_t untilUs, const std::vector<int64_t>& taps,
                          double base = 0.02, std::vector<int64_t>* fireAt = nullptr) {
    std::vector<int> out;
    for (; t < untilUs; t += kTapSamplePeriodUs) {
        double wy = 0.0;
        for (const int64_t tap : taps) {
            if (t == tap) wy = 0.6;
        }
        const TapResult result = d.onSample(t, base, wy, 0.0);
        if (result.taps > 0) {
            out.push_back(result.taps);
            if (fireAt != nullptr) fireAt->push_back(t);
        }
    }
    return out;
}

/** ヘッドセットのタップの判定・設定・状態ファイル・文言。 */
void testTaps() {
    const int64_t ms = 1000;  // µs
    {
        TapDetector d;
        int64_t t = 0;
        expect("タップ: 静止だけなら何もない", feedTaps(d, t, 1000 * ms, {}).empty());
        expect("タップ: 2 回（間 0.25 秒）で 2 回タップ", feedTaps(d, t, 2500 * ms, {1200 * ms, 1452 * ms}) == std::vector<int>({2}));
        expect("タップ: 3 回で 3 回タップ",
               feedTaps(d, t, 5000 * ms, {3600 * ms, 3852 * ms, 4100 * ms}) == std::vector<int>({3}));
        expect("タップ: 1 回だけは 1（何もしない）", feedTaps(d, t, 7000 * ms, {6000 * ms}) == std::vector<int>({1}));
        expect("タップ: 間が 0.6 秒あくと別々の 1 回",
               feedTaps(d, t, 10000 * ms, {8000 * ms, 8600 * ms}) == std::vector<int>({1, 1}));
        expect("タップ: 4 回は 4（何もしない）",
               feedTaps(d, t, 13000 * ms, {11000 * ms, 11252 * ms, 11500 * ms, 11752 * ms}) == std::vector<int>({4}));
        std::vector<int64_t> at;
        feedTaps(d, t, 15000 * ms, {14000 * ms, 14252 * ms}, 0.02, &at);
        expect("タップ: 3 回タップを待つので、2 回タップは最後のタップの 0.45 秒あとに決まる",
               at.size() == 1 && at[0] > 14252 * ms + kTapMaxGapUs && at[0] <= 14252 * ms + kTapMaxGapUs + kTapSamplePeriodUs);
    }
    {
        TapDetector d;
        int64_t t = 0;
        expect("タップ: 頭を回している間（|ω| 1 rad/s）の揺れは数えない",
               feedTaps(d, t, 3000 * ms, {1200 * ms, 1452 * ms, 1700 * ms}, 1.0).empty());
        expect("タップ: 頭を止めてからなら数える", feedTaps(d, t, 5000 * ms, {4000 * ms, 4252 * ms}) == std::vector<int>({2}));
    }
    {
        // 確定したら、その組の最後のタップから 1 秒は無視する
        TapDetector d;
        int64_t t = 0;
        expect("タップ: 確定のあと 1 秒以内のタップは無視する",
               feedTaps(d, t, 2400 * ms, {1000 * ms, 1252 * ms, 1900 * ms, 2152 * ms}) == std::vector<int>({2}));
        expect("タップ: 1 秒たてばまた数える", feedTaps(d, t, 4000 * ms, {3000 * ms, 3252 * ms}) == std::vector<int>({2}));
    }
    {
        // 3 回タップを使わないときは、2 回目でその場で確定し、3 回目は無視する
        TapDetector d;
        d.setFireDoubleImmediately(true);
        int64_t t = 0;
        std::vector<int64_t> at;
        expect("タップ: 3 回タップを使わないときは 2 回目でその場で 2 回タップ",
               feedTaps(d, t, 2000 * ms, {1000 * ms, 1252 * ms}, 0.02, &at) == std::vector<int>({2}) && at.size() == 1 &&
                   at[0] == 1252 * ms);
        expect("タップ: そのときの 3 回目は無視する（1 回にもならない）",
               feedTaps(d, t, 4000 * ms, {3000 * ms, 3252 * ms, 3500 * ms}) == std::vector<int>({2}));
    }
    {
        // 読み取りの間が空いても、途中の組は残す（メインループが一瞬止まっても 2 回・3 回を落とさない）
        TapDetector d;
        int64_t t = 0;
        std::vector<int> got = feedTaps(d, t, 1100 * ms, {1000 * ms});
        t += 40 * ms;  // 40 ms 読めなかった
        const std::vector<int> after = feedTaps(d, t, 3000 * ms, {1300 * ms});
        got.insert(got.end(), after.begin(), after.end());
        expect("タップ: 読めない間があっても、前のタップと組にする", got == std::vector<int>({2}));
        TapDetector waiting;
        int64_t tw = 0;
        std::vector<int> wgot = feedTaps(waiting, tw, 1300 * ms, {1000 * ms, 1252 * ms});
        tw += 40 * ms;  // 2 回目のあと、3 回目を待っている間に 40 ms 読めなかった
        const std::vector<int> wafter = feedTaps(waiting, tw, 3000 * ms, {});
        wgot.insert(wgot.end(), wafter.begin(), wafter.end());
        expect("タップ: 3 回目を待つ間に読めなくても、2 回で確定する", wgot == std::vector<int>({2}));
        int64_t t2 = 0;
        TapDetector fresh;
        expect("タップ: 読み始めた直後（止まっていたか分からない）の揺れは数えない",
               feedTaps(fresh, t2, 1000 * ms, {0, 8 * ms}).empty());
        d.reset();
        t = 10000 * ms;
        expect("タップ: reset() のあとも数えられる", feedTaps(d, t, 12000 * ms, {11000 * ms, 11252 * ms}) == std::vector<int>({2}));
        TapDetector same;
        expect("タップ: 同じ時刻の読み取りは使わない", !same.onSample(0, 0, 0, 0).fired() && same.onSample(0, 0, 5, 0).taps == 0);
    }
    {
        // 設定: 既定は none、書き出して読み直すと同じ
        const Config defaults;
        expect("設定の既定: 2 回・3 回タップは none（姿勢を読まない）",
               defaults.tap[0].type == ActionType::None && defaults.tap[1].type == ActionType::None && !defaults.tapsAssigned());
        Config config;
        config.commands = {{"c1", "通知", {"notify-send", "a"}, false}};
        config.tap[0].type = ActionType::RoomViewToggle;
        config.tap[1] = {ActionType::Command, "c1"};
        const std::string json = configToJson(config);
        Config read;
        std::vector<std::string> warnings;
        std::string error;
        const bool ok = parseConfigText(json, read, warnings, error);
        expect("設定のタップ: 書き出して読み直せる・知らせなし", ok && warnings.empty() && configToJson(read) == json);
        expect("設定のタップ: actions.tap_2 / tap_3 に書く",
               json.find("\"tap_2\": { \"type\": \"room_view_toggle\" }") != std::string::npos &&
                   json.find("\"tap_3\": { \"type\": \"command\", \"command\": \"c1\" }") != std::string::npos);
        expect("設定のタップ: 中身も同じ",
               read.tap[0].type == ActionType::RoomViewToggle && read.tap[1].type == ActionType::Command &&
                   read.tap[1].commandId == "c1" && read.tapsAssigned());
        expect("設定のタップ: slot(6)・slot(7) で引ける",
               &read.slot(kTap2Slot) == &read.tap[0] && &read.slot(kTap3Slot) == &read.tap[1] &&
                   &read.slot(kLongPressSlot) == &read.longPress);
        Config old;
        warnings.clear();
        parseConfigText(R"({ "version": 3, "actions": { "press_1": "none" } })", old, warnings, error);
        expect("設定のタップ: 書いていなければ none・知らせなし",
               warnings.empty() && old.tap[0].type == ActionType::None && old.tap[1].type == ActionType::None);
        warnings.clear();
        parseConfigText(R"({ "actions": { "tap_3": "screenshot", "tap_4": "none" } })", old, warnings, error);
        expect("設定のタップ: 片方だけでも読める・tap_4 は知らないキー",
               old.tap[1].type == ActionType::Screenshot && old.tap[0].type == ActionType::None && old.tapsAssigned() &&
                   warnings.size() == 1 && warnings[0].find("tap_4") != std::string::npos);
        expect("設定のタップ: コマンドを消すとタップの割り当ても none", removeCommand(read, "c1") && read.tap[1].type == ActionType::None);
    }
    {
        // 状態ファイル・文言・視界のお知らせ
        Status status;
        status.lastPress.kind = PressRecord::Kind::Tap;
        status.lastPress.count = 2;
        status.lastPress.action = ActionType::RoomViewToggle;
        status.lastPress.value = "on";
        status.lastPress.at = 1;
        const std::string json = statusToJson(status);
        Status read;
        std::string error;
        expect("状態: タップは kind \"tap\" と回数",
               json.find("\"kind\": \"tap\", \"count\": 2") != std::string::npos && parseStatusText(json, read, error) &&
                   read.lastPress.kind == PressRecord::Kind::Tap && read.lastPress.slot() == kTap2Slot);
        PressRecord three;
        three.kind = PressRecord::Kind::Tap;
        three.count = 3;
        PressRecord four = three;
        four.count = 4;
        expect("状態: 3 回タップの番号は 7、それ以外の回数は -1", three.slot() == kTap3Slot && four.slot() == -1);
        expect("文言: タップの名前",
               slotLabel(Language::Ja, kTap2Slot) == "2 回タップ" && slotLabel(Language::En, kTap3Slot) == "Triple tap" &&
                   slotTitle(Language::Ja, kTap3Slot) == "ヘッドセットを 3 回タップ" &&
                   slotTitle(Language::En, kTap2Slot) == "Tap headset twice" &&
                   slotTitle(Language::Ja, kLongPressSlot) == slotLabel(Language::Ja, kLongPressSlot));
        HudToast toast;
        expect("視界のお知らせ: タップでも出す（2 行目はタップの名前）",
               makeHudToast(status.lastPress, Language::Ja, toast) && toast.title == uiText(Language::Ja).hudPassOn &&
                   toast.sub == "2 回タップ");
    }
    {
        // 設定パネル: タップのカードと割り当て
        using K = PanelAction::Kind;
        Config config;
        expect("パネル: 2 回タップに割り当てる",
               applyPanelAction({K::Assign, static_cast<int>(ActionType::RoomViewToggle), kTap2Slot}, config) &&
                   config.tap[0].type == ActionType::RoomViewToggle);
        expect("パネル: 3 回タップにコマンドを割り当てる",
               applyPanelAction({K::Assign, static_cast<int>(ActionType::Command), kTap3Slot, "c1"}, config) &&
                   config.tap[1].type == ActionType::Command && config.tap[1].commandId == "c1");
        expect("パネル: 範囲外の番号には割り当てない",
               !applyPanelAction({K::Assign, static_cast<int>(ActionType::Screenshot), kSlotCount}, config));
        config.tap[1] = Action();  // 下の列をアクションの側にする（コマンドを割り当てているとコマンドの側になる）
        FontSet fonts;
        fonts.load("/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc");
        SettingsPanel panel(fonts);
        PanelView view;
        view.daemon = PanelView::Daemon::Running;
        for (const Language lang : {Language::Ja, Language::En}) {
            config.language = lang;
            panel.render(config, view);
            double x = 0;
            double y = 0;
            double x2 = 0;
            double y2 = 0;
            const bool cards = panel.buttonCenter({K::SelectSlot, kTap2Slot, 0}, x, y) &&
                               panel.buttonCenter({K::SelectSlot, kTap3Slot, 0}, x2, y2);
            double ax = 0;
            double ay = 0;
            panel.buttonCenter({K::SelectSlot, 0, 0}, ax, ay);
            expect(std::string("パネル: タップのカードが 3 段目にある（") + languageCode(lang) + "）",
                   cards && y > ay && y == y2 && x < x2 && y < SettingsPanel::kHeight);
            panel.pointerDown(x2, y2, lang == Language::Ja ? 0.0 : 1.0);
            panel.render(config, view);
            double cx = 0;
            double cy = 0;
            expect(std::string("パネル: タップのカードを選ぶと、その割り当ての選択肢が出る（") + languageCode(lang) + "）",
                   panel.selectedSlot() == kTap3Slot &&
                       panel.buttonCenter({K::Assign, static_cast<int>(ActionType::None), kTap3Slot}, cx, cy) &&
                       cy < SettingsPanel::kHeight - 40);
        }
    }
}

/**
 * 見本のアプリ（MPRIS のプレーヤー）を作る。
 * @param name バス名の頭を除いた名前（"chromium.instance2" など）
 * @param status PlaybackStatus
 * @param title 曲名
 * @return アプリ
 */
MediaPlayer mediaPlayer(const std::string& name, const std::string& status, const std::string& title = "") {
    MediaPlayer player;
    player.busName = std::string(kMprisPrefix) + name;
    player.status = status;
    player.canPlay = player.canPause = player.canGoNext = player.canGoPrevious = true;
    player.title = title;
    return player;
}

/** 音楽の操作: アプリの選び方・呼ぶメソッド・覚え方・設定の読み書き・文言・お知らせ・設定パネルの「音楽」の側。 */
void testMedia() {
    expect("音楽: MPRIS の名前だけを候補にする（playerctld と頭だけの名前は除く）",
           isMediaPlayerName("org.mpris.MediaPlayer2.chromium.instance2") && isMediaPlayerName("org.mpris.MediaPlayer2.vlc") &&
               !isMediaPlayerName(kPlayerctldName) && !isMediaPlayerName("org.mpris.MediaPlayer2.") &&
               !isMediaPlayerName("org.freedesktop.Notifications") && !isMediaPlayerName(":1.97703"));
    expect("音楽: 種類の見分け", isMediaAction(ActionType::MediaPlayPause) && isMediaAction(ActionType::MediaNext) &&
                                   isMediaAction(ActionType::MediaPrevious) && !isMediaAction(ActionType::MicMuteToggle) &&
                                   !isMediaAction(ActionType::Command) && !isMediaAction(ActionType::None));
    {
        const MediaMemory none;
        expect("音楽: アプリが無ければ選ばない", chooseMediaPlayer({}, none) == -1);
        expect("音楽: 止まっている（Stopped）・状態の分からないアプリは選ばない",
               chooseMediaPlayer({mediaPlayer("a", "Stopped"), mediaPlayer("b", "")}, none) == -1);
        expect("音楽: 再生中のアプリを選ぶ（一時停止中より先）",
               chooseMediaPlayer({mediaPlayer("a", "Paused"), mediaPlayer("b", "Playing"), mediaPlayer("c", "Stopped")}, none) ==
                   1);
        MediaPlayer locked = mediaPlayer("a", "Playing");
        locked.canControl = false;
        expect("音楽: CanControl が false のアプリは選ばない",
               chooseMediaPlayer({locked, mediaPlayer("b", "Paused")}, none) == 1);
        const std::vector<MediaPlayer> two = {mediaPlayer("a", "Playing"), mediaPlayer("b", "Playing")};
        MediaMemory memory;
        expect("音楽: 再生中が 2 つで覚えが無ければ並びの最初", chooseMediaPlayer(two, memory) == 0);
        memory.lastControlled = two[1].busName;
        expect("音楽: 再生中が 2 つなら、前に操作したほう", chooseMediaPlayer(two, memory) == 1);
        const std::vector<MediaPlayer> paused = {mediaPlayer("a", "Paused"), mediaPlayer("b", "Paused"),
                                                 mediaPlayer("c", "Paused")};
        MediaMemory pausedMemory;
        pausedMemory.lastPaused = paused[2].busName;
        pausedMemory.lastControlled = paused[1].busName;
        expect("音楽: 再生中が無ければ、前に一時停止にしたもの（同じタブに戻る）", chooseMediaPlayer(paused, pausedMemory) == 2);
        pausedMemory.lastPaused = std::string(kMprisPrefix) + "gone";
        expect("音楽: 一時停止にしたものがもう無ければ、前に操作した一時停止中のもの",
               chooseMediaPlayer(paused, pausedMemory) == 1);
        expect("音楽: 覚えが無ければ一時停止中の最初", chooseMediaPlayer(paused, none) == 0);
        MediaMemory both;
        both.lastPaused = paused[0].busName;
        expect("音楽: 前に一時停止にしたものがあっても、ほかで再生中ならそちら",
               chooseMediaPlayer({paused[0], mediaPlayer("x", "Playing")}, both) == 1);
    }
    {
        MediaPlayer playing = mediaPlayer("a", "Playing");
        MediaPlayer paused = mediaPlayer("a", "Paused");
        const MediaCall pause = mediaCallFor(ActionType::MediaPlayPause, playing);
        const MediaCall play = mediaCallFor(ActionType::MediaPlayPause, paused);
        expect("音楽: 再生中なら Pause、一時停止中なら Play（結果の値つき）",
               pause.method == "Pause" && pause.value == kMediaPaused && play.method == "Play" && play.value == kMediaPlaying);
        expect("音楽: 次の曲・前の曲", mediaCallFor(ActionType::MediaNext, playing).method == "Next" &&
                                           mediaCallFor(ActionType::MediaNext, playing).value == kMediaNext &&
                                           mediaCallFor(ActionType::MediaPrevious, paused).method == "Previous" &&
                                           mediaCallFor(ActionType::MediaPrevious, paused).value == kMediaPrevious);
        playing.canPause = false;
        paused.canPlay = false;
        playing.canGoNext = false;
        paused.canGoPrevious = false;
        expect("音楽: Can… が false なら呼ばない（対応していない）",
               mediaCallFor(ActionType::MediaPlayPause, playing).method.empty() &&
                   mediaCallFor(ActionType::MediaPlayPause, paused).method.empty() &&
                   mediaCallFor(ActionType::MediaNext, playing).method.empty() &&
                   mediaCallFor(ActionType::MediaPrevious, paused).method.empty() &&
                   mediaCallFor(ActionType::MediaNext, paused).method == "Next");
        MediaMemory memory;
        rememberMediaPlayer(memory, "A", kMediaPaused);
        const bool pausedOk = memory.lastControlled == "A" && memory.lastPaused == "A";
        rememberMediaPlayer(memory, "B", kMediaNext);
        const bool nextOk = memory.lastControlled == "B" && memory.lastPaused == "A";
        rememberMediaPlayer(memory, "A", kMediaPlaying);
        expect("音楽: 操作したアプリを覚える（一時停止にしたものは、再生に戻すまで）",
               pausedOk && nextOk && memory.lastControlled == "A" && memory.lastPaused.empty());
    }
    {
        // 設定ファイル: 3 つの種類を読み書きできる（version は 3 のまま）
        Config config;
        std::vector<std::string> warnings;
        std::string error;
        const bool ok = parseConfigText(R"({ "version": 3, "actions": { "press_1": "media_play_pause",
                                             "press_4": { "type": "media_next" }, "tap_2": { "type": "media_previous" } } })",
                                        config, warnings, error);
        expect("設定: media_play_pause / media_next / media_previous を読める・知らせなし",
               ok && warnings.empty() && config.press[0].type == ActionType::MediaPlayPause &&
                   config.press[3].type == ActionType::MediaNext && config.tap[0].type == ActionType::MediaPrevious);
        const std::string json = configToJson(config);
        Config again;
        expect("設定: 音楽の操作を書いて読み直すと同じ",
               parseConfigText(json, again, warnings, error) && configToJson(again) == json &&
                   json.find("\"press_1\": { \"type\": \"media_play_pause\" }") != std::string::npos &&
                   json.find("\"tap_2\": { \"type\": \"media_previous\" }") != std::string::npos);
        expect("設定: 音楽の操作に SteamVR は要らない（--test-action でつながない）",
               !actionSpec(ActionType::MediaPlayPause).needsVr && !actionSpec(ActionType::MediaNext).needsVr &&
                   !actionSpec(ActionType::MediaPrevious).needsVr);
    }
    expect("文言: 音楽の操作の名前（日英）",
           std::string(actionLabel(Language::Ja, ActionType::MediaPlayPause)) == "再生・一時停止" &&
               std::string(actionLabel(Language::Ja, ActionType::MediaNext)) == "次の曲" &&
               std::string(actionLabel(Language::Ja, ActionType::MediaPrevious)) == "前の曲" &&
               std::string(actionLabel(Language::En, ActionType::MediaPlayPause)) == "Play / pause" &&
               std::string(actionLabel(Language::En, ActionType::MediaNext)) == "Next track" &&
               std::string(actionLabel(Language::En, ActionType::MediaPrevious)) == "Previous track");
    expect("文言: 音楽の操作のアイコン", iconForAction(ActionType::MediaPlayPause) == Icon::PlayPause &&
                                             iconForAction(ActionType::MediaNext) == Icon::SkipNext &&
                                             iconForAction(ActionType::MediaPrevious) == Icon::SkipPrevious);
    {
        PressRecord press;
        press.kind = PressRecord::Kind::Count;
        press.count = 1;
        press.action = ActionType::MediaPlayPause;
        press.value = kMediaPaused;
        press.track = "アトラクトライト / *Luna - Covered by しほ";
        HudToast toast;
        expect("お知らせ: 一時停止（2 行目は曲名）",
               makeHudToast(press, Language::Ja, toast) && toast.title == "一時停止" && toast.sub == press.track &&
                   toast.icon == Icon::Pause && !toast.error);
        press.value = kMediaPlaying;
        expect("お知らせ: 再生（英語）", makeHudToast(press, Language::En, toast) && toast.title == "Playing" &&
                                              toast.icon == Icon::Play && toast.tone == HudToast::Tone::Accent);
        press.action = ActionType::MediaNext;
        press.value = kMediaNext;
        press.track.clear();
        press.count = 2;
        expect("お知らせ: 次の曲で曲名がまだ分からなければ、2 行目は押し方",
               makeHudToast(press, Language::Ja, toast) && toast.title == "次の曲" && toast.sub == "2 回押し" &&
                   toast.icon == Icon::SkipNext);
        press.kind = PressRecord::Kind::Tap;
        press.action = ActionType::MediaPrevious;
        press.value = kMediaPrevious;
        press.track = "Song";
        expect("お知らせ: 前の曲（タップ）", makeHudToast(press, Language::Ja, toast) && toast.title == "前の曲" &&
                                                  toast.sub == "Song" && toast.icon == Icon::SkipPrevious);
        press.kind = PressRecord::Kind::Count;
        press.count = 1;
        press.action = ActionType::MediaPlayPause;
        press.ok = false;
        press.value.clear();
        press.track.clear();
        press.reason = kReasonNoPlayer;
        expect("お知らせ: 再生しているアプリが無いときの失敗",
               makeHudToast(press, Language::Ja, toast) && toast.error && toast.title == "再生・一時停止できません" &&
                   toast.sub == "1 回押し・再生しているアプリが見つかりません");
        press.action = ActionType::MediaNext;
        press.reason = kReasonMediaUnsupported;
        expect("お知らせ: 対応していないときの失敗（英語）",
               makeHudToast(press, Language::En, toast) && toast.error && toast.title == "Couldn't skip to the next track" &&
                   toast.sub == "1 press · Not supported by this app");
    }
    {
        // 設定パネル: 割り当ての列の「音楽」の側
        using K = PanelAction::Kind;
        Config config;
        expect("パネル: 音楽の操作を割り当てる",
               applyPanelAction({K::Assign, static_cast<int>(ActionType::MediaNext), 3}, config) &&
                   config.press[3].type == ActionType::MediaNext && config.press[3].commandId.empty());
        FontSet fonts;
        fonts.load("/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc");
        SettingsPanel panel(fonts);
        PanelView view;
        view.daemon = PanelView::Daemon::Running;
        for (const Language lang : {Language::Ja, Language::En}) {
            const std::string tag = std::string("（") + languageCode(lang) + "）";
            config.language = lang;
            panel.setTab(PanelTab::Assign);
            panel.setSelectedSlot(3);
            panel.render(config, view);
            double x = 0;
            double y = 0;
            double bx = 0;
            double by = 0;
            double bw = 0;
            double bh = 0;
            // 選んだカードが音楽の操作なら、音楽の側（3 つが 1 段に並び、アクションの側の選択肢は無い）
            bool row = true;
            double firstY = -1;
            double lastRight = 0;
            for (const ActionType type : {ActionType::MediaPlayPause, ActionType::MediaNext, ActionType::MediaPrevious}) {
                if (!panel.buttonBounds({K::Assign, static_cast<int>(type), 3}, bx, by, bw, bh)) {
                    row = false;
                    continue;
                }
                if (firstY < 0) firstY = by;
                row = row && by == firstY && bx >= lastRight && bx + bw <= SettingsPanel::kWidth - 24 &&
                      by + bh <= SettingsPanel::kHeight - 40;
                lastRight = bx + bw;
            }
            expect("パネル: 音楽の操作を割り当てたカードを選ぶと、音楽の側の 3 つが 1 段に並ぶ" + tag,
                   row && !panel.buttonCenter({K::Assign, static_cast<int>(ActionType::MicMuteToggle), 3}, x, y));
            expect("パネル: ［アクション｜音楽｜コマンド］の切り替えがある" + tag,
                   panel.buttonCenter({K::PickerActions, 0, 0}, x, y) && panel.buttonCenter({K::PickerMedia, 0, 0}, x, y) &&
                       panel.buttonCenter({K::PickerCommands, 0, 0}, x, y));
            panel.buttonCenter({K::PickerActions, 0, 0}, x, y);
            panel.pointerDown(x, y, 0.0);
            panel.render(config, view);
            double ox = 0;
            double oy = 0;
            expect("パネル: アクションの側は 9 つ（音楽の操作は入らない）" + tag,
                   panel.buttonCenter({K::Assign, static_cast<int>(ActionType::MicMuteToggle), 3}, x, y) &&
                       panel.buttonCenter({K::Assign, static_cast<int>(ActionType::None), 3}, ox, oy) && oy > y &&
                       !panel.buttonCenter({K::Assign, static_cast<int>(ActionType::MediaNext), 3}, x, y));
            panel.buttonCenter({K::PickerMedia, 0, 0}, x, y);
            panel.pointerDown(x, y, 1.0);
            panel.render(config, view);
            const bool media = panel.buttonCenter({K::Assign, static_cast<int>(ActionType::MediaPlayPause), 3}, x, y);
            const PanelAction pressed = panel.pointerDown(x, y, 2.0);
            expect("パネル: 音楽の側に切り替えて、再生・一時停止を押すと割り当ての操作が返る" + tag,
                   media && pressed == PanelAction({K::Assign, static_cast<int>(ActionType::MediaPlayPause), 3}));
        }
    }
}

/** 視点のリセット: リセットしてよいかの判断・間引き・設定・文言・お知らせ・パネルのアクションの側。 */
void testRecenter() {
    {
        RecenterGate gate;
        RecenterState worn;
        worn.connected = true;
        worn.hmdInUse = true;
        worn.hmdTracking = true;
        RecenterState off = worn;
        off.connected = false;
        RecenterState standby = worn;
        standby.hmdInUse = false;
        RecenterState lost = worn;
        lost.hmdTracking = false;
        expect("視点のリセット: かぶっていて姿勢が取れていればリセットする", gate.decide(worn, 1000) == RecenterDecision::Reset);
        expect("視点のリセット: SteamVR につながっていなければしない", gate.decide(off, 1000) == RecenterDecision::NoSteamVr);
        expect("視点のリセット: かぶっていなければしない（姿勢が取れていても）",
               gate.decide(standby, 1000) == RecenterDecision::NotWorn);
        expect("視点のリセット: 姿勢が取れていなければしない", gate.decide(lost, 1000) == RecenterDecision::NotTracking);
        gate.markReset(1000);
        expect("視点のリセット: 1 秒以内の 2 回目はし直さない",
               gate.decide(worn, 1000) == RecenterDecision::Repeated && gate.decide(worn, 1999) == RecenterDecision::Repeated);
        expect("視点のリセット: 1 秒たてばまたリセットする", gate.decide(worn, 2000) == RecenterDecision::Reset);
        expect("視点のリセット: 1 秒以内でも、かぶっていなければ失敗（間引きより先）",
               gate.decide(standby, 1500) == RecenterDecision::NotWorn);
        RecenterGate fresh;
        expect("視点のリセット: 一度もリセットしていなければ、時刻が 0 でも間引かない",
               fresh.decide(worn, 0) == RecenterDecision::Reset);
        expect("視点のリセット: 失敗の理由のコード",
               std::string(recenterFailureReason(RecenterDecision::NoSteamVr)) == "no_steamvr" &&
                   std::string(recenterFailureReason(RecenterDecision::NotWorn)) == kReasonNotWorn &&
                   std::string(recenterFailureReason(RecenterDecision::NotTracking)) == kReasonNotTracking &&
                   std::string(recenterFailureReason(RecenterDecision::Reset)).empty() &&
                   std::string(recenterFailureReason(RecenterDecision::Repeated)).empty());
    }
    {
        // 設定ファイル: recenter を読み書きできる（version は 3 のまま）
        Config config;
        std::vector<std::string> warnings;
        std::string error;
        const bool ok = parseConfigText(R"({ "version": 3, "actions": { "long_press": "recenter",
                                             "tap_2": { "type": "recenter" } } })",
                                        config, warnings, error);
        expect("設定: recenter を読める・知らせなし",
               ok && warnings.empty() && config.longPress.type == ActionType::Recenter &&
                   config.tap[0].type == ActionType::Recenter);
        const std::string json = configToJson(config);
        Config again;
        expect("設定: 視点のリセットを書いて読み直すと同じ",
               parseConfigText(json, again, warnings, error) && configToJson(again) == json &&
                   json.find("\"long_press\": { \"type\": \"recenter\" }") != std::string::npos &&
                   json.find("\"tap_2\": { \"type\": \"recenter\" }") != std::string::npos);
        expect("設定: 視点のリセットには SteamVR が要る（--test-action でつなぐ）", actionSpec(ActionType::Recenter).needsVr);
    }
    expect("文言: 視点のリセットの名前・カードの 2 行目（日英）",
           std::string(actionLabel(Language::Ja, ActionType::Recenter)) == "視点のリセット" &&
               std::string(actionLabel(Language::En, ActionType::Recenter)) == "Recenter view" &&
               std::string(uiText(Language::Ja).stRecenter) == "今向いている方を正面にします");
    expect("文言: 視点のリセットのアイコン", iconForAction(ActionType::Recenter) == Icon::Recenter);
    {
        PressRecord press;
        press.kind = PressRecord::Kind::Long;
        press.action = ActionType::Recenter;
        press.ok = true;
        press.value = kRecentered;
        HudToast toast;
        expect("お知らせ: 視点のリセット（2 行目は押し方）",
               makeHudToast(press, Language::Ja, toast) && toast.title == "視点をリセットしました" && toast.sub == "長押し" &&
                   toast.icon == Icon::Recenter && !toast.error);
        press.kind = PressRecord::Kind::Tap;
        press.count = 2;
        press.value = kRecenterRepeated;
        expect("お知らせ: 1 秒以内の 2 回目も同じお知らせ（英語・タップ）",
               makeHudToast(press, Language::En, toast) && toast.title == "View recentered" && !toast.error);
        press.kind = PressRecord::Kind::Long;
        press.ok = false;
        press.value.clear();
        press.reason = kReasonNotWorn;
        expect("お知らせ: かぶっていないときの失敗",
               makeHudToast(press, Language::Ja, toast) && toast.error && toast.title == "視点をリセットできません" &&
                   toast.sub == "長押し・ヘッドセットをかぶっている間だけ使えます");
        press.reason = kReasonNotTracking;
        expect("お知らせ: 姿勢が取れていないときの失敗（英語）",
               makeHudToast(press, Language::En, toast) && toast.error && toast.title == "Couldn't recenter the view" &&
                   toast.sub == "Long press · The headset isn't being tracked");
        press.reason = "no_steamvr";
        expect("お知らせ: SteamVR につながっていないときの失敗",
               makeHudToast(press, Language::Ja, toast) && toast.sub == "長押し・SteamVR につながっていません");
    }
    {
        // 設定パネル: アクションの側は 5 列 × 2 段（9 つ。「なし」は最後）
        using K = PanelAction::Kind;
        Config config;
        expect("パネル: 視点のリセットを割り当てる",
               applyPanelAction({K::Assign, static_cast<int>(ActionType::Recenter), 5}, config) &&
                   config.longPress.type == ActionType::Recenter && config.longPress.commandId.empty());
        FontSet fonts;
        fonts.load("/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc");
        SettingsPanel panel(fonts);
        PanelView view;
        view.daemon = PanelView::Daemon::Running;
        const ActionType order[] = {ActionType::MicMuteToggle, ActionType::BrightnessCycle, ActionType::RoomViewToggle,
                                    ActionType::Screenshot,    ActionType::PerfOverlayToggle, ActionType::DashboardOpen,
                                    ActionType::Recenter,      ActionType::OriginalAux,     ActionType::None};
        for (const Language lang : {Language::Ja, Language::En}) {
            const std::string tag = std::string("（") + languageCode(lang) + "）";
            config.language = lang;
            panel.setTab(PanelTab::Assign);
            panel.setSelectedSlot(5);
            panel.render(config, view);
            bool grid = true;
            double rowY[2] = {-1, -1};
            double lastRight[2] = {0, 0};
            int index = 0;
            for (const ActionType type : order) {
                double bx = 0;
                double by = 0;
                double bw = 0;
                double bh = 0;
                const int row = index < 5 ? 0 : 1;
                ++index;
                if (!panel.buttonBounds({K::Assign, static_cast<int>(type), 5}, bx, by, bw, bh)) {
                    grid = false;
                    continue;
                }
                if (rowY[row] < 0) rowY[row] = by;
                grid = grid && by == rowY[row] && bx >= lastRight[row] && bx >= 24 &&
                       bx + bw <= SettingsPanel::kWidth - 24 && by + bh <= SettingsPanel::kHeight - 40;
                lastRight[row] = bx + bw;
            }
            expect("パネル: アクションの側は 5 つと 4 つの 2 段で、視点のリセットは 2 段目・「なし」は最後" + tag,
                   grid && rowY[1] > rowY[0]);
            double x = 0;
            double y = 0;
            panel.buttonCenter({K::Assign, static_cast<int>(ActionType::Recenter), 5}, x, y);
            const PanelAction pressed = panel.pointerDown(x, y, 0.0);
            expect("パネル: 視点のリセットを押すと割り当ての操作が返る" + tag,
                   pressed == PanelAction({K::Assign, static_cast<int>(ActionType::Recenter), 5}));
            panel.pointerUp();
        }
    }
}

/**
 * 終了コードだけの実行結果を作る（目のアクションの偽物の frameeyeosc-panel 用）。
 * @param code 終了コード（0 なら成功）
 * @param err 標準エラーの 1 行
 * @return 結果
 */
CommandResult exitResult(int code, const std::string& err = "") {
    CommandResult r;
    r.status = code == 0 ? CommandResult::Status::Ok : CommandResult::Status::Failed;
    r.exitCode = code;
    r.err = err;
    return r;
}

/** 目のアクション: 版の確かめ・終了コードの読み方・フィットの切り替え・設定・文言・お知らせ・パネルの目の側。 */
void testEye() {
    expect("目: 種類の見分け", isEyeAction(ActionType::EyeRecenter) && isEyeAction(ActionType::EyeFit) &&
                                 isEyeAction(ActionType::EyeRelearn) &&
                                 !isEyeAction(ActionType::Recenter) && !isEyeAction(ActionType::MediaNext) &&
                                 !isEyeAction(ActionType::None));
    {
        std::string version;
        expect("目: --version の出力から版を読む",
               parseEyePanelVersion("frameeyeosc-panel 0.7.9\n", version) && version == "0.7.9");
        expect("目: 2 けたの版も読む（0.7.10）",
               parseEyePanelVersion("frameeyeosc-panel 0.7.10\n", version) && version == "0.7.10");
        expect("目: 版が無い・形の違う出力は読まない",
               !parseEyePanelVersion("Usage: frameeyeosc-panel [options]\n", version) &&
                   !parseEyePanelVersion("frameeyeosc 0.8.0\n", version) && !parseEyePanelVersion("", version));
        expect("目: 見つからなければ入っていない・対応していなければ更新が必要・対応していれば使える",
               eyePanelStateFor(false, true) == EyePanelState::Missing &&
                   eyePanelStateFor(true, false) == EyePanelState::Outdated &&
                   eyePanelStateFor(true, true) == EyePanelState::Ready);
        expect("目: --action bogus が 1 と unknown action なら対応している",
               eyePanelSupportsActions(exitResult(1, "bogus: unknown action (use recenter, fit, stop or open)\n")));
        expect("目: 0.7.9 のように 2 で「Unknown argument」なら対応していない",
               !eyePanelSupportsActions(exitResult(2, "Unknown argument: --action\n")));
        CommandResult timeout;
        timeout.status = CommandResult::Status::Timeout;
        CommandResult spawn;
        spawn.status = CommandResult::Status::SpawnFailed;
        expect("目: 1 でも unknown action が無い・0 で終わる・時間切れ・起動できないなら対応していない",
               !eyePanelSupportsActions(exitResult(1, "action: bad arguments\n")) &&
                   !eyePanelSupportsActions(exitResult(0, "bogus: started\n")) && !eyePanelSupportsActions(timeout) &&
                   !eyePanelSupportsActions(spawn));
        expect("目: 確かめるときの引数は --action bogus",
               eyeProbeArgv("/p") == std::vector<std::string>({"/p", "--action", "bogus"}));
        const std::vector<std::string> argv = eyeActionArgv("/p", EyeCall::Fit);
        expect("目: 引数は --action fit",
               argv == std::vector<std::string>({"/p", "--action", "fit"}) &&
                   eyeActionArgv("/p", EyeCall::Recenter)[2] == "recenter" && eyeActionArgv("/p", EyeCall::Stop)[2] == "stop");
    }
    {
        // 終了コード → 結果
        const ActionOutcome accepted = eyeOutcomeFor(EyeCall::Recenter, exitResult(0, "re-center started"));
        expect("目: 0 は成功（正面合わせを始めた。標準エラーの 1 行はログに）",
               accepted.ok && accepted.value == kEyeRecentering && accepted.reason.empty() &&
                   accepted.detail.find("re-center started") != std::string::npos);
        expect("目: フィットの 0 は fitting", eyeOutcomeFor(EyeCall::Fit, exitResult(0)).value == kEyeFitting);
        struct Case {
            int code;
            const char* reason;
        };
        const Case cases[] = {
            {1, kReasonEyeOutdated},    {2, kReasonEyePanelStopped}, {3, kReasonEyeNoAnswer}, {4, kReasonEyeNotRunning},
            {5, kReasonEyeNotFitted},   {6, kReasonEyeBusy},         {7, "failed"},           {8, kReasonEyeLocked},
            {9, kReasonNotWorn},        {10, kReasonEyeRecenterBusy}, {11, kReasonEyeNoCameras}, {42, "failed"},
        };
        bool all = true;
        for (const Case& c : cases) {
            const ActionOutcome out = eyeOutcomeFor(EyeCall::Recenter, exitResult(c.code, "panel said"));
            all = all && !out.ok && out.reason == c.reason && out.value.empty() &&
                  out.detail.find("終了コード " + std::to_string(c.code)) != std::string::npos &&
                  out.detail.find("panel said") != std::string::npos;
        }
        expect("目: 終了コード 1〜11・ほかを理由のコードにする（説明に終了コードと標準エラー）", all);
        expect("目: 読み方の表: フィットを止めるのは 6 だけ（10 は止めない）・止めたことにするのは 7 だけ・0 と 12 は表に無い",
               findEyeExitRule(6) != nullptr && findEyeExitRule(6)->stopsFit && !findEyeExitRule(4)->stopsFit &&
                   !findEyeExitRule(8)->stopsFit && !findEyeExitRule(10)->stopsFit && !findEyeExitRule(9)->stopsFit &&
                   findEyeExitRule(7)->okForStop && !findEyeExitRule(6)->okForStop && findEyeExitRule(0) == nullptr &&
                   findEyeExitRule(12) == nullptr && findEyeExitRule(42) == nullptr && !findEyeExitRule(11)->stopsFit &&
                   findEyeExitRule(127) == nullptr);
        expect("目: 表に無いコード（12 など）は「うまくいきませんでした」",
               reasonText(Language::Ja, eyeOutcomeFor(EyeCall::Fit, exitResult(12)).reason) == "うまくいきませんでした" &&
                   reasonText(Language::En, eyeOutcomeFor(EyeCall::Recenter, exitResult(99)).reason) == "Something went wrong");
        CommandResult timeout;
        timeout.status = CommandResult::Status::Timeout;
        CommandResult spawn;
        spawn.status = CommandResult::Status::SpawnFailed;
        CommandResult signal;
        signal.status = CommandResult::Status::Failed;
        signal.exitCode = -1;
        expect("目: 時間切れは答えなし・起動できない（exec の失敗。127 も runCommand がこれにする）は入っていない・シグナルは失敗",
               eyeOutcomeFor(EyeCall::Fit, timeout).reason == kReasonEyeNoAnswer &&
                   eyeOutcomeFor(EyeCall::Fit, spawn).reason == kReasonEyeMissing &&
                   eyeOutcomeFor(EyeCall::Stop, spawn).reason == kReasonEyeMissing &&
                   eyeOutcomeFor(EyeCall::Fit, signal).reason == "failed");
        const ActionOutcome stopped = eyeOutcomeFor(EyeCall::Stop, exitResult(0));
        const ActionOutcome already = eyeOutcomeFor(EyeCall::Stop, exitResult(7));
        const ActionOutcome stopFailed = eyeOutcomeFor(EyeCall::Stop, exitResult(3));
        expect("目: 止めるのは 0 も 7（もう終わっていた）も成功、失敗でも値は stopped",
               stopped.ok && stopped.value == kEyeStopped && already.ok && already.value == kEyeStopped &&
                   !stopFailed.ok && stopFailed.value == kEyeStopped && stopFailed.reason == kReasonEyeNoAnswer);
    }
    {
        // 動かし方（偽物の frameeyeosc-panel）
        std::vector<std::string> calls;
        std::vector<int> codes;
        size_t next = 0;
        constexpr int kTimedOut = -100;  // codes にこれを入れると、その回は時間切れ
        const EyeRunner fake = [&](const std::vector<std::string>& argv, int timeoutMs) {
            calls.push_back(argv.size() == 3 && argv[0] == "/eye" && argv[1] == "--action" && timeoutMs == kEyeActionTimeoutMs
                                ? argv[2]
                                : "?");
            const int code = next < codes.size() ? codes[next++] : 0;
            if (code != kTimedOut) return exitResult(code);
            CommandResult timedOut;
            timedOut.status = CommandResult::Status::Timeout;
            return timedOut;
        };
        const auto run = [&](ActionType type, const EyePanelInfo& info, std::vector<int> exits) {
            calls.clear();
            codes = exits;
            next = 0;
            return runEyeAction(type, info, fake);
        };
        EyePanelInfo ready;
        ready.state = EyePanelState::Ready;
        ready.path = "/eye";
        ready.version = "0.8.0";
        EyePanelInfo missing;
        missing.state = EyePanelState::Missing;
        EyePanelInfo outdated = ready;
        outdated.state = EyePanelState::Outdated;
        outdated.version = "0.7.9";
        ActionOutcome out = run(ActionType::EyeFit, missing, {0});
        expect("目: 入っていなければ頼まずに失敗", !out.ok && out.reason == kReasonEyeMissing && calls.empty());
        out = run(ActionType::EyeRecenter, outdated, {0});
        expect("目: --action に対応していなければ頼まずに更新が必要（説明に今の版）",
               !out.ok && out.reason == kReasonEyeOutdated && calls.empty() &&
                   out.detail.find("0.7.9") != std::string::npos);
        out = run(ActionType::EyeRecenter, ready, {0});
        expect("目: 正面合わせを頼む", out.ok && out.value == kEyeRecentering && calls == std::vector<std::string>({"recenter"}));
        out = run(ActionType::EyeRecenter, ready, {6});
        expect("目: 正面合わせでフィット中（6）は止めずに失敗",
               !out.ok && out.reason == kReasonEyeBusy && calls == std::vector<std::string>({"recenter"}));
        out = run(ActionType::EyeRecenter, ready, {5});
        expect("目: 正面合わせでフィットをまだしていない（5）",
               !out.ok && out.reason == kReasonEyeNotFitted && calls.size() == 1);
        out = run(ActionType::EyeFit, ready, {0});
        expect("目: フィットを頼む", out.ok && out.value == kEyeFitting && calls == std::vector<std::string>({"fit"}));
        out = run(ActionType::EyeFit, ready, {6, 0});
        expect("目: フィットの途中（6）なら止める（切り替え）",
               out.ok && out.value == kEyeStopped && calls == std::vector<std::string>({"fit", "stop"}) &&
                   out.detail.find("止めます") != std::string::npos);
        out = run(ActionType::EyeFit, ready, {6, 7});
        expect("目: 止める前に終わっていた（7）も止めたことにする",
               out.ok && out.value == kEyeStopped && calls.size() == 2);
        out = run(ActionType::EyeFit, ready, {6, 2});
        expect("目: 止められなければ失敗（値は stopped で、お知らせは「止められません」）",
               !out.ok && out.value == kEyeStopped && out.reason == kReasonEyePanelStopped && calls.size() == 2);
        out = run(ActionType::EyeFit, ready, {kTimedOut});
        expect("目: フィットが時間切れなら止めずに答えなし",
               !out.ok && out.reason == kReasonEyeNoAnswer && out.value.empty() && calls == std::vector<std::string>({"fit"}));
        out = run(ActionType::EyeFit, ready, {4});
        expect("目: フィットで frameeyeosc が動いていない（4）は止めない",
               !out.ok && out.reason == kReasonEyeNotRunning && out.value.empty() && calls.size() == 1);
        out = run(ActionType::EyeFit, ready, {9});
        expect("目: フィットで目のデータが来ない（9）は、かぶっている間だけ",
               !out.ok && out.reason == kReasonNotWorn && calls.size() == 1);
        out = run(ActionType::EyeFit, ready, {10});
        expect("目: フィットで正面合わせの途中（10）は止めずに失敗",
               !out.ok && out.reason == kReasonEyeRecenterBusy && out.value.empty() &&
                   calls == std::vector<std::string>({"fit"}));
        out = run(ActionType::EyeRecenter, ready, {10});
        expect("目: 正面合わせで正面合わせの途中（10）", !out.ok && out.reason == kReasonEyeRecenterBusy && calls.size() == 1);
        out = run(ActionType::EyeRecenter, ready, {9});
        expect("目: 正面合わせで目のデータが来ない（9）", !out.ok && out.reason == kReasonNotWorn && calls.size() == 1);
        out = run(ActionType::EyeRelearn, ready, {0});
        expect("目: 覚え直しを頼む", out.ok && out.value == kEyeRelearning && calls == std::vector<std::string>({"relearn"}));
        out = run(ActionType::EyeRelearn, ready, {6});
        expect("目: 覚え直しでフィット中（6）は止めずに失敗（切り替えではない）",
               !out.ok && out.reason == kReasonEyeBusy && calls == std::vector<std::string>({"relearn"}));
        out = run(ActionType::EyeRelearn, ready, {9});
        expect("目: 覚え直しで目のデータが来ない（9）", !out.ok && out.reason == kReasonNotWorn && calls.size() == 1);
        out = run(ActionType::EyeRelearn, ready, {11});
        expect("目: 覚え直しで目のカメラが使えない（11）", !out.ok && out.reason == kReasonEyeNoCameras && calls.size() == 1);
        out = run(ActionType::EyeRelearn, outdated, {0});
        expect("目: 対応していない版には覚え直しも頼まない", !out.ok && out.reason == kReasonEyeOutdated && calls.empty());
        expect("目: 11 のわけを標準エラーから読む（校正中・映像が無い・ほかはそのまま）",
               eyeOutcomeFor(EyeCall::Relearn, exitResult(11, "relearn: eye cameras not ready (calibrating)\n")).reason ==
                       kReasonEyeCamerasCalibrating &&
                   eyeOutcomeFor(EyeCall::Relearn, exitResult(11, "relearn: eye cameras not ready (no eye video)\n")).reason ==
                       kReasonEyeCamerasNoVideo &&
                   eyeOutcomeFor(EyeCall::Relearn,
                                 exitResult(11, "relearn: eye cameras not in use or not ready (see the panel's Eye cameras tab)\n"))
                           .reason == kReasonEyeNoCameras &&
                   eyeOutcomeFor(EyeCall::Relearn, exitResult(2, "relearn: calibrating\n")).reason == kReasonEyePanelStopped);
        expect("目: 覚え直しの引数は --action relearn", eyeActionArgv("/p", EyeCall::Relearn)[2] == "relearn");
        out = run(ActionType::EyeFit, ready, {8});
        expect("目: フィットの値がコマンドラインで固定（8）は止めずに失敗",
               !out.ok && out.reason == kReasonEyeLocked && out.value.empty() && calls == std::vector<std::string>({"fit"}));
        out = run(ActionType::EyeRecenter, ready, {8});
        expect("目: 正面合わせでも固定（8）は失敗", !out.ok && out.reason == kReasonEyeLocked && calls.size() == 1);
        EyePanelInfo unknownVersion = ready;
        unknownVersion.version.clear();
        out = run(ActionType::EyeFit, unknownVersion, {0});
        expect("目: 版が読めなくても頼んでみる", out.ok && calls.size() == 1);
        out = run(ActionType::Recenter, ready, {0});
        expect("目: 目のアクションでなければ頼まない", !out.ok && calls.empty());
    }
    {
        // frameeyeosc-panel を確かめる係: HOME と PATH を一時フォルダにして、偽物のスクリプトで
        char base[] = "/tmp/fas-eye-test-XXXXXX";
        const bool made = mkdtemp(base) != nullptr;
        const std::string root = made ? base : "/tmp";
        const std::string bin = root + "/.local/bin";
        const std::string script = bin + "/frameeyeosc-panel";
        const std::string log = root + "/calls.log";
        // --action に対応した版は bogus を 1 と "unknown action" ではね、古い版は 0.7.9 と同じく 2 と "Unknown argument"
        const auto writeScript = [&](const std::string& version, bool supports) {
            std::string error;
            const std::string action = supports ? "echo \"$2: unknown action (use recenter, fit, stop or open)\" >&2; exit 1"
                                                : "echo \"Unknown argument: $1\" >&2; exit 2";
            writeFilePrivately(script,
                               "#!/bin/sh\necho \"$*\" >> " + log + "\nif [ \"$1\" = --action ]; then " + action +
                                   "; fi\necho \"frameeyeosc-panel " + version + "\"\n",
                               error);
            ::chmod(script.c_str(), 0700);
        };
        const auto calls = [&]() {
            std::string text;
            readTextFile(log, text);
            return text;
        };
        const auto callCount = [&]() {
            std::string text;
            readTextFile(log, text);
            return static_cast<int>(std::count(text.begin(), text.end(), '\n'));
        };
        const char* oldHome = std::getenv("HOME");
        const std::string savedHome = oldHome != nullptr ? oldHome : "";
        const char* oldPath = std::getenv("PATH");
        const std::string savedPath = oldPath != nullptr ? oldPath : "";
        ::setenv("HOME", root.c_str(), 1);
        ::setenv("PATH", (root + "/nowhere").c_str(), 1);
        EyePanelProbe probe;
        const bool missingFirst = probe.refresh() && probe.info().state == EyePanelState::Missing;
        ::mkdir((root + "/.local").c_str(), 0700);
        ::mkdir(bin.c_str(), 0700);
        writeScript("0.7.9", false);
        const bool outdated = probe.refresh() && probe.info().state == EyePanelState::Outdated &&
                              probe.info().version == "0.7.9" && probe.info().path == script &&
                              calls() == "--action bogus\n--version\n";
        const bool cached = !probe.refresh() && !probe.refresh() && callCount() == 2;
        ::usleep(20 * 1000);  // 更新時刻が変わるように
        writeScript("0.7.9", true);
        const bool updated = probe.refresh() && probe.info().state == EyePanelState::Ready && probe.info().version == "0.7.9" &&
                             callCount() == 4;
        ::unlink(script.c_str());
        const bool removed = probe.refresh() && probe.info().state == EyePanelState::Missing && probe.info().path.empty();
        if (oldHome != nullptr) ::setenv("HOME", savedHome.c_str(), 1);
        if (oldPath != nullptr) ::setenv("PATH", savedPath.c_str(), 1);
        ::unlink(log.c_str());
        ::rmdir(bin.c_str());
        ::rmdir((root + "/.local").c_str());
        if (made) ::rmdir(root.c_str());
        expect("目: 入っていなければ Missing（~/.local/bin と PATH を探す）", made && missingFirst);
        expect("目: 見つかったら --action bogus で確かめ、--version で版を読む（2 で終わる版は更新が必要）", outdated);
        expect("目: 実行ファイルが変わらなければ起動し直さない", cached);
        expect("目: 実行ファイルが変わったら確かめ直す（版が 0.7.9 のままでも、--action に対応していれば使える）", updated);
        expect("目: 消えたら Missing に戻る", removed);
    }
    {
        // 確かめが答えなかったとき: 覚えず、少しあとの refresh で起動し直す（偽物の係と時計で）
        char base[] = "/tmp/fas-eye-retry-XXXXXX";
        const bool made = mkdtemp(base) != nullptr;
        const std::string root = made ? base : "/tmp";
        const std::string bin = root + "/.local/bin";
        const std::string script = bin + "/frameeyeosc-panel";
        ::mkdir((root + "/.local").c_str(), 0700);
        ::mkdir(bin.c_str(), 0700);
        std::string error;
        writeFilePrivately(script, "#!/bin/sh\nexit 0\n", error);  // 起動はしない（見つかって stat できればよい）
        ::chmod(script.c_str(), 0700);
        const char* oldHome = std::getenv("HOME");
        const std::string savedHome = oldHome != nullptr ? oldHome : "";
        const char* oldPath = std::getenv("PATH");
        const std::string savedPath = oldPath != nullptr ? oldPath : "";
        ::setenv("HOME", root.c_str(), 1);
        ::setenv("PATH", (root + "/nowhere").c_str(), 1);
        CommandResult::Status probeStatus = CommandResult::Status::Timeout;
        int probes = 0;
        long long nowMs = 1000;
        const EyeRunner fake = [&](const std::vector<std::string>& argv, int /*timeoutMs*/) {
            if (argv.size() == 2 && argv[1] == "--version") {
                CommandResult r = exitResult(0);
                r.out = "frameeyeosc-panel 0.7.10\n";
                return r;
            }
            ++probes;
            if (probeStatus == CommandResult::Status::Failed) return exitResult(1, "bogus: unknown action (use recenter)\n");
            CommandResult r;
            r.status = probeStatus;
            return r;
        };
        EyePanelProbe probe(fake, [&] { return nowMs; });
        // 時間切れ: Unknown のまま（使えるかもしれないので頼んではみる）。すぐの refresh では起動しない
        const bool timedOut = probe.refresh() && probe.info().state == EyePanelState::Unknown && probe.info().path == script &&
                              probes == 1 && !probe.refresh() && probes == 1;
        nowMs += kEyeProbeRetryMs - 1;
        const bool waits = !probe.refresh() && probes == 1;
        nowMs += 1;
        probeStatus = CommandResult::Status::SpawnFailed;
        // 間をおいたら起動し直す。起動できなければ入っていない側（Missing）
        const bool retried = probe.refresh() && probes == 2 && probe.info().state == EyePanelState::Missing &&
                             probe.info().path.empty();
        nowMs += kEyeProbeRetryMs;
        probeStatus = CommandResult::Status::Failed;
        // 答えが返ったら覚える（もう起動しない）
        const bool ready = probe.refresh() && probes == 3 && probe.info().state == EyePanelState::Ready &&
                           probe.info().version == "0.7.10" && !probe.refresh() && probes == 3;
        if (oldHome != nullptr) ::setenv("HOME", savedHome.c_str(), 1);
        if (oldPath != nullptr) ::setenv("PATH", savedPath.c_str(), 1);
        ::unlink(script.c_str());
        ::rmdir(bin.c_str());
        ::rmdir((root + "/.local").c_str());
        if (made) ::rmdir(root.c_str());
        expect("目: 確かめが時間切れなら覚えず Unknown、すぐには起動し直さない", made && timedOut);
        expect("目: 確かめ直すのは間をおいてから", waits);
        expect("目: 時間切れのあと、次の refresh でもう一度起動する（起動できなければ Missing）", retried);
        expect("目: 答えが返ったら覚えて、もう起動しない", ready);
    }
    {
        // 設定ファイル: 2 つの種類を読み書きできる（version は 3 のまま）
        Config config;
        std::vector<std::string> warnings;
        std::string error;
        const bool ok = parseConfigText(R"({ "version": 3, "actions": { "press_1": "eye_recenter",
                                             "long_press": { "type": "eye_fit" }, "tap_3": { "type": "eye_fit" },
                                             "tap_2": "eye_relearn" } })",
                                        config, warnings, error);
        expect("設定: eye_recenter / eye_fit / eye_relearn を読める・知らせなし",
               ok && warnings.empty() && config.press[0].type == ActionType::EyeRecenter &&
                   config.longPress.type == ActionType::EyeFit && config.tap[1].type == ActionType::EyeFit &&
                   config.tap[0].type == ActionType::EyeRelearn);
        const std::string json = configToJson(config);
        Config again;
        expect("設定: 目のアクションを書いて読み直すと同じ・version は 3",
               parseConfigText(json, again, warnings, error) && configToJson(again) == json &&
                   json.find("\"press_1\": { \"type\": \"eye_recenter\" }") != std::string::npos &&
                   json.find("\"long_press\": { \"type\": \"eye_fit\" }") != std::string::npos &&
                   json.find("\"tap_2\": { \"type\": \"eye_relearn\" }") != std::string::npos &&
                   json.find("\"version\": 3") != std::string::npos);
        expect("設定: 目のアクションに SteamVR は要らない（--test-action でつながない）",
               !actionSpec(ActionType::EyeRecenter).needsVr && !actionSpec(ActionType::EyeFit).needsVr &&
                   !actionSpec(ActionType::EyeRelearn).needsVr);
        ActionType type = ActionType::None;
        expect("設定: --test-action の名前", parseActionType("eye_recenter", type) && type == ActionType::EyeRecenter &&
                                                 parseActionType("eye_fit", type) && type == ActionType::EyeFit &&
                                                 std::string(actionTypeName(ActionType::EyeFit)) == "eye_fit" &&
                                                 parseActionType("eye_relearn", type) && type == ActionType::EyeRelearn);
        Config future;
        parseConfigText(R"({ "version": 3, "actions": { "press_1": "eye_something_new" } })", future, warnings, error);
        expect("設定: 知らない目の種類は知らせて、既定の割り当てのまま",
               !warnings.empty() && future.press[0].type == Config().press[0].type);
    }
    expect("文言: 目のアクションの名前・カードの 2 行目（日英）",
           std::string(actionLabel(Language::Ja, ActionType::EyeRecenter)) == "目の正面合わせ" &&
               std::string(actionLabel(Language::Ja, ActionType::EyeFit)) == "目のフィット" &&
               std::string(actionLabel(Language::En, ActionType::EyeRecenter)) == "Recenter eyes" &&
               std::string(actionLabel(Language::En, ActionType::EyeFit)) == "Eye fit" &&
               std::string(actionLabel(Language::Ja, ActionType::EyeRelearn)) == "普段の目を覚え直す" &&
               std::string(actionLabel(Language::En, ActionType::EyeRelearn)) == "Relearn my eyes" &&
               std::string(uiText(Language::Ja).stEyeRelearn) == "普段の目を約 30 秒で覚え直します" &&
               std::string(uiText(Language::Ja).pickerEye) == "目" && std::string(uiText(Language::En).pickerEye) == "Eyes");
    expect("文言: 目のアクションのアイコン",
           iconForAction(ActionType::EyeRecenter) == Icon::EyeRecenter && iconForAction(ActionType::EyeFit) == Icon::EyeFit &&
               iconForAction(ActionType::EyeRelearn) == Icon::EyeRelearn);
    {
        PressRecord press;
        press.kind = PressRecord::Kind::Tap;
        press.count = 2;
        press.action = ActionType::EyeRelearn;
        press.ok = true;
        press.value = kEyeRelearning;
        HudToast toast;
        expect("お知らせ: 覚え直しは始めたときにも出す（点を見る必要が無い・約 30 秒）",
               makeHudToast(press, Language::Ja, toast) && !toast.error && toast.title == "普段の目を覚え直しています" &&
                   toast.sub == "2 回タップ・約 30 秒" && toast.icon == Icon::EyeRelearn &&
                   makeHudToast(press, Language::En, toast) && toast.title == "Relearning your eyes" &&
                   toast.sub == "Double tap · About 30 s");
        press.ok = false;
        press.value.clear();
        press.reason = kReasonEyeNoCameras;
        expect("お知らせ: 覚え直しで目のカメラが使えない",
               makeHudToast(press, Language::Ja, toast) && toast.error && toast.title == "普段の目を覚え直せません" &&
                   toast.sub == "2 回タップ・目のカメラが使えません" && makeHudToast(press, Language::En, toast) &&
                   toast.title == "Couldn't relearn your eyes" && toast.sub == "Double tap · The eye cameras aren't available");
        press.reason = kReasonEyeCamerasCalibrating;
        const bool calibrating = makeHudToast(press, Language::Ja, toast) && toast.sub == "2 回タップ・目のカメラの校正中です";
        press.reason = kReasonEyeCamerasNoVideo;
        expect("お知らせ: 覚え直しで校正中・映像が来ていない",
               calibrating && makeHudToast(press, Language::Ja, toast) && toast.sub == "2 回タップ・目のカメラの映像が来ていません");
        press.reason = kReasonNotWorn;
        expect("お知らせ: 覚え直しでかぶっていない",
               makeHudToast(press, Language::Ja, toast) && toast.sub == "2 回タップ・ヘッドセットをかぶっている間だけ使えます");
    }
    {
        PressRecord press;
        press.kind = PressRecord::Kind::Long;
        press.action = ActionType::EyeFit;
        press.ok = true;
        press.value = kEyeFitting;
        HudToast toast;
        expect("お知らせ: フィットを始めたときは出さない（frameeyeosc の点を見てもらう）",
               !makeHudToast(press, Language::Ja, toast));
        press.action = ActionType::EyeRecenter;
        press.value = kEyeRecentering;
        expect("お知らせ: 正面合わせを始めたときも出さない", !makeHudToast(press, Language::En, toast));
        press.action = ActionType::EyeFit;
        press.value = kEyeStopped;
        expect("お知らせ: フィットを止めた",
               makeHudToast(press, Language::Ja, toast) && !toast.error && toast.title == "目のフィットを止めました" &&
                   toast.sub == "長押し" && toast.icon == Icon::EyeFit);
        press.ok = false;
        press.reason = kReasonEyeNoAnswer;
        expect("お知らせ: 止められなかった（英語）",
               makeHudToast(press, Language::En, toast) && toast.error && toast.title == "Couldn't stop the eye fit" &&
                   toast.sub == "Long press · frameeyeosc's panel didn't answer");
        press.value.clear();
        press.reason = kReasonEyePanelStopped;
        expect("お知らせ: フィットを始められない・パネルが動いていない",
               makeHudToast(press, Language::Ja, toast) && toast.error && toast.title == "目のフィットを始められません" &&
                   toast.sub == "長押し・frameeyeosc のパネルが動いていません");
        press.kind = PressRecord::Kind::Count;
        press.count = 1;
        press.action = ActionType::EyeRecenter;
        press.reason = kReasonEyeNotFitted;
        expect("お知らせ: 正面合わせの前にフィットが要る",
               makeHudToast(press, Language::Ja, toast) && toast.title == "目の正面を合わせられません" &&
                   toast.sub == "1 回押し・先に目のフィットをしてください");
        press.reason = kReasonEyeBusy;
        expect("お知らせ: フィット中", makeHudToast(press, Language::Ja, toast) && toast.sub == "1 回押し・フィット中です");
        press.reason = kReasonEyeLocked;
        expect("お知らせ: 正面合わせで、値がコマンドラインで固定（日英）",
               makeHudToast(press, Language::Ja, toast) && toast.sub == "1 回押し・frameeyeosc のコマンドラインで固定されています" &&
                   makeHudToast(press, Language::En, toast) && toast.sub == "1 press · Locked by frameeyeosc's command line");
        press.action = ActionType::EyeFit;
        expect("お知らせ: フィットで、値がコマンドラインで固定",
               makeHudToast(press, Language::Ja, toast) && toast.error && toast.title == "目のフィットを始められません" &&
                   toast.sub == "1 回押し・frameeyeosc のコマンドラインで固定されています");
        press.action = ActionType::EyeRecenter;
        press.reason = kReasonEyeRecenterBusy;
        expect("お知らせ: 正面合わせ中（正面合わせ・フィットとも）",
               makeHudToast(press, Language::Ja, toast) && toast.title == "目の正面を合わせられません" &&
                   toast.sub == "1 回押し・正面合わせ中です" && makeHudToast(press, Language::En, toast) &&
                   toast.sub == "1 press · Recentering is running");
        press.action = ActionType::EyeFit;
        expect("お知らせ: フィットで正面合わせ中",
               makeHudToast(press, Language::Ja, toast) && toast.title == "目のフィットを始められません" &&
                   toast.sub == "1 回押し・正面合わせ中です");
        press.reason = kReasonEyeNotRunning;
        expect("お知らせ: frameeyeosc が動いていない（4）",
               makeHudToast(press, Language::Ja, toast) && toast.sub == "1 回押し・frameeyeosc が動いていません" &&
                   makeHudToast(press, Language::En, toast) && toast.sub == "1 press · frameeyeosc isn't running");
        press.reason = kReasonNotWorn;
        expect("お知らせ: 目のデータが来ない（9）は、視点のリセットと同じ「かぶっている間だけ」",
               makeHudToast(press, Language::Ja, toast) && toast.title == "目のフィットを始められません" &&
                   toast.sub == "1 回押し・ヘッドセットをかぶっている間だけ使えます" &&
                   makeHudToast(press, Language::En, toast) && toast.sub == "1 press · Works only while you wear the headset");
        press.action = ActionType::EyeRecenter;
        bool texts = true;
        for (const char* reason : {kReasonEyeMissing, kReasonEyeOutdated, kReasonEyeNotRunning}) {
            for (const Language lang : {Language::Ja, Language::En}) {
                texts = texts && reasonText(lang, reason) != reasonText(lang, "failed") &&
                        reasonText(lang, reason).find("frameeyeosc") != std::string::npos;
            }
        }
        expect("お知らせ: 入っていない・更新が必要・動いていないの文言（日英）", texts);
    }
    {
        // 設定パネル: 割り当ての列の「目」の側
        using K = PanelAction::Kind;
        Config config;
        expect("パネル: 目のフィットを割り当てる",
               applyPanelAction({K::Assign, static_cast<int>(ActionType::EyeFit), 5}, config) &&
                   config.longPress.type == ActionType::EyeFit && config.longPress.commandId.empty());
        FontSet fonts;
        fonts.load("/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc");
        SettingsPanel panel(fonts);
        PanelView view;
        view.daemon = PanelView::Daemon::Running;
        for (const EyePanelState state : {EyePanelState::Ready, EyePanelState::Missing, EyePanelState::Outdated}) {
            view.eyePanel.state = state;
            view.eyePanel.version = state == EyePanelState::Outdated ? "0.7.9" : "";
            for (const Language lang : {Language::Ja, Language::En}) {
                const std::string tag = std::string("（") + languageCode(lang) + "・" +
                                        (state == EyePanelState::Ready ? "使える" : state == EyePanelState::Missing ? "入っていない" : "古い") +
                                        "）";
                config.language = lang;
                panel.setTab(PanelTab::Assign);
                panel.setSelectedSlot(5);
                panel.render(config, view);
                double x = 0;
                double y = 0;
                double bx = 0;
                double by = 0;
                double bw = 0;
                double bh = 0;
                bool row = true;
                double firstY = -1;
                double lastRight = 0;
                for (const ActionType type : {ActionType::EyeRecenter, ActionType::EyeFit, ActionType::EyeRelearn}) {
                    if (!panel.buttonBounds({K::Assign, static_cast<int>(type), 5}, bx, by, bw, bh)) {
                        row = false;
                        continue;
                    }
                    if (firstY < 0) firstY = by;
                    row = row && by == firstY && bx >= lastRight && bx + bw <= SettingsPanel::kWidth - 24 &&
                          by + bh <= SettingsPanel::kHeight - 40;
                    lastRight = bx + bw;
                }
                expect("パネル: 目のアクションを割り当てたカードを選ぶと、目の側の 3 つが 1 段に並ぶ" + tag,
                       row && !panel.buttonCenter({K::Assign, static_cast<int>(ActionType::MicMuteToggle), 5}, x, y) &&
                           !panel.buttonCenter({K::Assign, static_cast<int>(ActionType::MediaNext), 5}, x, y));
                double segment[4][4] = {};
                bool segments = true;
                const K kinds[] = {K::PickerActions, K::PickerMedia, K::PickerEye, K::PickerCommands};
                for (int i = 0; i < 4; ++i) {
                    segments = segments && panel.buttonBounds({kinds[i], 0, 0}, segment[i][0], segment[i][1], segment[i][2],
                                                              segment[i][3]);
                    if (i > 0) segments = segments && segment[i][0] >= segment[i - 1][0] + segment[i - 1][2];
                }
                expect("パネル: ［アクション｜音楽｜目｜コマンド］がこの順に並ぶ" + tag,
                       segments && segment[3][0] + segment[3][2] <= SettingsPanel::kWidth - 24);
                panel.buttonCenter({K::PickerActions, 0, 0}, x, y);
                panel.pointerDown(x, y, 0.0);
                panel.render(config, view);
                expect("パネル: アクションの側に目のアクションは入らない" + tag,
                       panel.buttonCenter({K::Assign, static_cast<int>(ActionType::MicMuteToggle), 5}, x, y) &&
                           !panel.buttonCenter({K::Assign, static_cast<int>(ActionType::EyeFit), 5}, x, y) &&
                           !panel.buttonCenter({K::Assign, static_cast<int>(ActionType::EyeRecenter), 5}, x, y) &&
                           !panel.buttonCenter({K::Assign, static_cast<int>(ActionType::EyeRelearn), 5}, x, y));
                panel.buttonCenter({K::PickerEye, 0, 0}, x, y);
                panel.pointerDown(x, y, 1.0);
                panel.render(config, view);
                const bool eye = panel.buttonCenter({K::Assign, static_cast<int>(ActionType::EyeRecenter), 5}, x, y);
                const PanelAction pressed = panel.pointerDown(x, y, 2.0);
                expect("パネル: 目の側に切り替えて、目の正面合わせを押すと割り当ての操作が返る（使えなくても選べる）" + tag,
                       eye && pressed == PanelAction({K::Assign, static_cast<int>(ActionType::EyeRecenter), 5}));
                panel.pointerUp();
            }
        }
    }
}

int main() {
    testPressDetector();
    testBrightness();
    testConfig();
    testStatus();
    testParsers();
    testTextAndHud();
    testCommands();
    testKeyboardText();
    testPanel();
    testPrivateFiles();
    testOsc();
    testPanelState();
    testHeadPointer();
    testTaps();
    testMedia();
    testRecenter();
    testEye();
    std::printf("\n%d 件中 %d 件が合格\n", gTotal, gTotal - gFailures);
    return gFailures == 0 ? 0 : 1;
}
