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
static const char *mg_backend_from_config(void) {
    static char val[32];
    const char *dir_env = getenv("MG_DIR_PATH");
    char path[600], buf[16384];
    FILE *f;
    size_t n;
    const char *key, *colon, *q1, *q2;

    snprintf(path, sizeof(path), "%s/config.json",
             (dir_env && *dir_env) ? dir_env : "/sdcard/MG");
    f = fopen(path, "r");
    if (!f) {
        return NULL;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    /* Tolerant scan for  "backendType" : "DirectVulkan" — no JSON parser in
     * the dispatcher, and the cores' config parser must not be duplicated
     * here either; the plugin UI is the only writer of this key. */
    key = strstr(buf, "\"backendType\"");
    if (!key) {
        return NULL;
    }
    colon = strchr(key + sizeof("\"backendType\"") - 1, ':');
    if (!colon) {
        return NULL;
    }
    q1 = strchr(colon + 1, '"');
    if (!q1) {
        return NULL;
    }
    q2 = strchr(q1 + 1, '"');
    if (!q2 || q2 - q1 - 1 <= 0 || q2 - q1 - 1 >= (ptrdiff_t)sizeof(val)) {
        return NULL;
    }
    memcpy(val, q1 + 1, q2 - q1 - 1);
    val[q2 - q1 - 1] = '\0';
    MG_LOGI("backend from %s: \"%s\"", path, val);
    return val;
}

void mg_dispatch_init(void) {
    const char *be, *force;
    const char *lib;

    if (g_core) {
        return;
    }

    be = getenv("MOBILEGL_BACKEND_TYPE");
    if (!be || !*be) {
        be = mg_backend_from_config();
    }
    if (!be || !*be) {
        be = "DirectVulkan"; /* Air 6.0 default: Vulkan direct */
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
