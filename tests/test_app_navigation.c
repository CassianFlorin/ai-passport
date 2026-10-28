#include <assert.h>

#include "app_navigation.h"

static void test_home_entry(void)
{
    app_navigation_t nav = {0};

    // Only double OK leaves the identity page; no homepage action changes sound.
    for (app_key_t key = APP_KEY_UP; key <= APP_KEY_OK; ++key) {
        for (app_gesture_t gesture = APP_GESTURE_CLICK;
             gesture <= APP_GESTURE_LONG; ++gesture) {
            if (key == APP_KEY_OK && gesture == APP_GESTURE_DOUBLE) continue;
            assert(app_navigation_handle(&nav, key, gesture) == APP_ACTION_NONE);
            assert(nav.page == APP_PAGE_HOME && nav.selection == 0);
        }
    }
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_DOUBLE) == APP_ACTION_PAGE);
    assert(nav.page == APP_PAGE_MENU && nav.selection == 0);
    // Another double cannot open a function while the user is at the list.
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_DOUBLE) == APP_ACTION_NONE);
    assert(nav.page == APP_PAGE_MENU);
}

static void test_function_list(void)
{
    app_navigation_t nav = { .page = APP_PAGE_MENU };
    const app_page_t pages[APP_MENU_COUNT] = {
        APP_PAGE_AI, APP_PAGE_SYSTEM, APP_PAGE_SETTINGS,
    };

    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_CLICK) == APP_ACTION_NONE);
    assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_LONG) == APP_ACTION_NONE);
    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_LONG) == APP_ACTION_NONE);
    for (unsigned i = 0; i < APP_MENU_COUNT; ++i) {
        assert(nav.selection == i);
        assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_DOUBLE) == APP_ACTION_NONE);
        assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_CLICK) == APP_ACTION_PAGE);
        assert(nav.page == pages[i] && nav.selection == i);
        if (nav.page == APP_PAGE_SYSTEM) {
            // The status page cannot start recording or change sound settings.
            for (app_key_t key = APP_KEY_UP; key <= APP_KEY_OK; ++key) {
                assert(app_navigation_handle(&nav, key, APP_GESTURE_CLICK) == APP_ACTION_NONE);
                assert(app_navigation_handle(&nav, key, APP_GESTURE_DOUBLE) == APP_ACTION_NONE);
            }
            assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_LONG) == APP_ACTION_NONE);
        }
        assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_LONG) == APP_ACTION_PAGE);
        assert(nav.page == APP_PAGE_MENU && nav.selection == i);
        assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_DOUBLE) ==
               (i + 1 < APP_MENU_COUNT ? APP_ACTION_SELECTION : APP_ACTION_NONE));
    }
    assert(nav.selection == APP_MENU_SETTINGS);
    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_DOUBLE) == APP_ACTION_SELECTION);
    assert(nav.selection == APP_MENU_SYSTEM);
    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_CLICK) == APP_ACTION_SELECTION);
    assert(nav.selection == APP_MENU_AI);
    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_DOUBLE) == APP_ACTION_NONE);
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_LONG) == APP_ACTION_PAGE);
    assert(nav.page == APP_PAGE_HOME && nav.selection == APP_MENU_AI);
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_DOUBLE) == APP_ACTION_PAGE);
    assert(nav.page == APP_PAGE_MENU && nav.selection == APP_MENU_AI);
}

static void test_ai(void)
{
    app_navigation_t nav = { .page = APP_PAGE_AI, .selection = APP_MENU_AI };

    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_CLICK) == APP_ACTION_AI_TALK);
    // OK double belongs to the homepage and must never start a recording.
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_DOUBLE) == APP_ACTION_NONE);
    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_CLICK) == APP_ACTION_AI_SCROLL_UP);
    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_DOUBLE) == APP_ACTION_AI_SCROLL_UP);
    assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_CLICK) ==
           APP_ACTION_AI_SCROLL_DOWN);
    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_LONG) == APP_ACTION_AI_RESET);
    assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_LONG) == APP_ACTION_NONE);
    assert(nav.page == APP_PAGE_AI);
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_LONG) == APP_ACTION_PAGE);
    assert(nav.page == APP_PAGE_MENU && nav.selection == APP_MENU_AI);
}

static void test_settings(void)
{
    app_navigation_t nav = { .page = APP_PAGE_SETTINGS, .selection = APP_MENU_SETTINGS };

    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_CLICK) == APP_ACTION_VOLUME_UP);
    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_DOUBLE) == APP_ACTION_VOLUME_UP);
    assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_CLICK) == APP_ACTION_VOLUME_DOWN);
    assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_DOUBLE) == APP_ACTION_VOLUME_DOWN);
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_CLICK) == APP_ACTION_MUTE);
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_DOUBLE) == APP_ACTION_NONE);
    assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_LONG) == APP_ACTION_PREVIEW);
    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_LONG) == APP_ACTION_NONE);
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_LONG) == APP_ACTION_PAGE);
    assert(nav.page == APP_PAGE_MENU && nav.selection == APP_MENU_SETTINGS);
}

int main(void)
{
    test_home_entry();
    test_function_list();
    test_ai();
    test_settings();

    app_navigation_t nav = {0};
    assert(app_navigation_handle(&nav, (app_key_t)99, APP_GESTURE_CLICK) == APP_ACTION_NONE);
    assert(app_navigation_handle(&nav, APP_KEY_OK, (app_gesture_t)99) == APP_ACTION_NONE);
    assert(app_navigation_handle(0, APP_KEY_OK, APP_GESTURE_CLICK) == APP_ACTION_NONE);
    nav.page = (app_page_t)99;
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_LONG) == APP_ACTION_NONE);
    nav.page = APP_PAGE_MENU;
    nav.selection = APP_MENU_COUNT;
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_CLICK) == APP_ACTION_NONE);
    return 0;
}
