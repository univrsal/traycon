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

#ifdef __cplusplus
}
#endif

#endif /* TRAYCON_H */
