/* MobileGL - dispatcher/dispatcher.h
 * mg-3backends: unified renderer entry (libmobileglues.so).
 * SPDX-License-Identifier: LGPL-3.0-only
 */
#ifndef MOBILEGL_DISPATCHER_H
#define MOBILEGL_DISPATCHER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve `name` against the active backend core: dlsym first, then the
 * core's own eglGetProcAddress. Returns NULL when neither has it (the
 * generated forwarders degrade to no-ops in that case). */
void *mg_core_bind(const char *name);

/* Idempotent: reads MOBILEGL_BACKEND_TYPE, dlopens the matching core lib
 * next to the dispatcher and caches its eglGetProcAddress. Runs in a
 * constructor and defensively before the first bind. */
void mg_dispatch_init(void);

/* Lazy per-symbol bind used by generated forwarders. */
#define MG_BIND(v, n) \
    do { if (!(v)) { *(void **)(&(v)) = mg_core_bind(n); } } while (0)

#ifdef __cplusplus
}
#endif

#endif /* MOBILEGL_DISPATCHER_H */
