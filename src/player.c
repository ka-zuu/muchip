#include "player.h"

#include <stdlib.h>
#include <string.h>

#include "archive.h"
#include "eq.h"
#include "log.h"
#include "shuffle.h"

/* 曲長判定(SPEC 5.1 との乖離#1への対応)は playlist.c の
 * playlist_effective_length_ms() に一本化されている。playlist_open() の
 * スキャン時と、ここでの再生開始時の双方が同じ関数を呼ぶことで、
 * playlist_entry_t.duration_ms(UI表示用)と実際のフェード開始時刻が
 * 常に一致することを保証する。 */

/* config->shuffle が有効なとき、p->shuffle が現在の playlist/current_entry と
 * 整合していることを保証する(整合していれば何もしない)。無効なら使われて
 * いた shuffle.order を解放する(古い並びを再生に使ってしまわないため、
 * かつ ASan がリークとして拾わないため)。
 * player_play_entry() の最後(=あらゆる曲変更の後)と、
 * player_next_track()/player_prev_track() の先頭(=shuffleを設定画面で
 * オンにした直後などplay_entryをまだ経由していない場合の保険)の両方から
 * 呼ぶ。冪等なので呼び出し順序に依存しない。 */
static void sync_shuffle(player_t *p) {
    if (!p->config->shuffle || !p->playlist || p->current_entry < 0) {
        if (p->shuffle.order) shuffle_free(&p->shuffle);
        return;
    }
    if (!p->shuffle.order || p->shuffle.count != p->playlist->entry_count) {
        shuffle_reset(&p->shuffle, p->playlist->entry_count);
    }
    shuffle_sync(&p->shuffle, p->current_entry);
}

static void close_current_emu(player_t *p) {
    if (!p->emu) return;

    /* audio コールバックからの参照を先に外してから delete する。
     * gme_delete をコールバック実行中に呼んではならない。 (SPEC 5.1 落とし穴2) */
    audio_set_emu(&p->audio, NULL);
    gme_delete(p->emu);
    p->emu = NULL;
    audio_clear_scope(&p->audio); /* 停止後に直前の波形が残らないように (F-14) */
    p->current_source = -1;
    p->state = PLAYER_STOPPED;
}

/* config の eq_bass/eq_treble を emu へ適用する (F-20)。
 * gme_set_equalizer() は現在値を読んで treble/bass だけ差し替えるため、
 * gme_equalizer_t の予約フィールド(d2..d9)を自前でゼロ埋めしてはいけない
 * -- gme_equalizer() で現在値を取ってから書き換える。
 * ロックは呼び出し側の責務(開いた直後は不要、再生中は audio_lock 内)。 */
static void apply_equalizer(Music_Emu *emu, const mugbs_config_t *cfg) {
    gme_equalizer_t eq;
    gme_equalizer(emu, &eq);
    eq.treble = eq_treble_db(cfg->eq_treble);
    eq.bass = eq_bass_freq(cfg->eq_bass);
    gme_set_equalizer(emu, &eq);
}

/* 新しく開いた emu へ config の内容を焼き込む。gme_open_*() の直後に
 * 一度だけ呼ぶ。まだ audio_set_emu() でコールバックへ渡していない段階
 * なので、ここでは audio_lock は要らない。
 * (再生中の値変更は player_apply_config() 側が担当する) */
static void configure_new_emu(player_t *p, Music_Emu *emu) {
    gme_enable_accuracy(emu, 1);
    gme_set_stereo_depth(emu, p->config->stereo_depth);

    /* F-20: 必ず gme_open_*() の後に呼ぶ。Classic_Emu::setup_buffer() が
     * ロード時に set_equalizer(equalizer()) を呼ぶため、ロード前に設定しても
     * buf が未確定で bass_freq が反映されない。 */
    apply_equalizer(emu, p->config);
}

/* 既に開いている p->emu 上で track_index のトラックを開始する。
 * ファイルを開き直さない（同一ファイル内のトラック送り専用）。
 * 曲送りはすべて audio_lock/unlock で保護する。 (SPEC 5.4) */
static int start_track_at(player_t *p, int track_index) {
    audio_lock(&p->audio);
    gme_err_t err = gme_start_track(p->emu, track_index);
    audio_unlock(&p->audio);
    if (err) {
        LOG_ERR("gme_start_track(%d): %s", track_index, err);
        return -1;
    }

    gme_info_t *info = NULL;
    int fade_at, fade_len;
    err = gme_track_info(p->emu, &info, track_index);
    if (!err) {
        fade_at = playlist_effective_length_ms(info, p->config, NULL);
        fade_len = info->fade_length > 0 ? info->fade_length : p->config->fade_length_ms;
        gme_free_info(info); /* gme_track_info はヒープを返すので必ず解放する */
    } else {
        LOG_WARN("gme_track_info: %s。既定長 %d 秒でフェード開始します",
                  err, p->config->default_length_sec);
        fade_at = p->config->default_length_sec * 1000;
        fade_len = p->config->fade_length_ms;
    }

    audio_lock(&p->audio);
    gme_set_fade_msecs(p->emu, fade_at, fade_len);
    audio_unlock(&p->audio);

    /* gme_track_ended() が真になる(=曲が本当に終わる)のは fade_at + fade_len
     * 時点。UI表示・シーク上限は「フェード開始時刻」だけの duration_ms では
     * なく、この実際の終了時刻を見る必要がある(player_current_duration_ms
     * 参照)。そうしないと経過時間がフェード中に表示上の「合計時間」を
     * 追い越して見える(実際にバグとして発見された)。 */
    p->fade_len_ms = fade_len;

    p->state = PLAYER_PLAYING;
    /* track_ended フラグをクリアしつつ再設定する（emuポインタ自体は不変）。 */
    audio_set_emu(&p->audio, p->emu);
    audio_set_pause(&p->audio, 0);
    return 0;
}

int player_init(player_t *p, const mugbs_config_t *config) {
    memset(p, 0, sizeof(*p));
    p->config = config;
    p->state = PLAYER_STOPPED;
    p->current_source = -1;
    p->current_entry = -1;

    if (audio_init(&p->audio, config->sample_rate) != 0) {
        return -1;
    }
    return 0;
}

void player_shutdown(player_t *p) {
    close_current_emu(p);
    audio_shutdown(&p->audio);
    shuffle_free(&p->shuffle);
    p->playlist = NULL;
    p->current_entry = -1;
}

int player_load_playlist(player_t *p, const playlist_t *pl) {
    close_current_emu(p);
    /* 古いプレイリストの並び(entry_countが違う)を引きずらないよう、
     * ここで確実に解放する。有効ならplayer_play_entry()が新しい
     * entry_countで作り直す。 */
    shuffle_free(&p->shuffle);
    p->playlist = pl;
    p->current_entry = -1;
    return 0;
}

int player_play_entry(player_t *p, int entry_index) {
    if (!p->playlist || entry_index < 0 || entry_index >= p->playlist->entry_count) {
        LOG_ERR("player_play_entry: 不正なエントリ番号です: %d", entry_index);
        return -1;
    }

    const playlist_entry_t *e = &p->playlist->entries[entry_index];
    const playlist_source_t *src = &p->playlist->sources[e->source_index];

    if (e->source_index != p->current_source) {
        /* ソースをまたぐ: 現在のemuを閉じて開き直す。
         * これがm3uの複数ファイル参照(SPEC 5.2-2)や、zip内の複数ファイル
         * (SPEC 5.3)に対応する箇所。 */
        close_current_emu(p);

        Music_Emu *emu = NULL;
        gme_err_t err;

        if (src->zip_entry) {
            /* zip由来のソース: その都度展開してメモリから開く。
             * 一時ファイルはディスクに書かない (SPEC 5.3)。 */
            int idx = archive_find(p->playlist->archive, src->zip_entry);
            if (idx < 0) {
                LOG_ERR("zip内にファイルが見つかりません: %s", src->zip_entry);
                return -1;
            }
            void *data = NULL;
            size_t size = 0;
            if (archive_extract(p->playlist->archive, idx, &data, &size) != 0) {
                return -1;
            }
            err = gme_open_data(data, (long)size, &emu, p->config->sample_rate);
            free(data); /* gme_open_data はデータをコピーするのでここで解放してよい */
            if (err) {
                LOG_ERR("gme_open_data(zip内 %s): %s", src->zip_entry, err);
                return -1;
            }
        } else {
            err = gme_open_file(src->fs_path, &emu, p->config->sample_rate);
            if (err) {
                LOG_ERR("gme_open_file(%s): %s", src->fs_path, err);
                return -1;
            }
        }

        if (src->m3u_text) {
            err = gme_load_m3u_data(emu, src->m3u_text, (long)src->m3u_len);
            if (err) {
                LOG_WARN("m3uの再読み込みに失敗しました(%s): %s", src->display_path, err);
            }
        }

        configure_new_emu(p, emu);

        p->emu = emu;
        p->current_source = e->source_index;
    }

    if (start_track_at(p, e->track_index) != 0) {
        return -1;
    }

    p->current_entry = entry_index;
    sync_shuffle(p); /* F-25: シャッフル順を「いま再生したエントリ」に合わせる */
    LOG_INFO("再生: [%d/%d] \"%s\" (%s)",
              entry_index + 1, p->playlist->entry_count, e->title, src->display_path);
    return 0;
}

void player_toggle_pause(player_t *p) {
    if (p->state == PLAYER_PLAYING) {
        audio_set_pause(&p->audio, 1);
        p->state = PLAYER_PAUSED;
        /* 一時停止するとコールバックが止まるため、直前の波形が
         * 凍りついたまま画面に残る。無音へ落としておく (F-14)。 */
        audio_clear_scope(&p->audio);
    } else if (p->state == PLAYER_PAUSED) {
        audio_set_pause(&p->audio, 0);
        p->state = PLAYER_PLAYING;
    }
}

void player_snapshot_scope(player_t *p, short *out, int n) {
    audio_snapshot_scope(&p->audio, out, n);
}

int player_is_track_ended(player_t *p) {
    return audio_poll_track_ended(&p->audio);
}

int player_next_track(player_t *p) {
    if (!p->playlist || p->current_entry < 0) return -1;
    sync_shuffle(p);

    int next;

    if (p->config->repeat_mode == REPEAT_ONE) {
        /* SPEC 5.4: REPEAT_ONE は常に同一トラックを再開する。
         * シャッフルより優先する(shuffle.posは動かさない)。 */
        next = p->current_entry;
    } else if (p->shuffle.order) {
        /* F-25: シャッフル順で次へ。wrapはREPEAT_ALLと同じ判定を使う
         * (最終トラックの次で先頭に戻るか止まるか、という既存の
         * repeat_modeの意味をそのままシャッフル順に適用する)。 */
        next = shuffle_next(&p->shuffle, p->config->repeat_mode == REPEAT_ALL);
        if (next < 0) {
            LOG_INFO("シャッフル: 最終トラックに到達しました (REPEAT_NONE) -> 停止します");
            close_current_emu(p);
            p->current_entry = -1;
            return -1;
        }
    } else {
        next = p->current_entry + 1;
        if (next >= p->playlist->entry_count) {
            if (p->config->repeat_mode == REPEAT_ALL) {
                next = 0;
            } else {
                LOG_INFO("最終トラックに到達しました (REPEAT_NONE) -> 停止します");
                close_current_emu(p);
                p->current_entry = -1;
                return -1;
            }
        }
    }

    return player_play_entry(p, next);
}

int player_prev_track(player_t *p) {
    if (!p->playlist || p->current_entry < 0) return -1;
    sync_shuffle(p);

    int prev;

    if (p->shuffle.order) {
        prev = shuffle_prev(&p->shuffle, p->config->repeat_mode == REPEAT_ALL);
        /* シーケンシャル版と同じく、先頭で止まる場合はSTOPPEDにはせず
         * 現在のトラックへ留まる(replay)。 */
        if (prev < 0) prev = p->current_entry;
    } else {
        prev = p->current_entry - 1;
        if (prev < 0) {
            prev = (p->config->repeat_mode == REPEAT_ALL) ? (p->playlist->entry_count - 1) : 0;
        }
    }

    return player_play_entry(p, prev);
}

int player_seek(player_t *p, int msec) {
    if (!p->emu || p->state == PLAYER_STOPPED) return -1;

    audio_lock(&p->audio);
    gme_err_t err = gme_seek(p->emu, msec);
    audio_unlock(&p->audio);
    if (err) {
        LOG_ERR("gme_seek(%d): %s", msec, err);
        return -1;
    }

    audio_clear_track_ended(&p->audio);
    return 0;
}

int player_tell_ms(player_t *p) {
    if (!p->emu || p->state == PLAYER_STOPPED) return 0;

    audio_lock(&p->audio);
    int ms = gme_tell(p->emu);
    audio_unlock(&p->audio);
    return ms;
}

int player_current_duration_ms(const player_t *p) {
    if (!p->playlist || p->current_entry < 0) return 0;
    /* duration_ms(フェード開始時刻)だけでなく、フェードの分(fade_len_ms)を
     * 足した「実際に無音になる時刻」を返す。フェード中の経過時間が
     * ここで返す合計を追い越して見えるのを防ぐ(start_track_at()参照)。 */
    return p->playlist->entries[p->current_entry].duration_ms + p->fade_len_ms;
}

void player_apply_config(player_t *p) {
    /* stereo_depth / eq_*: 現在開いているemuへ即時反映する。
     * Effects_Buffer::config() も Classic_Emu::set_equalizer_() も
     * 再確保を行わないため、再生中に呼んでも安全(vendor/game-music-emu/gme/
     * Effects_Buffer.cpp, Classic_Emu.cpp 参照)。ただしいずれも
     * gme_play() と同時に走らせてはならないので audio_lock で囲む。
     * emuが無い(未再生)場合は何もしない -- 次に player_play_entry() が
     * 開くときに configure_new_emu() が同じ値を焼き込む。 */
    if (p->emu) {
        audio_lock(&p->audio);
        gme_set_stereo_depth(p->emu, p->config->stereo_depth);
        apply_equalizer(p->emu, p->config);
        audio_unlock(&p->audio);
    }

    /* repeat_mode / default_length_sec / fade_length_ms は config がポインタに
     * 変わったため、player側で何もしなくても次にそれぞれを読む箇所
     * (player_next_track / start_track_at)が自然に新しい値を見る。 */
}

void player_stop(player_t *p) {
    close_current_emu(p);
    p->current_entry = -1;
}
