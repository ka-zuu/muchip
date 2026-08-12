/* theme.h - カラーテーマ(色計算・プリセット)。 (SPEC 6.4, Issue #27)
 *
 * SDL には一切依存しない純 libc(config.h が include するため。config.c は
 * SDL をリンクしないテストからも使われる。詳細は config.h 冒頭コメント)。
 * SDL 型への変換(SDL_Color 化)は ui.c の責務。
 *
 * 色の置き場所を「編集可能な9スロット」と「そこから計算する派生色」に
 * 分けている(docs/design-notes.md「カラーテーマ」参照)。派生色を
 * スロットにしないのは、エディタ画面(P?)で編集する項目を絞り、
 * プリセット追加時に9色だけ決めればよいようにするため。
 */
#ifndef MUGBS_THEME_H
#define MUGBS_THEME_H

typedef struct {
    unsigned char r, g, b;
} theme_color_t;

/* 描画側が色を引くときの1つの名前空間。0..8はスロットそのもの
 * (theme_slot_t と同じ値を共有する。theme.c の _Static_assert参照)、
 * 9以降は theme_role_color() が計算する派生色。 */
typedef enum {
    THEME_ROLE_BG = 0,     /* 画面背景 */
    THEME_ROLE_PANEL,      /* ヘッダ/フッタ帯・ステータスオーバーレイ背景 */
    THEME_ROLE_FG,         /* 本文の文字 */
    THEME_ROLE_DIM,        /* 副文・バッテリー通常表示 */
    THEME_ROLE_ACCENT,     /* シークバー塗り・波形線 */
    THEME_ROLE_SEL,        /* リストのカーソル行背景 */
    THEME_ROLE_MARK,       /* 再生中トラックの強調文字 */
    THEME_ROLE_WARN,       /* エラー・バッテリー低下・確認ダイアログの枠 */
    THEME_ROLE_OK,         /* 充電中 */

    THEME_ROLE_RAISED,     /* 派生: Player中央一覧の背景(旧list_bg) */
    THEME_ROLE_GUTTER,     /* 派生: シークバーの未再生部分の背景 */
    THEME_ROLE_SUNKEN,     /* 派生: 波形の背景(旧wave_bg) */
    THEME_ROLE_BOX_BG,     /* 派生: 確認ダイアログの背景 */
    THEME_ROLE_COUNT,
} theme_role_t;

/* エディタ画面(Edit theme)が編集する9スロット。theme_role_t の先頭9件と
 * 値を共有するので、theme_role_color(t, (theme_role_t)slot) がそのまま使える。 */
typedef enum {
    THEME_BG     = THEME_ROLE_BG,
    THEME_PANEL  = THEME_ROLE_PANEL,
    THEME_FG     = THEME_ROLE_FG,
    THEME_DIM    = THEME_ROLE_DIM,
    THEME_ACCENT = THEME_ROLE_ACCENT,
    THEME_SEL    = THEME_ROLE_SEL,
    THEME_MARK   = THEME_ROLE_MARK,
    THEME_WARN   = THEME_ROLE_WARN,
    THEME_OK     = THEME_ROLE_OK,
    THEME_SLOT_COUNT,
} theme_slot_t;

typedef struct {
    theme_color_t slot[THEME_SLOT_COUNT];
} theme_t;

/* プリセット。CUSTOMはここには無く、config.ini の [theme] セクション
 * (mugbs_config_t.theme_custom)から来る。 */
typedef enum {
    THEME_MIDNIGHT = 0, /* 既定。現行(P6以前)の色そのもの */
    THEME_GAMEBOY,
    THEME_MONO,
    THEME_AMBER,
    THEME_SYNTHWAVE,
    THEME_CUSTOM,
    THEME_ID_COUNT,
} theme_id_t;

/* id のプリセット色を out へ書く。id が THEME_CUSTOM または範囲外なら
 * THEME_MIDNIGHT を書く(呼び出し側での事前チェックを不要にするための
 * フェイルセーフ。理由は theme.c 参照)。 */
void theme_preset(theme_id_t id, theme_t *out);

/* 実効テーマを確定する。id==THEME_CUSTOM なら *custom をそのまま、
 * それ以外なら theme_preset(id, out) と同じ。custom が NULL なら
 * THEME_CUSTOM でも THEME_MIDNIGHT にフォールバックする。 */
void theme_resolve(theme_id_t id, const theme_t *custom, theme_t *out);

/* role に対応する色を返す。role がスロット範囲(0..THEME_SLOT_COUNT-1)なら
 * t->slot[role] をそのまま、派生ロールなら都度計算する
 * (キャッシュは持たない。呼び出し頻度は1フレームに数十回程度で無視できる)。 */
theme_color_t theme_role_color(const theme_t *t, theme_role_t role);

/* ---- 色演算(すべて純関数。docs/design-notes.md「カラーテーマ」参照) ---- */

/* aとbをpct%(0..100にクランプ)だけ線形補間する(pct=0でa、100でb)。 */
theme_color_t theme_mix(theme_color_t a, theme_color_t b, int pct);

/* baseを、fgとは逆方向(fgがbaseより明るければ黒、暗ければ白)へpct%だけ
 * 遠ざける。窪んだパネル(波形背景等)用。baseがその方向に十分な
 * ヘッドルーム(各chの余地の最大値)を持たない場合
 * (例: base=黒でfgがさらに暗い方向を要求する、は起きないが
 * base=黒でfgが明るい場合に黒方向を要求するとヘッドルームが無い)は
 * theme_mix(base, fg, pct/3) にフォールバックする。 */
theme_color_t theme_recede(theme_color_t base, theme_color_t fg, int pct);

/* bg の上に置く文字色として a と b のどちらがコントラストが高いかを
 * 輝度差(theme_luma)で判定して返す。ui_draw_list() の選択行や
 * テーマエディタ画面のように、ユーザー編集値でコントラストが崩れうる
 * 箇所だけに限定して使う(docs/design-notes.md参照。全画面には適用しない)。 */
theme_color_t theme_best_on(theme_color_t bg, theme_color_t a, theme_color_t b);

/* ITU-R BT.601 相当の輝度(0..255)。 */
int theme_luma(theme_color_t c);

/* ---- プリセット名 <-> theme_id_t ---------------------------------------- */

/* "midnight"|"gameboy"|"mono"|"amber"|"synthwave"|"custom"(大小文字不問)を
 * 解決する。0で成功、-1で該当なし(*outは変更しない)。 */
int theme_id_from_name(const char *name, theme_id_t *out);

/* config.ini へ書くトークン。範囲外は "midnight" にフォールバックする。 */
const char *theme_id_name(theme_id_t id);

/* Settings画面の Theme 行に出す表示名(6値)。 THEME_NAMES 相当。 */
const char *theme_id_label(theme_id_t id);

/* ---- スロット名 ---------------------------------------------------------- */

/* config.ini [theme] セクションのキー名("bg"/"panel"/...)。 */
const char *theme_slot_key(theme_slot_t slot);

/* テーマエディタ画面に出す表示名("Background"/"Panel"/...)。 */
const char *theme_slot_label(theme_slot_t slot);

/* ---- 16進カラー文字列 ---------------------------------------------------- */

/* "RRGGBB" または "#RRGGBB"(大小文字不問、それ以外の桁数・非16進文字は失敗)
 * をパースする。0で成功、-1で失敗(*outは変更しない)。 */
int theme_color_parse(const char *s, theme_color_t *out);

/* "rrggbb"(小文字、#無し、7バイト=6文字+NUL)を書く。 */
void theme_color_format(theme_color_t c, char out[7]);

/* ---- テーマエディタの値変更 ---------------------------------------------- */

/* チャンネル値(0..255)を8刻みの梯子(0,8,16,...,248,255の33段)上で
 * dir(+1/-1)方向へ1段動かす。vが梯子上に無い場合はまず動く方向側の
 * 隣接値へ吸着してから動く(config.iniの手編集値からの初回操作対応。
 * SET_MINUTES等の目盛り吸着と同じ考え方)。0と255は固定点
 * (これ以上その方向へは動かない)。255-0間が8で割り切れないための
 * 特別扱い。 */
int theme_channel_step(int v, int dir);

#endif /* MUGBS_THEME_H */
