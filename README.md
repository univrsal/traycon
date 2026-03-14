# traycon

Minimal C library for creating system tray icons on **Linux** and **Windows**.

| Platform              | Backend                                    |
| --------------------- | ------------------------------------------ |
| Linux (Wayland / KDE) | StatusNotifierItem via D-Bus (`libdbus-1`) |
| Windows               | `Shell_NotifyIconW` (Win32)                |

## Example

```c
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
}

int main(void)
{
    int w = 32, h = 32;
    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
    if (!px) return 1;

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

```

The icon data is raw **RGBA**, row-major, top-left origin. Recommended
sizes are 32×32 or 48×48.
