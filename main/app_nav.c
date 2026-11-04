// main/app_nav.c —— 见 app_nav.h。
#include "app_nav.h"

#include <stddef.h>
#include <string.h>

void app_nav_init(app_nav_t *n, int64_t now_ms) {
    memset(n, 0, sizeof(*n));
    n->page = APP_PAGE_HOME;
    n->idle = APP_IDLE_AWAKE;
    n->last_activity_ms = now_ms;
}

void app_nav_set_items(app_nav_t *n, app_page_t page, uint8_t items) {
    if (page >= APP_PAGE_COUNT) return;
    n->items[page] = items;
    if (items == 0) {
        n->cursor[page] = 0;
    } else if (n->cursor[page] >= items) {
        n->cursor[page] = items - 1;
    }
}

static app_nav_action_t move_cursor(app_nav_t *n, int dir) {
    uint8_t items = n->items[n->page];
    if (items <= 1) return APP_NAV_NONE;
    uint8_t *c = &n->cursor[n->page];
    *c = (uint8_t)((*c + items + dir) % items);
    return APP_NAV_CURSOR;
}

static app_nav_action_t move_page(app_nav_t *n, int dir) {
    n->page = (app_page_t)((n->page + APP_PAGE_COUNT + dir) % APP_PAGE_COUNT);
    return APP_NAV_PAGE;
}

app_nav_action_t app_nav_key(app_nav_t *n, app_key_t key, int64_t now_ms) {
    n->last_activity_ms = now_ms;
    if (n->idle != APP_IDLE_AWAKE) {
        n->idle = APP_IDLE_AWAKE;
        return APP_NAV_WAKE;
    }
    switch (key) {
    case APP_KEY_UP:        return move_page(n, -1);
    case APP_KEY_DOWN:      return move_page(n, +1);
    case APP_KEY_OK:        return move_cursor(n, +1);
    case APP_KEY_OK_DOUBLE: return move_cursor(n, -1);
    case APP_KEY_OK_LONG:   return APP_NAV_ACTION;
    }
    return APP_NAV_NONE;
}

bool app_nav_poke(app_nav_t *n, int64_t now_ms) {
    n->last_activity_ms = now_ms;
    bool was_idle = n->idle != APP_IDLE_AWAKE;
    n->idle = APP_IDLE_AWAKE;
    return was_idle;
}

bool app_nav_tick(app_nav_t *n, int64_t now_ms, int64_t dim_ms, int64_t off_ms) {
    int64_t quiet = now_ms - n->last_activity_ms;
    app_idle_t want = APP_IDLE_AWAKE;
    if (off_ms > 0 && quiet >= off_ms) {
        want = APP_IDLE_OFF;
    } else if (dim_ms > 0 && quiet >= dim_ms) {
        want = APP_IDLE_DIM;
    }
    if (want == n->idle) return false;
    n->idle = want;
    return true;
}

uint16_t app_nav_next_screen_off(uint16_t minutes) {
    static const uint16_t opts[] = APP_SCREEN_OFF_OPTIONS;
    const size_t n = sizeof(opts) / sizeof(opts[0]);
    for (size_t i = 0; i < n; i++) {
        if (opts[i] == minutes) return opts[(i + 1) % n];
    }
    return opts[0];
}

void app_nav_idle_delays(uint16_t minutes, int64_t dim_max_ms, int64_t *dim_ms, int64_t *off_ms) {
    *off_ms = (int64_t)minutes * 60000;
    if (minutes == 0) {
        *dim_ms = dim_max_ms;
    } else {
        *dim_ms = *off_ms / 2 < dim_max_ms ? *off_ms / 2 : dim_max_ms;
    }
}
