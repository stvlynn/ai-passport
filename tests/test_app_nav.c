// tests/test_app_nav.c —— 页面切换、页内选择、息屏唤醒的状态机。
#include "app_nav.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    app_nav_t n;
    app_nav_init(&n, 0);
    assert(n.page == APP_PAGE_HOME && n.idle == APP_IDLE_AWAKE);

    // 上/下 只翻页,首尾相接;与页内条目数无关。
    app_nav_set_items(&n, APP_PAGE_SYSTEM, 6);
    assert(app_nav_key(&n, APP_KEY_DOWN, 10) == APP_NAV_PAGE && n.page == APP_PAGE_FEED);
    assert(app_nav_key(&n, APP_KEY_UP, 20) == APP_NAV_PAGE && n.page == APP_PAGE_HOME);
    assert(app_nav_key(&n, APP_KEY_UP, 30) == APP_NAV_PAGE && n.page == APP_PAGE_LYNN);   // 总览左边是 Lynn
    assert(app_nav_key(&n, APP_KEY_UP, 32) == APP_NAV_PAGE && n.page == APP_PAGE_SYSTEM);  // 首尾相接
    // 最后一页(设备)上 上/下 仍然翻页,不移动菜单光标。
    assert(app_nav_key(&n, APP_KEY_UP, 35) == APP_NAV_PAGE && n.page == APP_PAGE_REPOS);
    assert(app_nav_key(&n, APP_KEY_DOWN, 36) == APP_NAV_PAGE && n.page == APP_PAGE_SYSTEM);
    assert(n.cursor[APP_PAGE_SYSTEM] == 0);
    assert(app_nav_key(&n, APP_KEY_DOWN, 40) == APP_NAV_PAGE && n.page == APP_PAGE_LYNN);
    assert(app_nav_key(&n, APP_KEY_DOWN, 41) == APP_NAV_PAGE && n.page == APP_PAGE_HOME);

    // 确定 单击/双击 在页内循环选择;0 或 1 个条目时不动。
    assert(app_nav_key(&n, APP_KEY_OK, 50) == APP_NAV_NONE);
    app_nav_set_items(&n, APP_PAGE_HOME, 1);
    assert(app_nav_key(&n, APP_KEY_OK, 55) == APP_NAV_NONE);
    app_nav_set_items(&n, APP_PAGE_HOME, 3);
    assert(app_nav_key(&n, APP_KEY_OK_DOUBLE, 60) == APP_NAV_CURSOR && n.cursor[APP_PAGE_HOME] == 2);
    assert(app_nav_key(&n, APP_KEY_OK, 70) == APP_NAV_CURSOR && n.cursor[APP_PAGE_HOME] == 0);
    n.page = APP_PAGE_SYSTEM;
    for (int i = 0; i < 5; i++) assert(app_nav_key(&n, APP_KEY_OK, 80) == APP_NAV_CURSOR);
    assert(n.cursor[APP_PAGE_SYSTEM] == 5);
    assert(app_nav_key(&n, APP_KEY_OK, 90) == APP_NAV_CURSOR && n.cursor[APP_PAGE_SYSTEM] == 0);
    assert(app_nav_key(&n, APP_KEY_OK_LONG, 95) == APP_NAV_ACTION && n.page == APP_PAGE_SYSTEM);

    // 条目减少时光标被夹回。
    n.page = APP_PAGE_FEED;
    app_nav_set_items(&n, APP_PAGE_FEED, 4);
    n.cursor[APP_PAGE_FEED] = 3;
    app_nav_set_items(&n, APP_PAGE_FEED, 2);
    assert(n.cursor[APP_PAGE_FEED] == 1);
    app_nav_set_items(&n, APP_PAGE_FEED, 0);
    assert(n.cursor[APP_PAGE_FEED] == 0);
    assert(app_nav_key(&n, APP_KEY_OK_LONG, 110) == APP_NAV_ACTION);

    // 息屏阶段:变暗 → 关屏;第一次按键只唤醒,不翻页。
    assert(!app_nav_tick(&n, 1000, 5000, 20000));
    assert(app_nav_tick(&n, 5110, 5000, 20000) && n.idle == APP_IDLE_DIM);
    assert(!app_nav_tick(&n, 6000, 5000, 20000));
    assert(app_nav_tick(&n, 20110, 5000, 20000) && n.idle == APP_IDLE_OFF);
    app_page_t page = n.page;
    assert(app_nav_key(&n, APP_KEY_DOWN, 21000) == APP_NAV_WAKE);
    assert(n.page == page && n.idle == APP_IDLE_AWAKE);
    assert(!app_nav_tick(&n, 22000, 5000, 20000));

    // 外部事件唤醒并重置计时;阶段为 0 表示关闭。
    assert(app_nav_tick(&n, 30000, 5000, 0) && n.idle == APP_IDLE_DIM);
    assert(!app_nav_tick(&n, 900000, 5000, 0));
    assert(app_nav_poke(&n, 900000) && n.idle == APP_IDLE_AWAKE);
    assert(!app_nav_poke(&n, 900001));
    assert(!app_nav_tick(&n, 904000, 5000, 0));

    // 息屏时间选项循环;未知值回到第一项。
    assert(app_nav_next_screen_off(1) == 5);
    assert(app_nav_next_screen_off(30) == 0);
    assert(app_nav_next_screen_off(0) == 1);
    assert(app_nav_next_screen_off(7) == 1);
    int64_t dim, off;
    app_nav_idle_delays(10, 90000, &dim, &off);
    assert(off == 600000 && dim == 90000);
    app_nav_idle_delays(1, 90000, &dim, &off);
    assert(off == 60000 && dim == 30000);       // 短息屏:变暗取一半
    app_nav_idle_delays(0, 90000, &dim, &off);
    assert(off == 0 && dim == 90000);           // 从不:只变暗
    // 从不息屏时,长时间无操作也只停在变暗。
    app_nav_init(&n, 0);
    assert(app_nav_tick(&n, 90000, dim, off) && n.idle == APP_IDLE_DIM);
    assert(!app_nav_tick(&n, 86400000, dim, off) && n.idle == APP_IDLE_DIM);

    puts("test_app_nav: PASS");
    return 0;
}
