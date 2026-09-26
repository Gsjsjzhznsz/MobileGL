/* MobileGL - dispatcher/dispatcher.c
 * mg-3backends: unified renderer entry (libmobileglues.so).
 *
 * Air 6.0 merge contract: the MobileGL family presents ONE renderer entry
 * whose backend is selected in the launcher settings, exactly like the iOS
 * Air launcher's single "mg" entry:
 *
 *   MOBILEGL_BACKEND_TYPE=DirectVulkan -> Air 6.0 "Vulkan 直连"  (default)
 *   MOBILEGL_BACKEND_TYPE=DirectGLES   -> Air 6.0 "OpenGL 4.0"
 *   MOBILEGL_BACKEND_TYPE=MobileGlues  -> Air 6.0 "GLES"
 *
 * The dispatcher dlopens exactly one backend core that sits next to it:
 *   - libMobileGL.so : MobileGL core (DirectVulkan / DirectGLES backends,
 *                      selected inside the core by the same env value)
 *   - libmg_gles.so  : vendored MobileGlues core (GL -> OpenGL ES, FSR1)
 *
 * Backend selection: MOBILEGL_BACKEND_TYPE env first; when unset, the
 * "backendType" string in the MobileGlues config.json (MG_DIR_PATH or
 * /sdcard/MG) — the key the MobileGlues settings UI's Air 6.0-style picker
 * writes; default DirectVulkan ("Vulkan 直连").
 *
 * Every GL/EGL symbol the launcher resolves from the dispatcher is a lazy
 * forwarder generated from the Khronos headers (gen_forwarders.py), so both
 * cores stay byte-identical to their upstream builds — no symbol surgery,
 * no per-call branch inside the cores, one indirect jump per call here.
 *
 * SPDX-License-Identifier: LGPL-3.0-only
 */

#define _GNU_SOURCE /* dladdr/Dl_info on bionic */

#include "dispatcher.h"

#include <dlfcn.h>
#include <libgen.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

#ifdef __ANDROID__
#include <android/log.h>
#define MG_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "mg-dispatch", __VA_ARGS__)
#define MG_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "mg-dispatch", __VA_ARGS__)
#define MG_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "mg-dispatch", __VA_ARGS__)
#else
#include <stdio.h>
#define MG_LOGI(...) do { fprintf(stderr, "[mg-dispatch] " __VA_ARGS__); fputc('\n', stderr); } while (0)
#define MG_LOGW MG_LOGI
#define MG_LOGE MG_LOGI
#endif

static void *g_core = NULL;                /* dlopen handle of the active core */
static void *(*g_core_gpa)(const char *) = NULL; /* core eglGetProcAddress */
static const char *g_backend = "(unset)";  /* selected backend value */
static const char *g_core_lib = "(none)";  /* core lib we dlopened */
static int g_miss_logs = 0;

/* Path of the dispatcher itself, so cores are found regardless of how the
 * launcher extracted the plugin libs (sibling-of-self beats bare dlopen). */
static void mg_self_dir(char *out, size_t cap) {
    Dl_info info;
    out[0] = '\0';
    if (dladdr((void *)&mg_dispatch_init, &info) && info.dli_fname) {
        char self[512];
        snprintf(self, sizeof(self), "%s", info.dli_fname);
        const char *dir = dirname(self); /* dirname may modify its argument */
        snprintf(out, cap, "%s", dir ? dir : "");
    }
}

static void *mg_try_dlopen(const char *lib) {
    char path[600], dir[512];
    void *h;
    mg_self_dir(dir, sizeof(dir));
    if (dir[0]) {
        snprintf(path, sizeof(path), "%s/%s", dir, lib);
        h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (h) {
            g_core_lib = lib;
            return h;
        }
        MG_LOGW("dlopen(\"%s\") failed: %s", path, dlerror());
    }
    h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    if (h) {
        g_core_lib = lib;
        return h;
    }
    MG_LOGE("dlopen(\"%s\") failed: %s", lib, dlerror());
    return NULL;
}

/* mg-3backends (Air 6.0 switcher): the MobileGlues settings UI writes the
 * backend choice into the very config.json the cores already read. Env wins
 * (launcher-side selectable env), config.json is the fallback so the picker
 * in our own UI also drives launchers that never heard of the env.
 * Path resolution mirrors MobileGlues-cpp/config/config.cpp check_path():
 * MG_DIR_PATH env override, else /sdcard/MG. Returns a pointer into a static
 * buffer; mg_dispatch_init runs at most once, so that is safe. */

/* Tolerant scan for  "key" : <value>  in the MobileGlues config.json — no
 * JSON parser in the dispatcher, and the cores' config parser must not be
 * duplicated here either; the plugin UI is the only writer of these keys.
 * Value form follows org.json: quoted strings and bare numbers both appear
 * ("backendType": "DirectGLES", "fsr1Setting": 4). A bare number is copied
 * verbatim; the caller decides how to parse it.
 * Returns 1 and fills val on a hit, 0 when the file or the key is missing. */
static int mg_config_scan(const char *key, char *val, size_t cap) {
    static char buf[16384];
    char path[600], keybuf[64];
    FILE *f;
    size_t n;
    const char *needle, *keypos, *colon, *v;

    snprintf(path, sizeof(path), "%s/config.json",
             (getenv("MG_DIR_PATH") && *getenv("MG_DIR_PATH")) ? getenv("MG_DIR_PATH") : "/sdcard/MG");
    f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    needle = keybuf;
    snprintf(keybuf, sizeof(keybuf), "\"%s\"", key);
    keypos = strstr(buf, needle);
    if (!keypos) {
        return 0;
    }
    colon = strchr(keypos + strlen(needle), ':');
    if (!colon) {
        return 0;
    }
    v = colon + 1;
    while (*v == ' ' || *v == '\t') {
        v++;
    }
    if (*v == '"') {
        const char *q2 = strchr(v + 1, '"');
        if (!q2 || (size_t)(q2 - v - 1) <= 0 || (size_t)(q2 - v - 1) >= cap) {
            return 0;
        }
        memcpy(val, v + 1, q2 - v - 1);
        val[q2 - v - 1] = '\0';
        return 1;
    }
    /* Bare number token: take as many [0-9+-] as the JSON writer emitted. */
    {
        size_t m = 0;
        while ((v[m] == '-' || v[m] == '+' || (v[m] >= '0' && v[m] <= '9')) && m < cap - 1) {
            val[m] = v[m];
            m++;
        }
        val[m] = '\0';
        return m > 0;
    }
}

static const char *mg_backend_from_config(void) {
    static char val[32];
    if (!mg_config_scan("backendType", val, sizeof(val))) {
        return NULL;
    }
    MG_LOGI("backend from config.json: \"%s\"", val);
    return val;
}

void mg_dispatch_init(void) {
    const char *be, *force;
    const char *lib;
    char fsrVal[32];

    if (g_core) {
        return;
    }

    be = getenv("MOBILEGL_BACKEND_TYPE");
    if (!be || !*be) {
        be = mg_backend_from_config();
        /* The core (libMobileGL.so) picks DirectGLES/DirectVulkan from the same
         * env the dispatcher defaults to — pin the config.json answer into the
         * environment (never overwriting a real launcher env) so the entry lib
         * and the core can never disagree about which backend is active. */
        if (be && *be) {
            setenv("MOBILEGL_BACKEND_TYPE", be, 0);
        }
    }
    if (!be || !*be) {
        be = "DirectVulkan"; /* Air 6.0 default: Vulkan direct */
    }

    /* FSR1 settings ride the same config.json -> env bridge. The DirectGLES
     * core reads MOBILEGL_FSR1 / MOBILEGL_FSR1_SHARPNESS (env-only contract);
     * overwrite=0 keeps a real launcher env authoritative. fsr1Setting 0 is
     * "Disabled" — absent env means exactly that, so nothing is set. */
    if (mg_config_scan("fsr1Setting", fsrVal, sizeof(fsrVal))) {
        long fsr = strtol(fsrVal, NULL, 10);
        if (fsr >= 1 && fsr <= 4) {
            char digit[2] = {(char)('0' + fsr), '\0'};
            setenv("MOBILEGL_FSR1", digit, 0);
            MG_LOGI("fsr1 from config.json: preset %ld", fsr);
        }
    }
    if (mg_config_scan("fsr1Sharpness", fsrVal, sizeof(fsrVal))) {
        long sharp = strtol(fsrVal, NULL, 10);
        if (sharp >= 0 && sharp <= 100) {
            char sharpStr[8];
            snprintf(sharpStr, sizeof(sharpStr), "%ld", sharp);
            setenv("MOBILEGL_FSR1_SHARPNESS", sharpStr, 0);
            MG_LOGI("fsr1 from config.json: sharpness %ld", sharp);
        }
    }
    force = getenv("MOBILEGL_DISPATCHER_CORE"); /* debug override: full lib name */

    if (force && *force) {
        lib = force;
    } else if (!strcmp(be, "DirectVulkan") || !strcmp(be, "DirectGLES")) {
        lib = "libMobileGL.so"; /* core picks its backend from the same env */
    } else if (!strcmp(be, "MobileGlues") || !strcmp(be, "GLES")) {
        lib = "libmg_gles.so";
    } else {
        MG_LOGW("unknown MOBILEGL_BACKEND_TYPE=\"%s\", defaulting to DirectVulkan", be);
        be = "DirectVulkan";
        lib = "libMobileGL.so";
    }

    g_backend = be;
    g_core = mg_try_dlopen(lib);
    if (!g_core) {
        MG_LOGE("no backend core available (backend=%s) — GL/EGL calls will no-op", be);
        return;
    }
    g_core_gpa = (void *(*)(const char *))dlsym(g_core, "eglGetProcAddress");
    MG_LOGI("backend=%s core=%s (dispatcher ready)", g_backend, g_core_lib);
}

void *mg_core_bind(const char *name) {
    void *p;

    if (!g_core) {
        mg_dispatch_init();
    }
    if (g_core) {
        p = dlsym(g_core, name);
        if (p) {
            return p;
        }
        if (g_core_gpa) {
            p = g_core_gpa(name);
            if (p) {
                return p;
            }
        }
    }
    if (g_miss_logs < 10) {
        MG_LOGW("symbol not found in core: %s (forwarder will no-op)", name);
        g_miss_logs++;
    }
    return NULL;
}

/* Smart forward: the launcher (and LWJGL) resolve extension entry points
 * here. Prefer the core's own answer — its pointers point straight into the
 * core, which is both correct and faster than bouncing through forwarders.
 * When the core declines, fall back to our own forwarder table so names the
 * core resolves lazily still work. */
__attribute__((visibility("default")))
void *eglGetProcAddress(const char *procname) {
    if (!procname) {
        return NULL;
    }
    if (!g_core) {
        mg_dispatch_init();
    }
    if (g_core_gpa) {
        void *p = g_core_gpa(procname);
        if (p) {
            return p;
        }
    }
    if (g_core) {
        return dlsym(g_core, procname);
    }
    return NULL;
}
