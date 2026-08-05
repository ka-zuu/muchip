#include "player.h"

#include <stdlib.h>
#include <string.h>

#include "archive.h"
#include "log.h"

/* 曲長判定 (SPEC 5.1 との乖離#1への対応):
 * gme_info_t.play_length は曲長不明時に -1 ではなく 150000(既定150秒)を
 * 返してしまうため、これだけでは「本当に不明か」を判定できない。
 * 判定には length（総曲長）と intro_length/loop_length（ループ構造）を使う。
 * どちらも無ければ真に不明 → config の default_length_sec を使う (F-08)。 */
static int fade_start_ms(const gme_info_t *info, const mugbs_config_t *cfg) {
    if (info->length > 0) return info->length;
    if (info->intro_length > 0 && info->loop_length > 0) return info->play_length;
    return cfg->default_length_sec * 1000;
}

static void close_current_emu(player_t *p) {
    if (!p->emu) return;

    /* audio コールバックからの参照を先に外してから delete する。
     * gme_delete をコールバック実行中に呼んではならない。 (SPEC 5.1 落とし穴2) */
    audio_set_emu(&p->audio, NULL);
    gme_delete(p->emu);
    p->emu = NULL;
    p->current_source = -1;
    p->state = PLAYER_STOPPED;
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
        fade_at = fade_start_ms(info, &p->config);
        fade_len = info->fade_length > 0 ? info->fade_length : p->config.fade_length_ms;
        gme_free_info(info); /* gme_track_info はヒープを返すので必ず解放する */
    } else {
        LOG_WARN("gme_track_info: %s。既定長 %d 秒でフェード開始します",
                  err, p->config.default_length_sec);
        fade_at = p->config.default_length_sec * 1000;
        fade_len = p->config.fade_length_ms;
    }

    audio_lock(&p->audio);
    gme_set_fade_msecs(p->emu, fade_at, fade_len);
    audio_unlock(&p->audio);

    p->state = PLAYER_PLAYING;
    /* track_ended フラグをクリアしつつ再設定する（emuポインタ自体は不変）。 */
    audio_set_emu(&p->audio, p->emu);
    audio_set_pause(&p->audio, 0);
    return 0;
}

int player_init(player_t *p, const mugbs_config_t *config) {
    memset(p, 0, sizeof(*p));
    p->config = *config;
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
    p->playlist = NULL;
    p->current_entry = -1;
}

int player_load_playlist(player_t *p, const playlist_t *pl) {
    close_current_emu(p);
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
            err = gme_open_data(data, (long)size, &emu, p->config.sample_rate);
            free(data); /* gme_open_data はデータをコピーするのでここで解放してよい */
            if (err) {
                LOG_ERR("gme_open_data(zip内 %s): %s", src->zip_entry, err);
                return -1;
            }
        } else {
            err = gme_open_file(src->fs_path, &emu, p->config.sample_rate);
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

        gme_enable_accuracy(emu, 1);
        gme_set_stereo_depth(emu, p->config.stereo_depth);

        p->emu = emu;
        p->current_source = e->source_index;
    }

    if (start_track_at(p, e->track_index) != 0) {
        return -1;
    }

    p->current_entry = entry_index;
    LOG_INFO("再生: [%d/%d] \"%s\" (%s)",
              entry_index + 1, p->playlist->entry_count, e->title, src->display_path);
    return 0;
}

void player_toggle_pause(player_t *p) {
    if (p->state == PLAYER_PLAYING) {
        audio_set_pause(&p->audio, 1);
        p->state = PLAYER_PAUSED;
    } else if (p->state == PLAYER_PAUSED) {
        audio_set_pause(&p->audio, 0);
        p->state = PLAYER_PLAYING;
    }
}

int player_is_track_ended(player_t *p) {
    return audio_poll_track_ended(&p->audio);
}

int player_next_track(player_t *p) {
    if (!p->playlist || p->current_entry < 0) return -1;

    int next = p->current_entry + 1;

    if (p->config.repeat_mode == REPEAT_ONE) {
        /* SPEC 5.4: REPEAT_ONE は常に同一トラックを再開する。 */
        next = p->current_entry;
    } else if (next >= p->playlist->entry_count) {
        if (p->config.repeat_mode == REPEAT_ALL) {
            next = 0;
        } else {
            LOG_INFO("最終トラックに到達しました (REPEAT_NONE) -> 停止します");
            close_current_emu(p);
            p->current_entry = -1;
            return -1;
        }
    }

    return player_play_entry(p, next);
}

int player_prev_track(player_t *p) {
    if (!p->playlist || p->current_entry < 0) return -1;

    int prev = p->current_entry - 1;
    if (prev < 0) {
        prev = (p->config.repeat_mode == REPEAT_ALL) ? (p->playlist->entry_count - 1) : 0;
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
