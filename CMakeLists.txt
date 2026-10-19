cmake_minimum_required(VERSION 3.16)
project(frame-aux-shortcuts VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# SteamVR に同梱の OpenVR ライブラリ（Frame の SteamOS 上のパス）
set(OPENVR_LIB_DIR "/opt/steamvr/bin/linuxarm64" CACHE PATH "libopenvr_api.so のあるディレクトリ")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
# 音楽・動画の操作（MPRIS）にセッションバスへ話す sd-bus（SteamOS の systemd に入っている libsystemd）
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

# OpenVR も描画も使わない部分（判定・タップの判定・カーソルの判定・設定・状態ファイル・文言・コマンドの実行・OSC の送信・
# 音楽の操作のアプリの選び方・視点のリセットの判断・目のアクション（frameeyeosc-panel に頼む）など）
add_library(frame-aux-core STATIC
  src/press_detector.cpp
  src/brightness.cpp
  src/config.cpp
  src/json.cpp
  src/room_view.cpp
  src/i18n.cpp
  src/status.cpp
  src/runtime.cpp
  src/hud_message.cpp
  src/command.cpp
  src/screenshot.cpp
  src/commands.cpp
  src/keyboard_text.cpp
  src/osc.cpp
  src/panel_state.cpp
  src/head_pointer.cpp
  src/tap_detector.cpp
  src/media.cpp
  src/recenter.cpp
  src/eye_actions.cpp
)
target_include_directories(frame-aux-core PUBLIC src)
target_compile_options(frame-aux-core PRIVATE -Wall -Wextra)
# 視界のお知らせの中身がアイコンの種類（icons.h → draw.h → cairo.h）を使うので、ヘッダーのために cairo をつなぐ
target_link_libraries(frame-aux-core PUBLIC PkgConfig::CAIRO PkgConfig::FREETYPE)
# 設定パネルの frameeyeosc-panel の確かめ（EyePanelWatcher）がスレッドを使う
target_link_libraries(frame-aux-core PUBLIC Threads::Threads)

# 描画（cairo）: 設定パネル・視界の表示の絵、自動起動、新しい版の確認（vendor/frame-updater）、ほかのアプリ（vendor/frame-apps）
add_library(frame-aux-ui STATIC
  src/draw.cpp
  src/icons.cpp
  src/hud_render.cpp
  src/settings_panel.cpp
  src/autostart.cpp
  vendor/frame-updater/cpp/update_check.cpp
  vendor/frame-apps/cpp/frame_apps.cpp
  vendor/frame-apps/cpp/frame_apps_data.cpp
  vendor/frame-apps/cpp/frame_apps_catalog.cpp
  vendor/frame-apps/cpp/frame_apps_icons.cpp
  vendor/frame-apps/cpp/frame_apps_cairo.cpp
)
target_include_directories(frame-aux-ui PUBLIC src vendor/frame-updater/cpp vendor/frame-apps/cpp)
target_compile_options(frame-aux-ui PRIVATE -Wall -Wextra)
target_link_libraries(frame-aux-ui PUBLIC frame-aux-core PkgConfig::CAIRO PkgConfig::FREETYPE)

# OpenVR のテクスチャ（Vulkan）
add_library(frame-aux-vr STATIC
  src/vk_texture.cpp
)
target_include_directories(frame-aux-vr PUBLIC src third_party/openvr)
target_compile_options(frame-aux-vr PRIVATE -Wall -Wextra)
target_link_libraries(frame-aux-vr PUBLIC PkgConfig::VULKAN "${OPENVR_LIB_DIR}/libopenvr_api.so")

# 本体: aux の読み取り・判定・アクション・音・視界の表示・状態ファイル
add_executable(frame-aux-shortcuts
  src/main.cpp
  src/keymap.cpp
  src/actions.cpp
  src/action_worker.cpp
  src/mic_monitor.cpp
  src/sound.cpp
  src/vr_client.cpp
  src/hud_overlay.cpp
  src/media_control.cpp
)
target_link_libraries(frame-aux-shortcuts PRIVATE PkgConfig::SYSTEMD)
# 設定パネル: ダッシュボードのタブ
add_executable(frame-aux-shortcuts-panel
  src/panel_main.cpp
  src/dashboard_overlay.cpp
)
foreach(target frame-aux-shortcuts frame-aux-shortcuts-panel)
  target_compile_options(${target} PRIVATE -Wall -Wextra)
  # --version で出すバージョン（上の project(VERSION) から）
  target_compile_definitions(${target} PRIVATE FRAME_AUX_SHORTCUTS_VERSION="${PROJECT_VERSION}")
  target_link_libraries(${target} PRIVATE frame-aux-ui frame-aux-vr Threads::Threads)
  # 実行時も SteamVR 同梱のライブラリを使う
  # （ビルドした時点から INSTALL_RPATH だけにする。ビルド用の RPATH だと末尾に空の要素が付き、
  #   空の要素はカレントディレクトリからもライブラリを探す意味になるため）
  set_target_properties(${target} PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "${OPENVR_LIB_DIR}"
  )
endforeach()

option(FRAME_AUX_OPTIMIZE_RELEASE "Enable Release LTO and unused-code removal" ON)
if(FRAME_AUX_OPTIMIZE_RELEASE)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT frame_aux_ipo_supported LANGUAGES CXX)
  foreach(target frame-aux-core frame-aux-ui frame-aux-vr frame-aux-shortcuts frame-aux-shortcuts-panel)
    if(frame_aux_ipo_supported)
      set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    endif()
  endforeach()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target frame-aux-shortcuts frame-aux-shortcuts-panel)
      target_link_options(${target} PRIVATE $<$<CONFIG:Release>:-Wl,--gc-sections>)
    endforeach()
  endif()
endif()

install(TARGETS frame-aux-shortcuts frame-aux-shortcuts-panel RUNTIME DESTINATION bin)

# 単体テスト（押し方の判定・明るさ・設定の読み書きと version 1 からの移行・状態ファイル・視界の表示の中身・
# 設定パネルの当たり判定）。実機（gpio-keys・SteamVR・PipeWire）無しで動く。ctest --test-dir build で動かす。
include(CTest)
if(BUILD_TESTING)
  add_executable(frame-aux-unit-test tests/unit_test.cpp)
  target_compile_options(frame-aux-unit-test PRIVATE -Wall -Wextra)
  target_link_libraries(frame-aux-unit-test PRIVATE frame-aux-ui)
  if(FRAME_AUX_OPTIMIZE_RELEASE AND frame_aux_ipo_supported)
    set_property(TARGET frame-aux-unit-test PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  endif()
  add_test(NAME unit COMMAND frame-aux-unit-test)
endif()
