# P3で追加したm3u.c/playlist.cの単体・統合テスト。
# 外部テストフレームワークは追加せず(SPEC 12「依存追加は事前に相談」)、
# 素のCプログラム+終了コードでCTestに判定させる。

add_executable(test_m3u test_m3u.c ../src/m3u.c)
target_include_directories(test_m3u PRIVATE . ../src)
add_test(NAME test_m3u COMMAND test_m3u)

add_executable(test_playlist test_playlist.c ../src/playlist.c ../src/m3u.c ../src/archive.c)
target_include_directories(test_playlist PRIVATE . ../src)
target_link_libraries(test_playlist PRIVATE gme_static miniz)
set_target_properties(test_playlist PROPERTIES LINKER_LANGUAGE CXX)
target_link_options(test_playlist PRIVATE -static-libstdc++ -static-libgcc)
add_test(NAME test_playlist COMMAND test_playlist)

add_executable(test_archive test_archive.c ../src/archive.c)
target_include_directories(test_archive PRIVATE . ../src)
target_link_libraries(test_archive PRIVATE miniz)
add_test(NAME test_archive COMMAND test_archive)
