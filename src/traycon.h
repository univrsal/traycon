/*
 * Copyright (c) 2026, Alex <uni@vrsal.cc>
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef TRAYCON_H
#define TRAYCON_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct traycon traycon;

/* Called when the tray icon is left-clicked. */
typedef void (*traycon_click_cb)(traycon *tray, void *userdata);

/* Called when a context-menu item is selected. */
typedef void (*traycon_menu_cb)(traycon *tray, int item_id, void *userdata);

/*
 * Called when the user interacts with a desktop notification.
 *
 * action_id - the id string of the action/button that was clicked, or
 *             "default" if the notification body itself was clicked.
 */
typedef void (*traycon_notification_cb)(traycon *tray,
                                        const char *action_id,
                                        void *userdata);

/* Describes one button in a desktop notification. */
typedef struct traycon_notification_action {
    const char *id;      /* action identifier, returned in the callback */
    const char *label;   /* button text displayed to the user           */
} traycon_notification_action;

/* Menu item flags. */
#define TRAYCON_MENU_DISABLED   (1 << 0)  /* item is grayed out       */
#define TRAYCON_MENU_CHECKED    (1 << 1)  /* item shows a check mark  */

/*
 * Describes one entry in a context menu.
 * Set label to NULL for a horizontal separator line.
 */
typedef struct traycon_menu_item {
    const char *label;   /* display text; NULL = separator          */
    int         id;      /* user-defined ID, passed back in the cb  */
    int         flags;   /* combination of TRAYCON_MENU_* flags     */
} traycon_menu_item;

/*
 * Linux backend selection (no-op on macOS / Windows).
 *
 * TRAYCON_BACKEND_AUTO – try SNI (D-Bus), fall back to X11  (default)
 * TRAYCON_BACKEND_SNI  – StatusNotifierItem via D-Bus (Wayland / KDE)
 * TRAYCON_BACKEND_X11  – XEmbed system tray protocol  (X11)
 *
 * Call traycon_set_preferred_backend() before traycon_create().
 */
#define TRAYCON_BACKEND_AUTO  0
#define TRAYCON_BACKEND_SNI   1
#define TRAYCON_BACKEND_X11   2

void traycon_set_preferred_backend(int backend);

/*
 * Create a tray icon from raw RGBA pixel data.
 *
 * rgba     - pointer to width * height * 4 bytes (R, G, B, A per pixel,
 *            row-major, top-left origin)
 * width    - icon width in pixels  (recommended: 32 or 48)
 * height   - icon height in pixels
 * cb       - called on left-click (may be NULL)
 * userdata - forwarded to cb
 *
 * Returns NULL on failure.
 */
traycon *traycon_create(const unsigned char *rgba, int width, int height,
                        traycon_click_cb cb, void *userdata);

/*
 * Replace the icon image.  Same format as traycon_create().
 * Returns 0 on success, -1 on failure.
 */
int traycon_update_icon(traycon *tray, const unsigned char *rgba,
                        int width, int height);

/*
 * Process pending events (non-blocking).
 * Call this regularly from your main loop.
 * Returns 0 normally, -1 if the tray is no longer functional.
 */
int traycon_step(traycon *tray);

/*
 * Remove the tray icon and free all resources.
 * Passing NULL is safe.
 */
void traycon_destroy(traycon *tray);

/*
 * Show or hide the tray icon.
 * visible: non-zero to show, zero to hide.
 * Returns 0 on success, -1 on failure.
 */
int traycon_set_visible(traycon *tray, int visible);

/*
 * Set the right-click context menu for the tray icon.
 *
 * items    - array of menu items (copied internally; caller may free)
 * count    - number of items in the array (0 to remove the menu)
 * cb       - called when an item is selected (may be NULL)
 * userdata - forwarded to cb
 *
 * Pass items=NULL and count=0 to remove the menu.
 * Returns 0 on success, -1 on failure.
 */
int traycon_set_menu(traycon *tray, const traycon_menu_item *items,
                     int count, traycon_menu_cb cb, void *userdata);

/*
 * Show a desktop notification with optional action buttons.
 *
 * title    - notification title (required, UTF-8)
 * body     - notification body text (may be NULL, UTF-8)
 * actions  - array of action buttons (may be NULL; copied internally)
 * count    - number of actions (0 for no buttons)
 * cb       - called when an action button is clicked (may be NULL)
 * userdata - forwarded to cb
 *
 * Only one notification at a time is tracked per tray icon.
 * Calling again replaces the pending notification's callback state.
 *
 * Platform notes:
 *   Linux/BSD – uses org.freedesktop.Notifications (D-Bus); actions
 *               appear as buttons if the notification server supports
 *               them.  Requires libdbus-1; disabled when compiled with
 *               TRAYCON_NO_SNI unless D-Bus is otherwise available.
 *   macOS     – uses UNUserNotificationCenter (10.14+); actions appear
 *               as notification buttons.  The first call may trigger a
 *               system authorisation dialog.
 *   Windows   – uses Shell_NotifyIcon balloon tips; action buttons are
 *               NOT supported. The callback fires with "default" when
 *               the user clicks the balloon.
 *
 * Returns 0 on success, -1 on failure.
 */
int traycon_notify(traycon *tray, const char *title, const char *body,
                   const traycon_notification_action *actions, int count,
                   traycon_notification_cb cb, void *userdata);

/*
 * Dismiss any currently showing notification.
 * Returns 0 on success, -1 on failure or if no notification is active.
 */
int traycon_dismiss_notification(traycon *tray);

#ifdef __cplusplus
}
#endif

#endif /* TRAYCON_H */
