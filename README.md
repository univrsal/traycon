# traycon

Minimal C library for creating system tray icons on **Linux**, **macOS**, and **Windows**.

| Platform              | Backend                                    |
| --------------------- | ------------------------------------------ |
| Linux (Wayland / KDE) | StatusNotifierItem via D-Bus (`libdbus-1`) |
| macOS                 | `NSStatusBar` / `NSStatusItem` (AppKit)    |
| Windows               | `Shell_NotifyIconW` (Win32)                |

## Usage

`traycon` is distributed as a **single-file header** in the style of the
[stb libraries](https://github.com/nothings/stb). Drop `traycon.h` into your
project and include it wherever you need the API. In **exactly one** C/C++
translation unit, define `TRAYCON_IMPLEMENTATION` before the include to pull
in the implementation:

```c
#define TRAYCON_IMPLEMENTATION
#include "traycon.h"
```

All other files just include the header without the define:

```c
#include "traycon.h"
```

### macOS note

The AppKit backend is written in Objective-C. The translation unit that
defines `TRAYCON_IMPLEMENTATION` must therefore be compiled as Objective-C —
either give it a `.m` extension or pass `-x objective-c` to the compiler.
Link with `-framework Cocoa`.

## API

```c
// Create a tray icon from raw RGBA pixel data.
// Returns NULL on failure.
traycon *traycon_create(const unsigned char *rgba, int width, int height,
                        traycon_click_cb cb, void *userdata);

// Replace the icon image.  Returns 0 on success, -1 on failure.
int traycon_update_icon(traycon *tray, const unsigned char *rgba,
                        int width, int height);

// Process pending events (non-blocking). Call from your main loop.
// Returns 0 normally, -1 if the tray is no longer functional.
int traycon_step(traycon *tray);

// Remove the tray icon and free all resources.  Passing NULL is safe.
void traycon_destroy(traycon *tray);
```

The icon data is raw **RGBA**, row-major, top-left origin. Recommended
sizes are 32×32 or 48×48.

## Example

```c
#define TRAYCON_IMPLEMENTATION
#include "traycon.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#  include <windows.h>
#  define SLEEP_MS(ms) Sleep(ms)
#else
#  include <unistd.h>
#  define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

static void on_click(traycon *tray, void *userdata)
{
    (void)tray; (void)userdata;
    printf("clicked!\n");
}

int main(void)
{
    int w = 32, h = 32;
    unsigned char *px = calloc((size_t)w * h, 4);
    if (!px) return 1;

    /* fill with a solid blue circle */
    float r = w / 2.0f;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float dx = x - r + 0.5f, dy = y - r + 0.5f;
            if (dx*dx + dy*dy <= r*r) {
                unsigned char *p = px + (y*w + x)*4;
                p[0] = 64; p[1] = 160; p[2] = 255; p[3] = 255;
            }
        }

    traycon *tray = traycon_create(px, w, h, on_click, NULL);
    free(px);
    if (!tray) { fprintf(stderr, "failed\n"); return 1; }

    printf("Click the tray icon.  Ctrl-C to quit.\n");
    for (;;) {
        if (traycon_step(tray) < 0) break;
        SLEEP_MS(50);
    }

    traycon_destroy(tray);
    return 0;
}
```

## Building

The included `Makefile` builds the bundled example:

```sh
make          # compiles example
make bundle   # regenerates traycon.h from src/ via bundle.py
make clean    # removes example and traycon.h
```

Platform dependencies:

| Platform | Requirement                  |
| -------- | ---------------------------- |
| Linux    | `libdbus-1` (`pkg-config`)   |
| macOS    | `-framework Cocoa` (default) |
| Windows  | `shell32`, `user32`, `gdi32` |
