/* log.h - 軽量ロギングマクロ。すべて stderr に出力する。 (SPEC 12) */
#ifndef MUGBS_LOG_H
#define MUGBS_LOG_H

#include <stdio.h>

#define LOG_INFO(fmt, ...) \
    fprintf(stderr, "[INFO] " fmt "\n", ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__)

#define LOG_ERR(fmt, ...) \
    fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)

#endif /* MUGBS_LOG_H */
