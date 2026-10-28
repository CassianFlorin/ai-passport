#include "app_navigation.h"

#include <stddef.h>

app_action_t app_navigation_handle(app_navigation_t *state, app_key_t key,
                                   app_gesture_t gesture)
{
    if (!state || key < APP_KEY_UP || key > APP_KEY_OK ||
        gesture < APP_GESTURE_CLICK || gesture > APP_GESTURE_LONG ||
        state->page < APP_PAGE_HOME || state->page > APP_PAGE_SETTINGS ||
        state->selection >= APP_MENU_COUNT) {
        return APP_ACTION_NONE;
    }

    // The BSP emits one classified click or double event after its click window.
    // Keep OK double separate so opening the list cannot also open a function.
    if (gesture == APP_GESTURE_DOUBLE && key == APP_KEY_OK) {
        if (state->page != APP_PAGE_HOME) return APP_ACTION_NONE;
        state->page = APP_PAGE_MENU;
        return APP_ACTION_PAGE;
    }

    if (gesture == APP_GESTURE_LONG) {
        if (key == APP_KEY_OK && state->page != APP_PAGE_HOME) {
            state->page = state->page == APP_PAGE_MENU ? APP_PAGE_HOME : APP_PAGE_MENU;
            return APP_ACTION_PAGE;
        }
        if (state->page == APP_PAGE_AI && key == APP_KEY_UP) return APP_ACTION_AI_RESET;
        return state->page == APP_PAGE_SETTINGS && key == APP_KEY_DOWN
             ? APP_ACTION_PREVIEW : APP_ACTION_NONE;
    }

    switch (state->page) {
    case APP_PAGE_HOME:
        return APP_ACTION_NONE;
    case APP_PAGE_MENU: {
        if (key == APP_KEY_OK) {
            static const app_page_t pages[APP_MENU_COUNT] = {
                APP_PAGE_AI, APP_PAGE_SYSTEM, APP_PAGE_SETTINGS,
            };
            state->page = pages[state->selection];
            return APP_ACTION_PAGE;
        }
        // Move one row per classified gesture and clamp at either end.
        uint8_t next = state->selection;
        if (key == APP_KEY_UP && next > 0) next--;
        if (key == APP_KEY_DOWN && next + 1 < APP_MENU_COUNT) next++;
        if (next == state->selection) return APP_ACTION_NONE;
        state->selection = next;
        return APP_ACTION_SELECTION;
    }
    case APP_PAGE_AI:
        // OK advances the conversation; an OK double is ignored above.
        return key == APP_KEY_UP ? APP_ACTION_AI_SCROLL_UP
             : key == APP_KEY_DOWN ? APP_ACTION_AI_SCROLL_DOWN : APP_ACTION_AI_TALK;
    case APP_PAGE_SYSTEM:
        return APP_ACTION_NONE;
    case APP_PAGE_SETTINGS:
        // Keep the existing UP/DOWN double gestures as one volume step.
        return key == APP_KEY_UP ? APP_ACTION_VOLUME_UP
             : key == APP_KEY_DOWN ? APP_ACTION_VOLUME_DOWN : APP_ACTION_MUTE;
    default:
        return APP_ACTION_NONE;
    }
}
