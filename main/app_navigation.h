// Pure three-key navigation; raw press/release edges never reach this reducer.
#pragma once

#include <stdint.h>

typedef enum {
    APP_PAGE_HOME,
    APP_PAGE_MENU,
    APP_PAGE_AI,
    APP_PAGE_SYSTEM,
    APP_PAGE_SETTINGS,
} app_page_t;

typedef enum {
    APP_MENU_AI,
    APP_MENU_SYSTEM,
    APP_MENU_SETTINGS,
    APP_MENU_COUNT,
} app_menu_item_t;

typedef enum {
    APP_KEY_UP,
    APP_KEY_DOWN,
    APP_KEY_OK,
} app_key_t;

typedef enum {
    APP_GESTURE_CLICK,
    APP_GESTURE_DOUBLE,
    APP_GESTURE_LONG,
} app_gesture_t;

typedef enum {
    APP_ACTION_NONE,
    APP_ACTION_SELECTION,
    APP_ACTION_PAGE,
    APP_ACTION_VOLUME_UP,
    APP_ACTION_VOLUME_DOWN,
    APP_ACTION_MUTE,
    APP_ACTION_PREVIEW,
    APP_ACTION_AI_TALK,
    APP_ACTION_AI_SCROLL_UP,
    APP_ACTION_AI_SCROLL_DOWN,
    APP_ACTION_AI_RESET,
} app_action_t;

typedef struct {
    app_page_t page;
    uint8_t selection; // app_menu_item_t; retained when returning from a page.
} app_navigation_t;

app_action_t app_navigation_handle(app_navigation_t *state, app_key_t key,
                                   app_gesture_t gesture);
