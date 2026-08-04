#include "player.h"

#include <string.h>

#include "log.h"

/* 曲長判定 (SPEC 5.1 との乖離#1への対応):
 * gme_info_t.play_length は曲長不明時に -1 ではなく 150000(既定150秒)を
 * 返してしまうため、これだけでは「本当に不明か」を判定できない。
 * 判定には length（総曲長）と intro_length/loop_length（ループ構造）を使う。
 * どちらも無ければ真に不明 → config の default_length_sec を使う (F-08)。
 *
 * P1〜P2はここで直接 gme_track_info を見て計算する暫定実装。
 * P3 で playlist_entry_t.play_length_ms に集約したらそちらを使う。 */
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
    p->state = PLAYER_STOPPED;
}

int player_init(player_t *p, const mugbs_config_t *config) {
    memset(p, 0, sizeof(*p));
    p->config = *config;
    p->state = PLAYER_STOPPED;

    if (audio_init(&p->audio, config->sample_rate) != 0) {
        return -1;
    }
    return 0;
}

void player_shutdown(player_t *p) {
    close_current_emu(p);
    audio_shutdown(&p->audio);
}

int player_open_and_play(player_t *p, const char *path, int track_index) {
    close_current_emu(p);

    Music_Emu *emu = NULL;
    gme_err_t err = gme_open_file(path, &emu, p->config.sample_rate);
    if (err) {
        LOG_ERR("gme_open_file(%s): %s", path, err);
        return -1;
    }

    int track_count = gme_track_count(emu);
    if (track_index < 0 || track_index >= track_count) {
        LOG_ERR("トラック番号が範囲外です: %d (全%dトラック)", track_index + 1, track_count);
        gme_delete(emu);
        return -1;
    }

    gme_enable_accuracy(emu, 1);
    gme_set_stereo_depth(emu, p->config.stereo_depth);

    err = gme_start_track(emu, track_index);
    if (err) {
        LOG_ERR("gme_start_track(%d): %s", track_index, err);
        gme_delete(emu);
        return -1;
    }

    gme_info_t *info = NULL;
    err = gme_track_info(emu, &info, track_index);
    if (!err) {
        int fade_at = fade_start_ms(info, &p->config);
        int fade_len = info->fade_length > 0 ? info->fade_length : p->config.fade_length_ms;
        gme_set_fade_msecs(emu, fade_at, fade_len);

        LOG_INFO("再生開始: %s [%d/%d] \"%s\" by %s (フェード開始 %dms, 長さ %dms)",
                  path, track_index + 1, track_count,
                  info->song[0] ? info->song : "(no title)",
                  info->author[0] ? info->author : "(unknown)",
                  fade_at, fade_len);
        gme_free_info(info); /* gme_track_info はヒープを返すので必ず解放する */
    } else {
        LOG_WARN("gme_track_info(%s): %s。既定長 %d 秒でフェード開始します",
                  path, err, p->config.default_length_sec);
        gme_set_fade_msecs(emu, p->config.default_length_sec * 1000, p->config.fade_length_ms);
    }

    p->emu = emu;
    audio_set_emu(&p->audio, emu);
    audio_set_pause(&p->audio, 0);
    p->state = PLAYER_PLAYING;
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
