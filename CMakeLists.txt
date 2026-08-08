cmake_minimum_required(VERSION 3.13)
# libgme は C++ で書かれている (SPEC 4.1)。muchip 自体のソースは C だが、
# libgme を静的リンクする最終リンクは C++ ランタイム(libstdc++)を要求するため
# CXX も有効化し、muchip のリンカドライバを g++ に切り替える(下記 LINKER_LANGUAGE)。
#
# VERSION は muChip の唯一のバージョン情報源。scripts/package.sh が .muxapp の
# ファイル名 (muChip-<version>.muxapp) を決めるためにこの行を sed で直接読むので、
# 「project(muchip VERSION x.y.z ...)」を1行で書く形を崩さないこと
# (tests/test_package.sh が CMake の解釈と package.sh の解釈の一致を検証する)。
#
# 1.0.0 = MUST要件(F-01〜F-08)に加え、P8のSHOULD/NICE要件
# (F-10 チャンネルミュート / F-14 ビジュアライザ / F-20 EQ) まで実装し、
# 実機で動作を確認した状態。
# 1.0.1 = Player画面の文字サイズ修正(Issue #3)とCI/リリース自動化。
# ユーザー向けの新機能は無いためpatchバンプ。
# 1.1.0 = Player画面の文字バランス調整(Issue #8)に加え、バッテリー残量
# 表示(F-26, Issue #7)というユーザー向けの新機能が入るためminorバンプ。
# 1.2.0 = .nsf/.nsfe (NSF, Nintendo Sound Format) をBrowserで一級市民として
# 扱えるようにした(F-27, Issue #2)。libgmeのNSF/NSFEエミュレータ自体は
# 元々リンクされていた(USE_GME_NSF/NSFEがlibgme側の既定でONだったため)
# ので実質はBrowser側の拡張子フィルタ追加であり、バイナリサイズの増分は
# 無いが、ユーザー向けの新機能のためminorバンプ。
# 1.6.0 = アプリ名を muGBS から muChip へ改めた(Issue #13)。パッケージ名・
# 実行ファイル名・環境変数が変わる利用者向けの変更のためminorバンプ。
project(muchip VERSION 1.6.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# TARGET_HOST=ON (既定): 開発機 (x86_64 Linux 等) 向けにネイティブビルドする。
# 実機向けクロスビルド (P7) では toolchain-aarch64.cmake 経由でOFFにする。
# SDL2の解決方法は下記 SDL2 セクション参照。 (SPEC 8.1, 10.1)
option(TARGET_HOST "ホスト(開発機)向けにネイティブビルドする" ON)

add_compile_options(-Wall -Wextra)

# --- libgme (game-music-emu) --------------------------------------------
# GBS デコード・拡張M3U解析を丸ごと委譲する。自前で GB APU を実装しない。 (SPEC 4.1, 5.1)
# オプション名は GME_ プレフィクス付き（旧名 BUILD_SHARED_LIBS 等は既定値の参照元でしかない）。
set(GME_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(GME_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(GME_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GME_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GME_ENABLE_UBSAN OFF CACHE BOOL "" FORCE)
set(GME_ENABLE_ASAN OFF CACHE BOOL "" FORCE)
# zlib 依存を持ち込まない。VGZ 等の圧縮系フォーマットが読めなくなるだけで GBS には影響しない。
set(GME_ZLIB OFF CACHE BOOL "" FORCE)
set(USE_GME_GBS ON CACHE BOOL "" FORCE)
# NSF/NSFE (Nintendo Sound Format) デコード。GBSと同格の一級市民として扱う
# (F-27, Issue #2)。libgme側の既定でも元々ONだが、暗黙の既定値に頼ると
# upstreamが既定を変えたときに黙って壊れるため明示する。
set(USE_GME_NSF ON CACHE BOOL "" FORCE)
set(USE_GME_NSFE ON CACHE BOOL "" FORCE)

add_subdirectory(vendor/game-music-emu EXCLUDE_FROM_ALL)

# --- miniz (zip展開, P4で使用開始) ---------------------------------------
add_library(miniz STATIC
    vendor/miniz/miniz.c
    vendor/miniz/miniz_tdef.c
    vendor/miniz/miniz_tinfl.c
    vendor/miniz/miniz_zip.c
)
target_include_directories(miniz PUBLIC vendor/miniz)

# --- muchip 本体 -------------------------------------------------------------
add_executable(muchip
    src/main.c
    src/config.c
    src/battery.c
    src/eq.c
    src/audio.c
    src/shuffle.c
    src/player.c
    src/m3u.c
    src/playlist.c
    src/archive.c
    src/ui.c
    src/browser.c
    src/input.c
    src/app.c
)

target_include_directories(muchip PRIVATE src)

# 実機の log.txt からどのビルドか判別できるように、バージョンを埋め込む。
target_compile_definitions(muchip PRIVATE MUCHIP_VERSION="${PROJECT_VERSION}")

# install() は意図的に用意していない。scripts/package.sh が packaging/muChip/ の
# 資材とクロスビルド済みバイナリを直接ステージングして .muxapp を作るため、
# CMake の install ツリーを経由すると中間層が1つ増えるだけで得が無い。

# --- SDL2 -----------------------------------------------------------------
if(TARGET_HOST)
    find_package(SDL2 REQUIRED)
    if(TARGET SDL2::SDL2)
        target_link_libraries(muchip PRIVATE SDL2::SDL2)
    else()
        target_include_directories(muchip PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(muchip PRIVATE ${SDL2_LIBRARIES})
    endif()
else()
    # クロスビルド時: 実機のSDL2にはCMake config も pkg-config も無い
    # (実機から抜いたヘッダ+.soのみ)ため find_package は使わない。
    # docker/Dockerfile が sysroot/ の中身をクロスツールチェイン既定の
    # sysroot (/usr/aarch64-linux-gnu) へ上書き配置しているので、
    # そのSDL2サブディレクトリを直接指定するだけでよい。
    # (実機で実証済み。詳細は cmake/toolchain-aarch64.cmake のコメント参照)
    set(AARCH64_TOOLCHAIN_SYSROOT "/usr/aarch64-linux-gnu" CACHE PATH
        "crossbuild-essential-arm64 が既定で参照するsysroot(実機ファイルで上書き済み)")
    target_include_directories(muchip PRIVATE ${AARCH64_TOOLCHAIN_SYSROOT}/include/SDL2)
    target_link_libraries(muchip PRIVATE SDL2)
endif()

target_link_libraries(muchip PRIVATE gme_static miniz m)

# libgme(C++静的ライブラリ)をリンクするため、リンカドライバを g++ にする。
# C コンパイラ(gcc)のままだと -lstdc++ が自動で付かず operator new/delete 等が
# 未定義参照になる。
set_target_properties(muchip PROPERTIES LINKER_LANGUAGE CXX)

# 実機の glibc/libstdc++ とのバージョン差で "GLIBCXX_3.4.xx not found" が
# 起きるのを避けるため libstdc++/libgcc を静的リンクする。 (SPEC 4.1)
target_link_options(muchip PRIVATE -static-libstdc++ -static-libgcc)

if(TARGET_HOST)
    # tests/ はaarch64向けにもビルドは通るが、ctestでの実行はホスト上でしか
    # できない(クロスビルドしたaarch64バイナリはx86_64ホストで動かせない)。
    enable_testing()
    add_subdirectory(tests)
endif()
