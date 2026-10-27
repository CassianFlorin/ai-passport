#include "app_navigation.h"

#include <stddef.h>

app_action_t app_navigation_handle(app_navigation_t *state, app_key_t key,
                                   app_gesture_t gesture)
{
    if (!state || key < APP_KEY_UP || key > APP_KEY_OK ||
        gesture < APP_GESTURE_CLICK || gesture > APP_GESTURE_LONG ||
        state->page < APP_PAGE_HOME || state->page > APP_PAGE_SETTINGS) {
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
        return state->page == APP_PAGE_SETTINGS && key == APP_KEY_DOWN
             ? APP_ACTION_PREVIEW : APP_ACTION_NONE;
    }

    switch (state->page) {
    case APP_PAGE_HOME:
        return APP_ACTION_NONE;
    case APP_PAGE_MENU: {
        if (key == APP_KEY_OK) {
            state->page = state->selection == 0 ? APP_PAGE_SYSTEM : APP_PAGE_SETTINGS;
            return APP_ACTION_PAGE;
        }
        uint8_t next = key == APP_KEY_UP ? 0 : 1;
        if (next == state->selection) return APP_ACTION_NONE;
        state->selection = next;
        return APP_ACTION_SELECTION;
    }
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
