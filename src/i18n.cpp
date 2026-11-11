// 画面に出す文言の表の中身（設定パネル・視界の表示）。
#include "i18n.h"

#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace {

/**
 * Steam の言語設定を読む。~/.steam/registry.vdf の最初の "language" の値（"japanese" など）。
 * @return 値。読めなければ空
 */
std::string steamLanguage() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') return "";
    std::ifstream file(std::string(home) + "/.steam/registry.vdf");
    std::string line;
    while (std::getline(file, line)) {
        // 形式: <タブ>"language"<タブ>"japanese"
        const std::string key = "\"language\"";
        const size_t at = line.find(key);
        if (at == std::string::npos) continue;
        const size_t open = line.find('"', at + key.size());
        const size_t close = open == std::string::npos ? open : line.find('"', open + 1);
        if (close == std::string::npos) return "";
        return line.substr(open + 1, close - open - 1);
    }
    return "";
}

/**
 * ロケールの環境変数（LC_ALL → LC_MESSAGES → LANG の順で最初に空でないもの）が日本語か。
 * @return 日本語なら true
 */
bool localeIsJapanese() {
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(name);
        if (value != nullptr && value[0] != '\0') return std::string(value).rfind("ja", 0) == 0;
    }
    return false;
}

/**
 * システム言語を調べる（systemLanguage の本体）。
 * @return 言語
 */
Language detectSystemLanguage() {
    const std::string steam = steamLanguage();
    if (!steam.empty()) return steam == "japanese" ? Language::Ja : Language::En;
    return localeIsJapanese() ? Language::Ja : Language::En;
}

/**
 * 日本語の表を作る。
 * @return 表
 */
UiText makeJapanese() {
    UiText t {};
    t.title = "aux ショートカット";
    t.statusRunning = "動作中";
    t.statusStandby = "待機中";
    t.statusHandedBack = "従来の aux";
    t.statusStopped = "止まっています";
    t.tabAssign = "割り当て";
    t.tabBrightness = "明るさ";
    t.tabPress = "押し方";
    t.tabCommands = "コマンド";
    t.tabDisplay = "表示と音";
    t.tabApp = "アプリ";
    t.footerSaved = "押すとすぐ反映され、設定ファイルに保存されます。";
    t.footerBgOn = "パネルを閉じても、aux ボタンのショートカットは裏で動き続けます";
    t.footerBgOff = "パネルを閉じると、aux ボタンは元の動き（1 回押しでダッシュボード）に戻ります";
    t.on = "オン";
    t.off = "オフ";
    t.actMic = "マイクのミュート";
    t.actBright = "明るさ";
    t.actPass = "パススルー";
    t.actShot = "スクリーンショット";
    t.actPerf = "パフォーマンス表示";
    t.actDash = "ダッシュボード";
    t.actRecenter = "視点のリセット";
    t.actOriginal = "aux の元の動き";
    t.actMediaPlayPause = "再生・一時停止";
    t.actMediaNext = "次の曲";
    t.actMediaPrevious = "前の曲";
    t.actEyeRecenter = "目の正面合わせ";
    t.actEyeFit = "目のフィット";
    t.actEyeRelearn = "普段の目を覚え直す";
    t.actCmd = "コマンド";
    t.actNone = "なし";
    t.recentFormat = "さっき押した %s";
    t.recentTapFormat = "さっきタップ %s";
    t.stMicMuted = "いま ミュート中";
    t.stMicOn = "いま オン";
    t.stMicUnknown = "ミュートを切り替えます";
    t.stBrightFormat = "いま %s（%s）";
    t.stPassOn = "いま オン";
    t.stPassOff = "いま オフ";
    t.stPassUnknown = "オン・オフを切り替えます";
    t.stPerfShown = "いま 出ている";
    t.stPerfHidden = "いま 消えている";
    t.stPerfStopped = "frame-perf-overlay が止まっています";
    t.stPerfMissing = "frame-perf-overlay がありません";
    t.stPerfUnknown = "表示を切り替えます";
    t.stDash = "ダッシュボードを開きます";
    t.stOriginal = "元の 1 回押し（開く・クリック）";
    t.stRecenter = "今向いている方を正面にします";
    t.stMedia = "Chrome などで流している音楽";
    t.stEyeRecenter = "目の正面だけ合わせ直します";
    t.stEyeFit = "視線とまぶたを合わせます";
    t.stEyeRelearn = "普段の目を約 30 秒で覚え直します";
    t.stEyeMissing = "frameeyeosc が入っていません";
    t.stEyeOutdated = "frameeyeosc の更新が必要です";
    t.stCmdMissing = "このコマンドはもうありません";
    t.missingCommand = "消えたコマンド";
    t.stNone = "何もしない";
    t.assignHeadingFormat = "%s に割り当てる";
    t.assignHint = "上のカードを選んでから、ここで決めます";
    t.tapNoteTitle = "ヘッドセットのタップ";
    t.tapNote = "かぶっている間だけ使えます。ヘッドセットの横を指で軽くトントンとたたきます";
    t.chipMissing = "入っていない";
    t.chipOutdated = "更新が必要";
    t.pickerActions = "アクション";
    t.pickerCommandsFormat = "コマンド（%s）";
    t.pickerMedia = "音楽";
    t.mediaNote = "Frame で流している音楽・動画を操作します（Chrome・Chromium など、MPRIS に対応したアプリ）。Steam Link でつないだ PC の音は操作できません";
    t.pickerEye = "目";
    t.eyeNote = "frameeyeosc（目のトラッキング）に頼みます。目のフィットの途中にもう一度押すと止まります。正面合わせは先に目のフィットをしてから、覚え直しは目のカメラを使っているときに使えます";
    t.eyeMissingNote = "frameeyeosc が入っていません。アプリのタブから入れられます";
    t.eyeOutdatedNote = "frameeyeosc を新しい版に更新してください";
    t.eyeVersionNowFormat = "（今は %s）";
    t.cmdAdd = "＋ コマンドを足す";
    t.cmdHeading = "コマンド";
    t.cmdHint = "自分で用意したコマンドを登録しておくと、「割り当て」で押し方に選べます";
    t.cmdUnused = "まだ割り当てていません";
    t.cmdTested = "実行しました";
    t.cmdTestFailed = "実行できませんでした";
    t.cmdTest = "試す";
    t.cmdEdit = "編集";
    t.cmdDelete = "消す";
    t.cmdDeleteConfirm = "もう一度押すと消す";
    t.cmdFolder = "フォルダ";
    t.cmdEmpty = "まだコマンドがありません。「＋ コマンドを足す」から登録します";
    t.cmdNote = "~/.config/frame-aux-shortcuts/commands/ に実行ファイルを置いても、ここに並びます（名前はファイル名）。コマンドはシェルを通さずに実行し、終わるのは待ちません";
    t.sheetAddTitle = "コマンドを足す";
    t.sheetEditTitle = "コマンドを編集";
    t.fieldName = "名前";
    t.fieldCommand = "実行するコマンド";
    t.namePlaceholder = "例: スピーカーのミュート";
    t.commandPlaceholder = "例: wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle";
    t.sheetHint = "欄を押すと SteamVR のキーボードが出ます。空白で区切ると引数になり、\"…\" でまとめられます";
    t.sheetCancel = "やめる";
    t.sheetSave = "保存する";
    t.sheetQuoteError = "\" が閉じていません";
    t.keyboardName = "コマンドの名前";
    t.keyboardCommand = "実行するコマンド";
    t.keyboardIp = "PC の IP アドレス";
    t.sheetModePresets = "プリセットから";
    t.sheetModeOwn = "自分で書く";
    t.presetVrcName = "VRChat のミュート（PC）";
    t.presetVrcDesc = "つないでいる PC の VRChat に OSC を送って、ミュートを切り替えます";
    t.presetSpkName = "スピーカーのミュート";
    t.presetSpkDesc = "Frame の音を消す・戻す";
    t.presetNote = "プリセットも、追加したあとは自分のコマンドと同じように並び替え・編集・削除できます";
    t.oscDestTitle = "送り先の PC";
    t.oscDestHint = "いま Steam Link でつないでいる PC を自動で見つけます";
    t.oscDestAuto = "自動";
    t.oscDestManual = "手で入れる";
    t.oscFoundFormat = "見つかりました: %s";
    t.oscFoundHint = "Steam Link の相手。押すたびに探し直すので、PC を変えてもそのまま使えます";
    t.oscNotFound = "つないでいる PC が見つかりません";
    t.oscNotFoundHint = "Steam Link で PC につないでいるときに使えます。このまま追加しても大丈夫です";
    t.oscIpLabel = "PC の IP アドレス";
    t.oscIpPlaceholder = "例: 192.168.0.12";
    t.oscIpInvalid = "IP アドレスの形ではありません";
    t.oscSetupTitle = "PC の VRChat で、先に 1 回だけ";
    t.oscSetup1 = "アクションメニューの「オプション」→「OSC」をオンにする";
    t.oscSetup2 = "マイクの設定を「トグル」にする（押して話す方式だと切り替わりません）";
    t.oscFirewall = "届かないときは、PC のファイアウォールで VRChat の受信を「パブリック」でも許可します。VRChat のミュートは、Frame のマイクのミュートとは別です";
    t.spkDetailTitle = "Frame の音を消す・戻す";
    t.spkDetail = "ゲームや動画の音（スピーカー）をミュートします。マイクのミュートとは別です。準備は要りません";
    t.presetAdd = "追加する";
    t.oscTested = "PC に送りました。VRChat のマイクの表示を見てね";
    t.oscTestNoPc = "送れません: つないでいる PC が見つかりません";
    t.oscTestFailed = "送れませんでした";
    t.stOscVoice = "PC の VRChat に送る";
    t.stOscFormat = "PC に OSC を送る（%s）";
    t.brightHeading = "明るさの段";
    t.brightWhereFormat = "%sで、左から順に切り替わります";
    t.brightNowhere = "どの押し方にも割り当てていません";
    t.listSep = "・";
    t.brightStepFormat = "%s 段目";
    t.brightNow = "いま";
    t.brightRemove = "この段を消す";
    t.brightAdd = "段を足す";
    t.brightLoop = "1 段目に戻る";
    t.brightNote = "2〜4 段まで。10〜125%（100% が SteamVR の標準。SteamVR の設定画面は 40% まで）。押したときは、いまの明るさに一番近い段の次へ進みます";
    t.pressHeading = "押し方の決まり方";
    t.longTitle = "長押し";
    t.longHint = "この時間より長く押し続けると、離す前に長押しに決まります";
    t.gapTitle = "回数の区切り";
    t.gapHint = "離してからこの時間内に次を押すと、続けて数えます";
    t.secondsFormat = "%s 秒";
    t.pressHold = "押し続ける";
    t.pressReleaseHere = "ここで離せば 1 回押し";
    t.pressLongChipFormat = "%s 秒で長押しに決まる";
    t.pressFirst = "1 回目";
    t.pressGapBox = "この間に押すと 2 回目";
    t.pressGapChip = "押さなければ、ここで「1 回押し」に決まる";
    t.pressNote = "6 回以上押したときは、数え間違いを防ぐため何もしません";
    t.soundTitle = "押したときの音";
    t.soundHint = "うまくいったときと失敗したときで違う音";
    t.hudTitle = "押したときのお知らせ";
    t.hudHint = "何をしたかを視界に数秒だけ出します";
    t.badgeTitle = "ミュート中の表示";
    t.badgeHint = "ミュートしている間、視界の上に出し続けます";
    t.previewCaption = "視界のイメージ";
    t.badgeText = "マイク ミュート中";
    t.bgTitle = "パネルを閉じても aux を使う";
    t.bgHintOn = "パネルを閉じても、裏でショートカットが動き続けます";
    t.bgHintOff = "パネルを閉じると、aux は元の動きに戻ります";
    t.ptrTitle = "コントローラー未接続時は従来の aux";
    t.ptrHintOn = "aux を従来の動きに戻します（開く・クリック）";
    t.ptrHintOff = "aux はショートカットのまま";
    t.checkTitle = "新しい版の確認";
    t.checkHint = "起動時と 1 時間に 1 回、GitHub を見に行きます";
    t.autoTitle = "SteamVR と一緒に起動";
    t.autoHint = "次の SteamVR の起動から";
    t.autoNotInstalled = "サービスが入っていません（install.sh で入ります）";
    t.autoBusy = "切り替え中…";
    t.autoFailed = "切り替えに失敗しました（ログを見てね）";
    t.langTitle = "言語";
    t.appsTitle = "いっしょに使うアプリ";
    t.appsRelatedHint = "このアプリの機能で使うものだけを出しています";
    t.appsReasonPerf = "パフォーマンス表示（5 回押し）に使う";
    t.appsReasonKeyboard = "コマンドの名前を日本語で入れるときに使う";
    t.appsAllTitle = "ささけんの Frame アプリ";
    t.appsAllCountFormat = "%1$d 個のうち %2$d 個が入っていません";
    t.appsAllInstalledFormat = "%d 個すべて入っています";
    t.appsInstall = "入れる";
    t.chipRunning = "動作中";
    t.appsChipInstalled = "入っています";
    t.appsChipBusy = "Konsole で実行中…";
    t.appsRelatedTag = "このアプリで使う";
    t.appsListSub = "入れる・更新する・消すは、Konsole のインストーラーで行います";
    t.appsListNote = "一覧は「新しい版の確認」がオンのとき、frame.sasaken1102s.net から取ってきます（1 時間に 1 回まで。取れなければ前回の分か同梱の一覧）";
    t.appsOpenInstaller = "インストーラーを開く";
    t.appsConfirmTitleFormat = "%s を入れる";
    t.appsConfirmMenuTitle = "インストーラーを開く";
    t.appsConfirmLead = "Konsole が開いて、このコマンドを実行します";
    t.appsConfirmProgress = "進み具合は Konsole に出ます（聞かれることがあれば、そこで答えます）";
    t.appsConfirmMenu = "メニューから、入れる・更新する・消すアプリを番号で選びます";
    t.appsConfirmNoSudo = "sudo は使いません。入るのはホームフォルダの中だけです";
    t.appsConfirmClose = "終わったら Konsole を閉じてね。このパネルの表示も変わります";
    t.appsConfirmCancel = "やめる";
    t.appsConfirmLaunch = "Konsole で開く";
    t.appsErrorBusy = "同じアプリの Konsole がまだ開いています";
    t.appsErrorNoDisplay = "Konsole を開けませんでした: 画面が見つかりません（DISPLAY がありません）";
    t.appsErrorNoKonsole = "Konsole を開けませんでした: Konsole が入っていません";
    t.appsErrorFailed = "Konsole を開けませんでした";
    t.resetAll = "すべて既定に戻す";
    t.resetConfirm = "もう一度押すと既定に戻す";
    t.quitBoth = "パネルと裏の動きを終了";
    t.quitApp = "アプリを終了";
    t.quitConfirm = "もう一度押すと終了";
    t.updateVersionFormat = "いまの版 %s";
    t.updateUpToDateFormat = "最新版です（%s）";
    t.updateChecking = "新しい版を確かめています…";
    t.updateAvailableFormat = "新しい版 %s があります";
    t.updateButton = "更新する";
    t.updateManual = "ここからは入れられない版です。GitHub から手で更新してね";
    t.updateConfirmFormat = "%s に更新しますか？";
    t.updateConfirmHint = "ダウンロードして入れ替えます。途中でこの画面が閉じて開き直すことがあります";
    t.updateConfirmYes = "更新する";
    t.updateConfirmNo = "やめる";
    t.updateInstallingFormat = "更新中: %s";
    t.updateInstalledFormat = "%s を入れました。開き直すと新しい版になります";
    t.updateInstallFailed = "更新できませんでした（今の版のままです）:";
    t.updateCheckFailed = "新しい版を確かめられませんでした:";
    t.updateCheckNow = "今すぐ確かめる";
    t.updateRetry = "もう一度";
    t.updateDismiss = "閉じる";
    t.updateLogHint = "くわしくは ~/.cache/frame-aux-shortcuts/update.log";
    t.hudMicMuted = "マイクをミュートしました";
    t.hudMicOn = "マイクをオンにしました";
    t.hudMicToggled = "マイクのミュートを切り替えました";
    t.hudBrightFormat = "明るさ %s%%";
    t.hudPassOn = "パススルーをオンにしました";
    t.hudPassOff = "パススルーをオフにしました";
    t.hudShot = "スクリーンショットを保存しました";
    t.hudPerfShown = "パフォーマンス表示を出しました";
    t.hudPerfHidden = "パフォーマンス表示を消しました";
    t.hudPerfToggled = "パフォーマンス表示を切り替えました";
    t.hudDash = "ダッシュボードを開きました";
    t.hudCmd = "コマンドを実行しました";
    t.hudVrcVoice = "VRChat にミュートの切り替えを送りました";
    t.hudOscFormat = "OSC を送りました: %s";
    t.hudMediaPlaying = "再生";
    t.hudMediaPaused = "一時停止";
    t.hudMediaNext = "次の曲";
    t.hudMediaPrevious = "前の曲";
    t.hudRecenter = "視点をリセットしました";
    t.hudEyeStopped = "目のフィットを止めました";
    t.hudEyeRelearning = "普段の目を覚え直しています";
    t.hudEyeRelearnSub = "約 30 秒";
    t.failMic = "マイクを切り替えられません";
    t.failBright = "明るさを変えられません";
    t.failPass = "パススルーを切り替えられません";
    t.failShot = "スクリーンショットを撮れませんでした";
    t.failPerf = "パフォーマンス表示を切り替えられません";
    t.failDash = "ダッシュボードを開けません";
    t.failOriginal = "aux の元の動きを送れません";
    t.failCmd = "コマンドを実行できません";
    t.failVrcVoice = "VRChat に送れません";
    t.failOsc = "OSC を送れません";
    t.failMediaPlayPause = "再生・一時停止できません";
    t.failMediaNext = "次の曲に進めません";
    t.failMediaPrevious = "前の曲に戻れません";
    t.failRecenter = "視点をリセットできません";
    t.failEyeRecenter = "目の正面を合わせられません";
    t.failEyeFit = "目のフィットを始められません";
    t.failEyeStop = "目のフィットを止められません";
    t.failEyeRelearn = "普段の目を覚え直せません";
    t.reasonNoSteamVr = "SteamVR につながっていません";
    t.reasonRoomViewApi = "SteamVR の更新で使えなくなったかも";
    t.reasonPerfMissing = "frame-perf-overlay が入っていません";
    t.reasonPerfStopped = "frame-perf-overlay が動いていません";
    t.reasonNoCommand = "コマンドが見つかりません";
    t.reasonNoPc = "つないでいる PC が見つかりません";
    t.reasonNoPlayer = "再生しているアプリが見つかりません";
    t.reasonMediaUnsupported = "このアプリでは使えません";
    t.reasonNotWorn = "ヘッドセットをかぶっている間だけ使えます";
    t.reasonNotTracking = "ヘッドセットの位置が取れていません";
    t.reasonEyeMissing = "frameeyeosc が入っていません";
    t.reasonEyeOutdated = "frameeyeosc の更新が必要です";
    t.reasonEyePanelStopped = "frameeyeosc のパネルが動いていません";
    t.reasonEyeNoAnswer = "frameeyeosc のパネルが応答しません";
    t.reasonEyeNotFitted = "先に目のフィットをしてください";
    t.reasonEyeBusy = "フィット中です";
    t.reasonEyeRecenterBusy = "正面合わせ中です";
    t.reasonEyeNotRunning = "frameeyeosc が動いていません";
    t.reasonEyeNoCameras = "目のカメラが使えません";
    t.reasonEyeCamerasCalibrating = "目のカメラの校正中です";
    t.reasonEyeCamerasNoVideo = "目のカメラの映像が来ていません";
    t.reasonEyeLocked = "frameeyeosc のコマンドラインで固定されています";
    t.reasonFailed = "うまくいきませんでした";
    t.subSep = "・";
    t.hudHandBack = "aux を従来の動きに戻しました";
    t.hudHandBackSub = "コントローラーが接続されていない間";
    t.hudShortcutsBack = "aux のショートカットに戻しました";
    return t;
}

/**
 * 英語の表を作る。
 * @return 表
 */
UiText makeEnglish() {
    UiText t {};
    t.title = "aux Shortcuts";
    t.statusRunning = "Running";
    t.statusStandby = "Standby";
    t.statusHandedBack = "Normal aux";
    t.statusStopped = "Stopped";
    t.tabAssign = "Assign";
    t.tabBrightness = "Brightness";
    t.tabPress = "Presses";
    t.tabCommands = "Commands";
    t.tabDisplay = "Display";
    t.tabApp = "App";
    t.footerSaved = "Changes apply instantly and are saved. ";
    t.footerBgOn = "The aux shortcuts keep working after you close this panel";
    t.footerBgOff = "When you close this panel, aux goes back to normal (one press opens the dashboard)";
    t.on = "On";
    t.off = "Off";
    t.actMic = "Mic mute";
    t.actBright = "Brightness";
    t.actPass = "Passthrough";
    t.actShot = "Screenshot";
    t.actPerf = "Perf overlay";
    t.actDash = "Dashboard";
    t.actRecenter = "Recenter view";
    t.actOriginal = "Original aux";
    t.actMediaPlayPause = "Play / pause";
    t.actMediaNext = "Next track";
    t.actMediaPrevious = "Previous track";
    t.actEyeRecenter = "Recenter eyes";
    t.actEyeFit = "Eye fit";
    t.actEyeRelearn = "Relearn my eyes";
    t.actCmd = "Command";
    t.actNone = "None";
    t.recentFormat = "Pressed %s";
    t.recentTapFormat = "Tapped %s";
    t.stMicMuted = "Muted now";
    t.stMicOn = "On now";
    t.stMicUnknown = "Toggles mute";
    t.stBrightFormat = "Now %s (%s)";
    t.stPassOn = "On now";
    t.stPassOff = "Off now";
    t.stPassUnknown = "Turns it on or off";
    t.stPerfShown = "Shown now";
    t.stPerfHidden = "Hidden now";
    t.stPerfStopped = "frame-perf-overlay is not running";
    t.stPerfMissing = "frame-perf-overlay missing";
    t.stPerfUnknown = "Shows or hides it";
    t.stDash = "Opens the SteamVR dashboard";
    t.stOriginal = "One normal press: open or click";
    t.stRecenter = "Sets forward to where you face";
    t.stMedia = "Music playing in Chrome, etc.";
    t.stEyeRecenter = "Re-centers your gaze only";
    t.stEyeFit = "Fits your gaze and eyelids";
    t.stEyeRelearn = "Relearns your relaxed eyes (~30 s)";
    t.stEyeMissing = "frameeyeosc isn't installed";
    t.stEyeOutdated = "frameeyeosc needs an update";
    t.stCmdMissing = "This command no longer exists";
    t.missingCommand = "Missing command";
    t.stNone = "Does nothing";
    t.assignHeadingFormat = "Assign to %s";
    t.assignHint = "Pick a card above, then choose here";
    t.tapNoteTitle = "Headset taps";
    t.tapNote = "Works only while you wear the headset. Knock lightly on its side with a finger";
    t.chipMissing = "Not installed";
    t.chipOutdated = "Update needed";
    t.pickerActions = "Actions";
    t.pickerCommandsFormat = "Commands (%s)";
    t.pickerMedia = "Music";
    t.mediaNote = "Controls music or video playing on the Frame (Chrome, Chromium and other apps that support MPRIS). Audio from a PC over Steam Link can't be controlled.";
    t.pickerEye = "Eyes";
    t.eyeNote = "Asks frameeyeosc (eye tracking) to do it. Press an eye fit again while it runs to stop it. Recentering needs an eye fit done first; relearning needs the eye cameras in use.";
    t.eyeMissingNote = "frameeyeosc isn't installed. You can install it from the App tab.";
    t.eyeOutdatedNote = "Update frameeyeosc to a newer version";
    t.eyeVersionNowFormat = " (now %s)";
    t.cmdAdd = "+ Add command";
    t.cmdHeading = "Commands";
    t.cmdHint = "Register your own commands, then pick them for a press in Assign";
    t.cmdUnused = "Not assigned yet";
    t.cmdTested = "Ran it";
    t.cmdTestFailed = "Couldn't run it";
    t.cmdTest = "Try";
    t.cmdEdit = "Edit";
    t.cmdDelete = "Delete";
    t.cmdDeleteConfirm = "Press again to delete";
    t.cmdFolder = "Folder";
    t.cmdEmpty = "No commands yet. Add one with \"+ Add command\"";
    t.cmdNote = "Executable files in ~/.config/frame-aux-shortcuts/commands/ are listed here too (named after the file). Commands run without a shell, and aux doesn't wait for them to finish";
    t.sheetAddTitle = "Add a command";
    t.sheetEditTitle = "Edit command";
    t.fieldName = "Name";
    t.fieldCommand = "Command to run";
    t.namePlaceholder = "e.g. Mute speakers";
    t.commandPlaceholder = "e.g. wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle";
    t.sheetHint = "Tap a field for the SteamVR keyboard. Spaces separate arguments; \"…\" keeps text together";
    t.sheetCancel = "Cancel";
    t.sheetSave = "Save";
    t.sheetQuoteError = "A \" isn't closed";
    t.keyboardName = "Command name";
    t.keyboardCommand = "Command to run";
    t.keyboardIp = "PC's IP address";
    t.sheetModePresets = "Presets";
    t.sheetModeOwn = "Write your own";
    t.presetVrcName = "VRChat mute (PC)";
    t.presetVrcDesc = "Sends OSC to VRChat on the connected PC to toggle mute";
    t.presetSpkName = "Mute speakers";
    t.presetSpkDesc = "Mutes or unmutes the Frame's sound";
    t.presetNote = "Once added, a preset is a normal command: reorder, edit or delete it like your own";
    t.oscDestTitle = "Send to";
    t.oscDestHint = "Finds the PC you're streaming from with Steam Link";
    t.oscDestAuto = "Auto";
    t.oscDestManual = "Enter IP";
    t.oscFoundFormat = "Found: %s";
    t.oscFoundHint = "Your Steam Link PC. It looks again on every press, so it follows you to another PC";
    t.oscNotFound = "No connected PC found";
    t.oscNotFoundHint = "It works while you stream from a PC with Steam Link. You can add it now anyway";
    t.oscIpLabel = "PC's IP address";
    t.oscIpPlaceholder = "e.g. 192.168.0.12";
    t.oscIpInvalid = "Not an IP address";
    t.oscSetupTitle = "First, once in VRChat on the PC";
    t.oscSetup1 = "Turn on OSC: Action Menu → Options → OSC";
    t.oscSetup2 = "Set the mic to toggle mode (push-to-talk won't toggle)";
    t.oscFirewall = "If nothing arrives, allow VRChat on public networks too in Windows Firewall on the PC. VRChat's mute is on the PC, separate from the Frame's mic mute";
    t.spkDetailTitle = "Mute or unmute the Frame's sound";
    t.spkDetail = "Mutes the sound of games and videos (the speakers). Separate from mic mute. No setup needed";
    t.presetAdd = "Add";
    t.oscTested = "Sent to the PC. Check the mic icon in VRChat";
    t.oscTestNoPc = "Not sent: no connected PC found";
    t.oscTestFailed = "Couldn't send it";
    t.stOscVoice = "Sends to VRChat on the PC";
    t.stOscFormat = "Sends OSC to the PC (%s)";
    t.brightHeading = "Brightness steps";
    t.brightWhereFormat = "%s steps through these from left to right";
    t.brightNowhere = "Not assigned to any press";
    t.listSep = ", ";
    t.brightStepFormat = "Step %s";
    t.brightNow = "Now";
    t.brightRemove = "Remove";
    t.brightAdd = "Add step";
    t.brightLoop = "Back to step 1";
    t.brightNote = "2 to 4 steps, 10–125% (100% is SteamVR's default; its slider stops at 40%). A press moves to the step after the one closest to the current brightness";
    t.pressHeading = "How presses are decided";
    t.longTitle = "Long press";
    t.longHint = "Hold longer than this and it becomes a long press before you let go";
    t.gapTitle = "Count gap";
    t.gapHint = "Press again within this time after letting go and it keeps counting";
    t.secondsFormat = "%s s";
    t.pressHold = "Holding";
    t.pressReleaseHere = "Let go here for 1 press";
    t.pressLongChipFormat = "Long press at %s s";
    t.pressFirst = "1st press";
    t.pressGapBox = "Press here for a 2nd";
    t.pressGapChip = "No press: it becomes 1 press here";
    t.pressNote = "Six or more presses do nothing, to avoid miscounts";
    t.soundTitle = "Sound on press";
    t.soundHint = "Different sounds for success and failure";
    t.hudTitle = "Notice on press";
    t.hudHint = "Shows what happened in your view for a few seconds";
    t.badgeTitle = "Muted indicator";
    t.badgeHint = "Stays at the top of your view while muted";
    t.previewCaption = "In your view";
    t.badgeText = "Mic muted";
    t.bgTitle = "Keep aux on when closed";
    t.bgHintOn = "Shortcuts keep working in the background";
    t.bgHintOff = "Closing the panel gives aux back to SteamVR";
    t.ptrTitle = "Normal aux when no controller is connected";
    t.ptrHintOn = "Without controllers, aux opens the dashboard and clicks";
    t.ptrHintOff = "Shortcuts stay on even without controllers";
    t.checkTitle = "Check for updates";
    t.checkHint = "Looks on GitHub at start and every hour";
    t.autoTitle = "Start with SteamVR";
    t.autoHint = "From the next SteamVR start";
    t.autoNotInstalled = "The service is not installed (install.sh does it)";
    t.autoBusy = "Switching…";
    t.autoFailed = "Couldn't switch it (see the log)";
    t.langTitle = "Language";
    t.appsTitle = "Companion apps";
    t.appsRelatedHint = "Only the apps this app uses";
    t.appsReasonPerf = "Used for the perf overlay (5 presses)";
    t.appsReasonKeyboard = "For typing command names in Japanese";
    t.appsAllTitle = "Frame apps by sasaken@";
    t.appsAllCountFormat = "%2$d of %1$d not installed";
    t.appsAllInstalledFormat = "All %d are installed";
    t.appsInstall = "Install";
    t.chipRunning = "Running";
    t.appsChipInstalled = "Installed";
    t.appsChipBusy = "Running in Konsole…";
    t.appsRelatedTag = "Used by this app";
    t.appsListSub = "Install, update and remove in the installer in Konsole";
    t.appsListNote = "With update checks on, this list comes from frame.sasaken1102s.net (at most hourly; otherwise the last one or the built-in one)";
    t.appsOpenInstaller = "Open the installer";
    t.appsConfirmTitleFormat = "Install %s";
    t.appsConfirmMenuTitle = "Open the installer";
    t.appsConfirmLead = "Konsole opens and runs this command";
    t.appsConfirmProgress = "Konsole shows the progress (answer any questions there)";
    t.appsConfirmMenu = "In the menu, pick the apps to install, update or remove by number";
    t.appsConfirmNoSudo = "No sudo; everything goes into your home folder";
    t.appsConfirmClose = "Close Konsole when it's done; this panel updates too";
    t.appsConfirmCancel = "Cancel";
    t.appsConfirmLaunch = "Open in Konsole";
    t.appsErrorBusy = "A Konsole for this app is still open";
    t.appsErrorNoDisplay = "Couldn't open Konsole: no screen (DISPLAY isn't set)";
    t.appsErrorNoKonsole = "Couldn't open Konsole: Konsole isn't installed";
    t.appsErrorFailed = "Couldn't open Konsole";
    t.resetAll = "Reset everything";
    t.resetConfirm = "Press again to reset";
    t.quitBoth = "Quit panel and background";
    t.quitApp = "Quit app";
    t.quitConfirm = "Press again to quit";
    t.updateVersionFormat = "Version %s";
    t.updateUpToDateFormat = "Up to date (%s)";
    t.updateChecking = "Checking for updates…";
    t.updateAvailableFormat = "Version %s is available";
    t.updateButton = "Update";
    t.updateManual = "This version can't be installed from here. Update by hand from GitHub";
    t.updateConfirmFormat = "Update to %s?";
    t.updateConfirmHint = "It downloads and installs the new version. This panel may close and reopen meanwhile";
    t.updateConfirmYes = "Update";
    t.updateConfirmNo = "Cancel";
    t.updateInstallingFormat = "Updating: %s";
    t.updateInstalledFormat = "%s is installed. Reopen to use it";
    t.updateInstallFailed = "The update failed (nothing was changed):";
    t.updateCheckFailed = "Couldn't check for updates:";
    t.updateCheckNow = "Check now";
    t.updateRetry = "Try again";
    t.updateDismiss = "Close";
    t.updateLogHint = "Details: ~/.cache/frame-aux-shortcuts/update.log";
    t.hudMicMuted = "Mic muted";
    t.hudMicOn = "Mic on";
    t.hudMicToggled = "Mic mute toggled";
    t.hudBrightFormat = "Brightness %s%%";
    t.hudPassOn = "Passthrough on";
    t.hudPassOff = "Passthrough off";
    t.hudShot = "Screenshot saved";
    t.hudPerfShown = "Perf overlay shown";
    t.hudPerfHidden = "Perf overlay hidden";
    t.hudPerfToggled = "Perf overlay toggled";
    t.hudDash = "Dashboard opened";
    t.hudCmd = "Command started";
    t.hudVrcVoice = "Sent mute toggle to VRChat";
    t.hudOscFormat = "OSC sent: %s";
    t.hudMediaPlaying = "Playing";
    t.hudMediaPaused = "Paused";
    t.hudMediaNext = "Next track";
    t.hudMediaPrevious = "Previous track";
    t.hudRecenter = "View recentered";
    t.hudEyeStopped = "Eye fit stopped";
    t.hudEyeRelearning = "Relearning your eyes";
    t.hudEyeRelearnSub = "About 30 s";
    t.failMic = "Couldn't toggle the mic";
    t.failBright = "Couldn't change brightness";
    t.failPass = "Couldn't toggle passthrough";
    t.failShot = "Couldn't take a screenshot";
    t.failPerf = "Couldn't toggle the perf overlay";
    t.failDash = "Couldn't open the dashboard";
    t.failOriginal = "Couldn't send the aux press";
    t.failCmd = "Couldn't run the command";
    t.failVrcVoice = "Couldn't send to VRChat";
    t.failOsc = "Couldn't send OSC";
    t.failMediaPlayPause = "Couldn't play or pause";
    t.failMediaNext = "Couldn't skip to the next track";
    t.failMediaPrevious = "Couldn't go to the previous track";
    t.failRecenter = "Couldn't recenter the view";
    t.failEyeRecenter = "Couldn't recenter your eyes";
    t.failEyeFit = "Couldn't start the eye fit";
    t.failEyeStop = "Couldn't stop the eye fit";
    t.failEyeRelearn = "Couldn't relearn your eyes";
    t.reasonNoSteamVr = "Not connected to SteamVR";
    t.reasonRoomViewApi = "A SteamVR update may have broken this";
    t.reasonPerfMissing = "frame-perf-overlay is not installed";
    t.reasonPerfStopped = "frame-perf-overlay is not running";
    t.reasonNoCommand = "Command not found";
    t.reasonNoPc = "No connected PC found";
    t.reasonNoPlayer = "No app is playing media";
    t.reasonMediaUnsupported = "Not supported by this app";
    t.reasonNotWorn = "Works only while you wear the headset";
    t.reasonNotTracking = "The headset isn't being tracked";
    t.reasonEyeMissing = "frameeyeosc isn't installed";
    t.reasonEyeOutdated = "frameeyeosc needs an update";
    t.reasonEyePanelStopped = "frameeyeosc's panel isn't running";
    t.reasonEyeNoAnswer = "frameeyeosc's panel didn't answer";
    t.reasonEyeNotFitted = "Do an eye fit first";
    t.reasonEyeBusy = "An eye fit is running";
    t.reasonEyeRecenterBusy = "Recentering is running";
    t.reasonEyeNotRunning = "frameeyeosc isn't running";
    t.reasonEyeNoCameras = "The eye cameras aren't available";
    t.reasonEyeCamerasCalibrating = "The eye cameras are calibrating";
    t.reasonEyeCamerasNoVideo = "No video from the eye cameras";
    t.reasonEyeLocked = "Locked by frameeyeosc's command line";
    t.reasonFailed = "Something went wrong";
    t.subSep = " · ";
    t.hudHandBack = "aux is back to normal";
    t.hudHandBackSub = "While no controller is connected";
    t.hudShortcutsBack = "aux shortcuts are back";
    return t;
}

const UiText kJapanese = makeJapanese();
const UiText kEnglish = makeEnglish();

}  // namespace

Language systemLanguage() {
    static const Language cached = detectSystemLanguage();
    return cached;
}

const char* languageCode(Language language) {
    return language == Language::En ? "en" : "ja";
}

bool parseLanguage(const std::string& code, Language& language) {
    if (code == "ja") {
        language = Language::Ja;
        return true;
    }
    if (code == "en") {
        language = Language::En;
        return true;
    }
    return false;
}

const UiText& uiText(Language language) {
    return language == Language::En ? kEnglish : kJapanese;
}

std::string slotLabel(Language language, int slot) {
    const bool en = language == Language::En;
    if (slot == kTap2Slot) return en ? "Double tap" : "2 回タップ";
    if (slot == kTap3Slot) return en ? "Triple tap" : "3 回タップ";
    if (slot >= kLongPressSlot) return en ? "Long press" : "長押し";
    const int count = slot + 1;
    if (en) return count == 1 ? "1 press" : std::to_string(count) + " presses";
    return std::to_string(count) + " 回押し";
}

std::string slotTitle(Language language, int slot) {
    const bool en = language == Language::En;
    if (slot == kTap2Slot) return en ? "Tap headset twice" : "ヘッドセットを 2 回タップ";
    if (slot == kTap3Slot) return en ? "Tap headset 3 times" : "ヘッドセットを 3 回タップ";
    return slotLabel(language, slot);
}

const char* actionLabel(Language language, ActionType type) {
    const UiText& t = uiText(language);
    switch (type) {
        case ActionType::MicMuteToggle: return t.actMic;
        case ActionType::BrightnessCycle: return t.actBright;
        case ActionType::RoomViewToggle: return t.actPass;
        case ActionType::Screenshot: return t.actShot;
        case ActionType::PerfOverlayToggle: return t.actPerf;
        case ActionType::DashboardOpen: return t.actDash;
        case ActionType::Recenter: return t.actRecenter;
        case ActionType::OriginalAux: return t.actOriginal;
        case ActionType::MediaPlayPause: return t.actMediaPlayPause;
        case ActionType::MediaNext: return t.actMediaNext;
        case ActionType::MediaPrevious: return t.actMediaPrevious;
        case ActionType::EyeRecenter: return t.actEyeRecenter;
        case ActionType::EyeFit: return t.actEyeFit;
        case ActionType::EyeRelearn: return t.actEyeRelearn;
        case ActionType::Command: return t.actCmd;
        case ActionType::None: return t.actNone;
    }
    return t.actNone;
}

std::string formatText(const char* format, const std::string& value) {
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), format, value.c_str());
    return buffer;
}

std::string updateStepText(Language language, const std::string& step) {
    const bool en = language == Language::En;
    if (step == "start") return en ? "Preparing" : "準備中";
    if (step == "download") return en ? "Downloading" : "ダウンロード中";
    if (step == "verify") return en ? "Verifying" : "ファイルを確認中";
    if (step == "extract") return en ? "Unpacking" : "展開中";
    if (step == "install") return en ? "Installing" : "入れ替え中";
    return step;
}

std::string updateErrorText(Language language, const std::string& error) {
    const bool en = language == Language::En;
    if (error == "network") return en ? "Can't reach GitHub" : "GitHub につながりません";
    if (error == "rate-limited") {
        return en ? "GitHub's rate limit was hit. Try again in an hour" : "GitHub の回数制限にかかりました。1 時間ほどあとで試してね";
    }
    if (error == "not-found") return en ? "No published release" : "公開されている版がありません";
    if (error == "bad-response") return en ? "Couldn't read GitHub's answer" : "GitHub の返事を読めませんでした";
    if (error == "bad-version") return en ? "Couldn't read the version number" : "版の番号を読めませんでした";
    if (error == "bad-url") return en ? "Stopped: the download led outside GitHub" : "GitHub 以外の場所へ向かったので止めました";
    if (error == "missing-tool") return en ? "A required command (python3) is missing" : "必要なコマンド（python3）がありません";
    if (error == "no-checksums") {
        return en ? "This release has no SHA256SUMS. Update by hand" : "この版には確認用の SHA256SUMS がありません。手で更新してね";
    }
    if (error == "no-asset") return en ? "This release has no file to install" : "この版には入れるファイルがありません";
    if (error == "checksum-mismatch") return en ? "The download is corrupt (checksum mismatch)" : "ダウンロードしたファイルが壊れています";
    if (error == "unsafe-archive") return en ? "Stopped: the archive has unsafe contents" : "ファイルの中身が安全でないので止めました";
    if (error == "no-installer") return en ? "The archive has no install.sh" : "ファイルに install.sh がありません";
    if (error == "install-failed") return en ? "install.sh failed" : "install.sh が失敗しました";
    if (error == "bad-args") return en ? "The saved install options are invalid" : "前回のインストールのオプションを読めません";
    if (error == "busy") return en ? "Another update is running" : "別の更新が動いています";
    if (error == "not-newer") return en ? "Already up to date" : "もう最新版です";
    if (error == "detach-failed") return en ? "Couldn't start the update (systemd-run)" : "更新を始められませんでした（systemd-run）";
    if (error == "interrupted") return en ? "The update was interrupted" : "更新が途中で止まりました";
    if (error == "io") return en ? "Couldn't write files" : "ファイルを書けませんでした";
    if (error == "usage" || error == "script-failed" || error == "spawn-failed") {
        return en ? "The updater didn't run" : "更新の仕組みが動きませんでした";
    }
    return en ? "Something went wrong" : "うまくいきませんでした";
}
