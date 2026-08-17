/*
 * herz_hook.c — LD_PRELOAD hook untuk herz-kimmy.sh (NCZ MLBB ft HERZ V 21.95)
 *
 * Strategi: HOOK cURL (bukan patch global binary)
 *  - intercept system() / popen() yang menjalankan curl login
 *  - ganti response server dengan JSON VALID (format convex, lihat cara_konek_convex.md)
 *  - tulis ke cache file yang binary baca: /data/local/tmp/.ncz_last_json_<pkg>.dat
 *  - binary menganggap login sukses -> menu kebuka -> fitur jalan
 *  - binary TIDAK di-patch sama sekali (game tidak error)
 *
 * Dibuild dengan DobbyHook (jmpews/Dobby) via NDK, dipakai via:
 *   LD_PRELOAD=/data/local/tmp/herz_hook.so ./herz-kimmy.sh ...
 *
 * Catatan rng/timestamp: server convex butuh rng dalam +/-30 detik dari now.
 * Di sini kita generate time(NULL) langsung -> selalu valid.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>

#include "dobby.h"

/* ------------------------------------------------------------------ */
/* config                                                              */
/* ------------------------------------------------------------------ */
#define LOG_TAG "HERZ_HOOK"

/* response JSON valid — format sama dengan cara_konek_convex.md */
static const char *CACHE_DIR = "/data/local/tmp";
static const char *CACHE_PREFIX = ".ncz_last_json_";
static const char *CACHE_SUFFIX = ".dat";

/* expired selalu jauh di masa depan biar ga kena "MOD EXPIRED" */
#define EXPIRED_STR "15 - Des - 2099 12:00:00"
#define EXPIRED_TS  4099999999LL   /* ~2099 */

/* ------------------------------------------------------------------ */
/* logging ke file + logcat                                           */
/* ------------------------------------------------------------------ */
static void log_msg(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    FILE *f = fopen("/data/local/tmp/.herz_hook.log", "a");
    if (f) {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char ts[64];
        strftime(ts, sizeof(ts), "%H:%M:%S", tm);
        fprintf(f, "[%s] %s\n", ts, buf);
        fclose(f);
    }
}

/* ------------------------------------------------------------------ */
/* response JSON generator                                            */
/* ------------------------------------------------------------------ */
static int write_cache_response(const char *cmd, const char *pkg_hint) {
    char path[512];
    char json[4096];
    time_t now = time(NULL);

    /* package name: ambil dari command kalau ada package_name=, fallback com.mobile.legends.usa */
    char pkg[256] = "com.mobile.legends.usa";
    if (pkg_hint && pkg_hint[0]) {
        snprintf(pkg, sizeof(pkg), "%s", pkg_hint);
    } else if (cmd) {
        const char *pn = strstr(cmd, "package_name=");
        if (pn) {
            pn += strlen("package_name=");
            char tmp[256];
            int i = 0;
            while (pn[i] && pn[i] != '\'' && pn[i] != '"' && pn[i] != ' ' && pn[i] != '&' && i < 255) {
                tmp[i] = pn[i];
                i++;
            }
            tmp[i] = 0;
            if (i > 0) snprintf(pkg, sizeof(pkg), "%s", tmp);
        }
    }

    /* coba cari redirect path di command: > /data/local/tmp/.ncz_last_json_... */
    const char *rp = cmd ? strstr(cmd, ".ncz_last_json_") : NULL;
    if (rp) {
        /* path dimulai dari "/" sebelum .ncz_last_json_ */
        const char *start = rp;
        while (start > cmd && start[-1] != ' ' && start[-1] != '\'' && start[-1] != '"' && start[-1] != '>' && start[-1] != ';') {
            if (start[-1] == '/') break;
            start--;
        }
        while (start > cmd && start[-1] != ' ' && start[-1] != '\'' && start[-1] != '"' && start[-1] != '>' && start[-1] != ';')
            start--;
        snprintf(path, sizeof(path), "%s", start);
        /* buang trailing chars (redirect > dst; dst berakhir di spasi/quote) */
        char *end = path;
        while (*end && *end != ' ' && *end != '\'' && *end != '"' && *end != ';' && *end != '\n')
            end++;
        *end = 0;
    } else {
        snprintf(path, sizeof(path), "%s/%s%s%s", CACHE_DIR, CACHE_PREFIX, pkg, CACHE_SUFFIX);
    }

    /* rng = unix ts sekarang (harus dalam 30 detik dari server time) */
    snprintf(json, sizeof(json),
        "{\"ok\":true,\"status\":true,\"reason\":\"success\","
        "\"seal\":\"96ce5f9743814c22352025eb8703fc39\","
        "\"data\":{"
        "\"token\":\"TOKEN-HERZ-OK-%ld\","
        "\"rng\":%ld,"
        "\"tittle\":\"MLBB\","
        "\"expired\":\"%s\","
        "\"server\":{\"ok\":true},"
        "\"key\":{\"expired\":\"%s\"},"
        "\"url\":\"https://filescit.my.id/gembelcit/device-report.php\""
        "}}",
        (long)now, (long)now, EXPIRED_STR, EXPIRED_STR);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        /* fallback: tulis ke beberapa nama cache yang mungkin */
        char alt[512];
        snprintf(alt, sizeof(alt), "%s/%s%s%s", CACHE_DIR, CACHE_PREFIX, "com.mobile.legends", CACHE_SUFFIX);
        fd = open(alt, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            log_msg("WARN: cannot write cache %s", path);
            return 0;
        }
        snprintf(path, sizeof(path), "%s", alt);
    }
    ssize_t w = write(fd, json, strlen(json));
    close(fd);

    log_msg("OK: injected response -> %s (%d bytes, rng=%ld)", path, (int)w, (long)now);
    return 1;
}

/* ------------------------------------------------------------------ */
/* deteksi command curl login                                         */
/* ------------------------------------------------------------------ */
static int is_curl_login(const char *cmd) {
    if (!cmd) return 0;
    if (!strstr(cmd, "curl")) return 0;
    if (strstr(cmd, "device-report") || strstr(cmd, "filescit") ||
        strstr(cmd, "convex.site") || strstr(cmd, "login_key=") ||
        strstr(cmd, ".ncz_last_json_"))
        return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* hook system()                                                      */
/* ------------------------------------------------------------------ */
typedef int (*system_fn)(const char *);
static system_fn orig_system = NULL;

static int fake_system(const char *cmd) {
    if (is_curl_login(cmd)) {
        log_msg("system(): intercept curl login: %.100s", cmd ? cmd : "(null)");
        write_cache_response(cmd, NULL);
        /* session/expiry juga di-patch via memory (lihat init) */
        return 0; /* sukses tanpa menjalankan curl */
    }
    return orig_system(cmd);
}

/* ------------------------------------------------------------------ */
/* hook popen()                                                       */
/* ------------------------------------------------------------------ */
typedef FILE *(*popen_fn)(const char *, const char *);
static popen_fn orig_popen = NULL;

static FILE *fake_popen(const char *cmd, const char *mode) {
    if (is_curl_login(cmd)) {
        log_msg("popen(): intercept curl login: %.100s", cmd ? cmd : "(null)");
        write_cache_response(cmd, NULL);
        /* return pipe kosong yang langsung EOF — binary baca cache file sendiri */
        return tmpfile();
    }
    return orig_popen(cmd, mode);
}

/* ------------------------------------------------------------------ */
/* constructor                                                        */
/* ------------------------------------------------------------------ */
__attribute__((constructor))
static void herz_hook_init(void) {
    log_msg("==== herz_hook loaded (LD_PRELOAD) ====");

    /* 1. hook system() */
    void *sys = DobbySymbolResolver(NULL, "system");
    if (sys) {
        if (DobbyHook(sys, (void *)fake_system, (void **)&orig_system) == 0) {
            log_msg("hooked system() @ %p", sys);
        } else {
            log_msg("ERROR: DobbyHook(system) failed");
        }
    } else {
        log_msg("ERROR: system symbol not found");
    }

    /* 2. hook popen() */
    void *pp = DobbySymbolResolver(NULL, "popen");
    if (pp) {
        if (DobbyHook(pp, (void *)fake_popen, (void **)&orig_popen) == 0) {
            log_msg("hooked popen() @ %p", pp);
        } else {
            log_msg("ERROR: DobbyHook(popen) failed");
        }
    }

    /* catatan: expired check di-handle dari response JSON (expired 2099),
     * G_EXPIRY_TS di-set binary dari parse "expired" field -> otomatis lolos.
     * Tidak perlu patch memory global (base address PIE random). */
}
