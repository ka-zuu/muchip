# P3で追加したm3u.c/playlist.cの単体・統合テスト。
# 外部テストフレームワークは追加せず(SPEC 12「依存追加は事前に相談」)、
# 素のCプログラム+終了コードでCTestに判定させる。

add_executable(test_m3u test_m3u.c ../src/m3u.c)
target_include_directories(test_m3u PRIVATE . ../src)
add_test(NAME test_m3u COMMAND test_m3u)

# P6で追加したconfig.c(INIパーサ)の単体テスト。SDLに依存しない純libc。
add_executable(test_config test_config.c ../src/config.c)
target_include_directories(test_config PRIVATE . ../src)
add_test(NAME test_config COMMAND test_config)

add_executable(test_playlist test_playlist.c ../src/playlist.c ../src/m3u.c ../src/archive.c
                              ../src/config.c)
target_include_directories(test_playlist PRIVATE . ../src)
target_link_libraries(test_playlist PRIVATE gme_static miniz)
set_target_properties(test_playlist PROPERTIES LINKER_LANGUAGE CXX)
target_link_options(test_playlist PRIVATE -static-libstdc++ -static-libgcc)
add_test(NAME test_playlist COMMAND test_playlist)

add_executable(test_archive test_archive.c ../src/archive.c)
target_include_directories(test_archive PRIVATE . ../src)
target_link_libraries(test_archive PRIVATE miniz)
add_test(NAME test_archive COMMAND test_archive)

# P5で追加したbrowser.c(SDL非依存のディレクトリ走査ロジック)の単体テスト。
add_executable(test_browser test_browser.c ../src/browser.c)
target_include_directories(test_browser PRIVATE . ../src)
add_test(NAME test_browser COMMAND test_browser)

# P6で追加した ui_metrics_compute()/ui_glyph_size_for() (SDL_Init不要な純関数)の
# 単体テスト。ui.c 全体をリンクする必要があるためSDL2は要るが、SDL_Initは
# 一度も呼ばない。SDL2の解決方法はルートCMakeLists.txtのmugbs本体と同じ
# (find_package はそちらで既に呼ばれておりTARGET SDL2::SDL2/SDL2_LIBRARIES は
# このサブディレクトリからも見える)。
add_executable(test_ui_metrics test_ui_metrics.c ../src/ui.c)
target_include_directories(test_ui_metrics PRIVATE . ../src)
if(TARGET SDL2::SDL2)
    target_link_libraries(test_ui_metrics PRIVATE SDL2::SDL2)
else()
    target_include_directories(test_ui_metrics PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(test_ui_metrics PRIVATE ${SDL2_LIBRARIES})
endif()
add_test(NAME test_ui_metrics COMMAND test_ui_metrics)

# --- ヘッドレスUIスモークテスト (P5) ----------------------------------------
# app.c の画面遷移(Browser/Player/TrackList)がクラッシュしないことを、
# 実際にmugbs本体をSDL_VIDEODRIVER=dummy/SDL_AUDIODRIVER=dummyの下で
# 走らせて確認する。表示や実際の操作感はホストでの手動確認(README参照)で
# 見る前提であり、これはあくまで回帰検出用。
add_executable(gen_fixture_gbs gen_fixture_gbs.c)

set(UI_SMOKE_DIR ${CMAKE_CURRENT_BINARY_DIR}/ui_smoke_fixtures)
set(UI_SMOKE_GBS ${UI_SMOKE_DIR}/Game.gbs)
add_custom_command(
    OUTPUT ${UI_SMOKE_GBS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${UI_SMOKE_DIR}
    COMMAND gen_fixture_gbs ${UI_SMOKE_GBS} 2
    DEPENDS gen_fixture_gbs
    COMMENT "Generating synthetic GBS fixture for UI smoke test"
)
add_custom_target(ui_smoke_fixture ALL DEPENDS ${UI_SMOKE_GBS})

# P6: SPEC 6.2「解像度非依存」の完了条件(複数解像度でレイアウトが破綻しない)を
# CTestでも見るため、実機やその他muOS対応機種を代表する複数の解像度で
# 同じスクリプトを回す。640x480=実機(RG35XX PRO)、720x720=RG CubeXX相当、
# 1024x768=TrimUI Brick相当。--config は必ず個別に指定する
# (省略するとカレントディレクトリに config.ini を書き出してしまう)。
set(UI_SMOKE_RESOLUTIONS 320x240 480x320 640x480 720x720 1024x768 1280x720)
foreach(res ${UI_SMOKE_RESOLUTIONS})
    add_test(NAME test_ui_smoke_${res}
        COMMAND mugbs --window ${res} --start-dir ${UI_SMOKE_DIR}
                      --config ${CMAKE_CURRENT_BINARY_DIR}/ui_smoke_${res}.ini
                      --ui-script ${CMAKE_CURRENT_SOURCE_DIR}/ui_smoke.script)
    set_tests_properties(test_ui_smoke_${res} PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endforeach()
