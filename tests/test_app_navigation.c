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

    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_CLICK) == APP_ACTION_NONE);
    assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_LONG) == APP_ACTION_NONE);
    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_LONG) == APP_ACTION_NONE);
    assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_CLICK) == APP_ACTION_SELECTION);
    assert(nav.selection == 1 && nav.page == APP_PAGE_MENU);
    assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_CLICK) == APP_ACTION_NONE);
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_CLICK) == APP_ACTION_PAGE);
    assert(nav.page == APP_PAGE_SETTINGS && nav.selection == 1);
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_LONG) == APP_ACTION_PAGE);
    assert(nav.page == APP_PAGE_MENU && nav.selection == 1);

    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_DOUBLE) == APP_ACTION_SELECTION);
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_CLICK) == APP_ACTION_PAGE);
    assert(nav.page == APP_PAGE_SYSTEM && nav.selection == 0);
    // The metrics page has no shortcuts that bypass the function list.
    for (app_key_t key = APP_KEY_UP; key <= APP_KEY_OK; ++key) {
        assert(app_navigation_handle(&nav, key, APP_GESTURE_CLICK) == APP_ACTION_NONE);
        assert(app_navigation_handle(&nav, key, APP_GESTURE_DOUBLE) == APP_ACTION_NONE);
    }
    assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_LONG) == APP_ACTION_NONE);
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_LONG) == APP_ACTION_PAGE);
    assert(nav.page == APP_PAGE_MENU && nav.selection == 0);
    assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_DOUBLE) == APP_ACTION_SELECTION);
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_LONG) == APP_ACTION_PAGE);
    assert(nav.page == APP_PAGE_HOME && nav.selection == 1);
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_DOUBLE) == APP_ACTION_PAGE);
    assert(nav.page == APP_PAGE_MENU && nav.selection == 1);
}

static void test_settings(void)
{
    app_navigation_t nav = { .page = APP_PAGE_SETTINGS, .selection = 1 };

    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_CLICK) == APP_ACTION_VOLUME_UP);
    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_DOUBLE) == APP_ACTION_VOLUME_UP);
    assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_CLICK) == APP_ACTION_VOLUME_DOWN);
    assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_DOUBLE) == APP_ACTION_VOLUME_DOWN);
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_CLICK) == APP_ACTION_MUTE);
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_DOUBLE) == APP_ACTION_NONE);
    assert(app_navigation_handle(&nav, APP_KEY_DOWN, APP_GESTURE_LONG) == APP_ACTION_PREVIEW);
    assert(app_navigation_handle(&nav, APP_KEY_UP, APP_GESTURE_LONG) == APP_ACTION_NONE);
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_LONG) == APP_ACTION_PAGE);
    assert(nav.page == APP_PAGE_MENU && nav.selection == 1);
}

int main(void)
{
    test_home_entry();
    test_function_list();
    test_settings();

    app_navigation_t nav = {0};
    assert(app_navigation_handle(&nav, (app_key_t)99, APP_GESTURE_CLICK) == APP_ACTION_NONE);
    assert(app_navigation_handle(&nav, APP_KEY_OK, (app_gesture_t)99) == APP_ACTION_NONE);
    assert(app_navigation_handle(0, APP_KEY_OK, APP_GESTURE_CLICK) == APP_ACTION_NONE);
    nav.page = (app_page_t)99;
    assert(app_navigation_handle(&nav, APP_KEY_OK, APP_GESTURE_LONG) == APP_ACTION_NONE);
    return 0;
}
