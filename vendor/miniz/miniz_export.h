/* miniz_export.h
 *
 * 手動で用意したエクスポートヘッダ。
 *
 * 本来は miniz 本体の CMakeLists.txt (GenerateExportHeader) がビルド時に
 * 生成するファイルだが、本プロジェクトは miniz を split-file ソースのまま
 * vendoring し、静的リンクのみで使う（共有ライブラリとしてはビルドしない）。
 * そのため dllexport/visibility 制御は不要で、空定義で足りる。
 */
#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H

#define MINIZ_EXPORT

#endif /* MINIZ_EXPORT_H */
