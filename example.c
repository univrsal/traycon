/*
 * traycon example – creates a blue circle tray icon and prints a
 * message each time it is clicked.
 */

#define _DEFAULT_SOURCE  /* usleep */

#define TRAYCON_IMPLEMENTATION
#include "traycon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#else
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

static int click_count = 0;

static void on_click(traycon *tray, void *userdata)
{
    (void)userdata;
    click_count++;
    printf("tray icon clicked (%d)\n", click_count);

    /* Swap colour between blue and green on each click */
    int w = 32, h = 32;
    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
    if (!px) return;
    float r = w / 2.0f;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i   = (y * w + x) * 4;
            float dx = x - r + 0.5f;
            float dy = y - r + 0.5f;
            if (dx * dx + dy * dy <= r * r) {
                if (click_count & 1) {
                    px[i+0] = 80; px[i+1] = 220; px[i+2] = 100; /* green */
                } else {
                    px[i+0] = 64; px[i+1] = 160; px[i+2] = 255; /* blue  */
                }
                px[i+3] = 255;
            } else {
                px[i+0] = px[i+1] = px[i+2] = px[i+3] = 0;
            }
        }
    }
    traycon_update_icon(tray, px, w, h);
    free(px);
}

int main(void)
{
    int w = 32, h = 32;
    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
    if (!px) return 1;

    /* Draw a blue circle with transparent background */
    float r = w / 2.0f;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i   = (y * w + x) * 4;
            float dx = x - r + 0.5f;
            float dy = y - r + 0.5f;
            if (dx * dx + dy * dy <= r * r) {
                px[i+0] = 64;  /* R */
                px[i+1] = 160; /* G */
                px[i+2] = 255; /* B */
                px[i+3] = 255; /* A */
            } else {
                px[i+0] = px[i+1] = px[i+2] = px[i+3] = 0;
            }
        }
    }

    traycon *tray = traycon_create(px, w, h, on_click, NULL);
    free(px);
    if (!tray) {
        fprintf(stderr, "Failed to create tray icon\n");
        return 1;
    }

    printf("Tray icon created.  Click it!  Press Ctrl-C to quit.\n");

    for (;;) {
        if (traycon_step(tray) < 0)
            break;
        SLEEP_MS(50);
    }

    traycon_destroy(tray);
    return 0;
}
