/*
 * Workaround for upstream GNOME Mutter 50/51 bug #5215 + Tablet Sprite Cursor Hider:
 *
 * 1. Bug #5215 NULL-guard:
 *    meta_wayland_tablet_tool_check_focus_serial() calls
 *    wl_resource_get_client(tool->focus_surface->resource) without checking
 *    whether tool->focus_surface->resource is NULL when a Wayland client sends
 *    wp_cursor_shape_device_v1.set_shape while a stylus focus surface is being
 *    destroyed.
 *
 * 2. Tablet Tool Default Arrow Cursor Suppression:
 *    In Mutter 51.1, CLUTTER_SPRITE_ROLE_TABLET (role == 1) ignores
 *    MetaCursorTracker's pointer_visible state in meta_sprite_update_cursor()
 *    and renders a second mouse arrow cursor following the stylus. Since
 *    surface-pen-dot@linux-surface renders the native Windows 11-style white
 *    hover dot (and eraser ring), we return CLUTTER_CURSOR_NONE (1) for
 *    CLUTTER_SPRITE_ROLE_TABLET cursor queries and whenever pointer visibility
 *    is inhibited by the pen dot extension.
 *
 * Loaded ONLY into gnome-shell via systemd user drop-in
 * (~/.config/systemd/user/org.gnome.Shell@user.service.d/10-mutter-5215-nullguard.conf),
 * preserving 100% stock Arch mutter package integrity.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>

struct wl_client;
struct wl_resource;

#define CLUTTER_SPRITE_ROLE_TABLET 1
#define CLUTTER_CURSOR_NONE 1

static struct wl_client *(*real_wl_resource_get_client)(struct wl_resource *) = NULL;
static int (*real_clutter_sprite_get_role)(void *) = NULL;
static void *(*real_clutter_sprite_get_cursor)(void *) = NULL;
static int (*real_clutter_seat_query_state)(void *, void *, void *, void *) = NULL;
static int (*real_clutter_cursor_get_cursor_type)(void *) = NULL;
static void *(*real_clutter_backend_get_cursor)(void *, int) = NULL;
static void (*real_meta_cursor_tracker_inhibit_cursor_visibility)(void *) = NULL;
static void (*real_meta_cursor_tracker_uninhibit_cursor_visibility)(void *) = NULL;

static __thread int g_in_tablet_cursor_update = 0;
static volatile int g_pen_dot_inhibit_depth = 0;

static void resolve_symbols(void) {
    if (!real_wl_resource_get_client)
        real_wl_resource_get_client = dlsym(RTLD_NEXT, "wl_resource_get_client");
    if (!real_clutter_sprite_get_role)
        real_clutter_sprite_get_role = dlsym(RTLD_NEXT, "clutter_sprite_get_role");
    if (!real_clutter_sprite_get_cursor)
        real_clutter_sprite_get_cursor = dlsym(RTLD_NEXT, "clutter_sprite_get_cursor");
    if (!real_clutter_seat_query_state)
        real_clutter_seat_query_state = dlsym(RTLD_NEXT, "clutter_seat_query_state");
    if (!real_clutter_cursor_get_cursor_type)
        real_clutter_cursor_get_cursor_type = dlsym(RTLD_NEXT, "clutter_cursor_get_cursor_type");
    if (!real_clutter_backend_get_cursor)
        real_clutter_backend_get_cursor = dlsym(RTLD_NEXT, "clutter_backend_get_cursor");
    if (!real_meta_cursor_tracker_inhibit_cursor_visibility)
        real_meta_cursor_tracker_inhibit_cursor_visibility =
            dlsym(RTLD_NEXT, "meta_cursor_tracker_inhibit_cursor_visibility");
    if (!real_meta_cursor_tracker_uninhibit_cursor_visibility)
        real_meta_cursor_tracker_uninhibit_cursor_visibility =
            dlsym(RTLD_NEXT, "meta_cursor_tracker_uninhibit_cursor_visibility");
}

__attribute__((constructor))
static void init_mutter_5215_nullguard(void) {
    resolve_symbols();
}

struct wl_client *wl_resource_get_client(struct wl_resource *resource) {
    if (__builtin_expect(resource == NULL, 0))
        return NULL;
    if (__builtin_expect(real_wl_resource_get_client == NULL, 0))
        resolve_symbols();
    return real_wl_resource_get_client ? real_wl_resource_get_client(resource) : NULL;
}

int clutter_sprite_get_role(void *sprite) {
    if (__builtin_expect(real_clutter_sprite_get_role == NULL, 0))
        resolve_symbols();
    if (!real_clutter_sprite_get_role || !sprite)
        return 0;
    int role = real_clutter_sprite_get_role(sprite);
    g_in_tablet_cursor_update = (role == CLUTTER_SPRITE_ROLE_TABLET);
    uintptr_t ret_low12 = ((uintptr_t)__builtin_return_address(0)) & 0xfff;
    if (role == CLUTTER_SPRITE_ROLE_TABLET && (ret_low12 == 0xabc || ret_low12 == 0xdec))
        return 0; /* CLUTTER_SPRITE_ROLE_POINTER: obey pointer_visible in meta_sprite_update/sync_cursor */
    return role;
}

int clutter_seat_query_state(void *seat, void *sprite, void *coords, void *modifiers) {
    if (__builtin_expect(real_clutter_seat_query_state == NULL, 0))
        resolve_symbols();
    if (sprite && real_clutter_sprite_get_role)
        g_in_tablet_cursor_update = (real_clutter_sprite_get_role(sprite) == CLUTTER_SPRITE_ROLE_TABLET);
    return real_clutter_seat_query_state
        ? real_clutter_seat_query_state(seat, sprite, coords, modifiers)
        : 0;
}

void *clutter_sprite_get_cursor(void *sprite) {
    if (__builtin_expect(real_clutter_sprite_get_cursor == NULL, 0))
        resolve_symbols();
    if (sprite && real_clutter_sprite_get_role &&
        real_clutter_sprite_get_role(sprite) == CLUTTER_SPRITE_ROLE_TABLET) {
        g_in_tablet_cursor_update = 1;
        return NULL;
    }
    g_in_tablet_cursor_update = 0;
    return real_clutter_sprite_get_cursor ? real_clutter_sprite_get_cursor(sprite) : NULL;
}

int clutter_cursor_get_cursor_type(void *cursor) {
    if (__builtin_expect(real_clutter_cursor_get_cursor_type == NULL, 0))
        resolve_symbols();
    if (g_in_tablet_cursor_update || g_pen_dot_inhibit_depth > 0)
        return CLUTTER_CURSOR_NONE;
    return (real_clutter_cursor_get_cursor_type && cursor)
        ? real_clutter_cursor_get_cursor_type(cursor)
        : CLUTTER_CURSOR_NONE;
}

void *clutter_backend_get_cursor(void *backend, int cursor_type) {
    if (__builtin_expect(real_clutter_backend_get_cursor == NULL, 0))
        resolve_symbols();
    uintptr_t ret_low12 = ((uintptr_t)__builtin_return_address(0)) & 0xfff;
    if (ret_low12 == 0x88c || ret_low12 == 0x8b0 || ret_low12 == 0x8d4)
        g_in_tablet_cursor_update = 1;
    if (g_in_tablet_cursor_update || g_pen_dot_inhibit_depth > 0)
        cursor_type = CLUTTER_CURSOR_NONE;
    return real_clutter_backend_get_cursor
        ? real_clutter_backend_get_cursor(backend, cursor_type)
        : NULL;
}

void meta_cursor_tracker_inhibit_cursor_visibility(void *tracker) {
    if (__builtin_expect(real_meta_cursor_tracker_inhibit_cursor_visibility == NULL, 0))
        resolve_symbols();
    __atomic_add_fetch(&g_pen_dot_inhibit_depth, 1, __ATOMIC_SEQ_CST);
    if (real_meta_cursor_tracker_inhibit_cursor_visibility && tracker)
        real_meta_cursor_tracker_inhibit_cursor_visibility(tracker);
}

void meta_cursor_tracker_uninhibit_cursor_visibility(void *tracker) {
    if (__builtin_expect(real_meta_cursor_tracker_uninhibit_cursor_visibility == NULL, 0))
        resolve_symbols();
    int cur = __atomic_load_n(&g_pen_dot_inhibit_depth, __ATOMIC_SEQ_CST);
    while (cur > 0) {
        if (__atomic_compare_exchange_n(
                &g_pen_dot_inhibit_depth, &cur, cur - 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            break;
    }
    if (real_meta_cursor_tracker_uninhibit_cursor_visibility && tracker)
        real_meta_cursor_tracker_uninhibit_cursor_visibility(tracker);
}
