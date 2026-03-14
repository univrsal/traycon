/*
 * traycon – Windows implementation
 *
 * Uses Shell_NotifyIconW and a message-only window to display a
 * system-tray icon and receive click notifications.
 */

#include "traycon.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>

#define WM_TRAYICON  (WM_APP + 1)
#define TRAY_ICON_ID 1

/* ------------------------------------------------------------------ */
/*  Internal data                                                      */
/* ------------------------------------------------------------------ */

struct traycon {
    HWND             hwnd;
    NOTIFYICONDATAW  nid;
    HICON            hicon;
    traycon_click_cb cb;
    void            *userdata;
};

static const wchar_t CLASS_NAME[] = L"traycon_wnd";
static int class_registered = 0;

/* ------------------------------------------------------------------ */
/*  Create HICON from RGBA pixel data                                  */
/* ------------------------------------------------------------------ */

static HICON icon_from_rgba(const unsigned char *rgba, int w, int h)
{
    BITMAPV5HEADER bi;
    memset(&bi, 0, sizeof bi);
    bi.bV5Size        = sizeof bi;
    bi.bV5Width       = w;
    bi.bV5Height      = -h;          /* top-down */
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    HDC hdc = GetDC(NULL);
    unsigned char *bits = NULL;
    HBITMAP hbmp = CreateDIBSection(hdc, (BITMAPINFO *)&bi,
                                    DIB_RGB_COLORS, (void **)&bits,
                                    NULL, 0);
    ReleaseDC(NULL, hdc);
    if (!hbmp) return NULL;

    /* RGBA → pre-multiplied BGRA */
    int n = w * h;
    for (int i = 0; i < n; i++) {
        unsigned char r = rgba[i * 4 + 0];
        unsigned char g = rgba[i * 4 + 1];
        unsigned char b = rgba[i * 4 + 2];
        unsigned char a = rgba[i * 4 + 3];
        bits[i * 4 + 0] = (unsigned char)((b * a + 127) / 255);
        bits[i * 4 + 1] = (unsigned char)((g * a + 127) / 255);
        bits[i * 4 + 2] = (unsigned char)((r * a + 127) / 255);
        bits[i * 4 + 3] = a;
    }

    HBITMAP hmask = CreateBitmap(w, h, 1, 1, NULL);
    ICONINFO ii;
    memset(&ii, 0, sizeof ii);
    ii.fIcon    = TRUE;
    ii.hbmMask  = hmask;
    ii.hbmColor = hbmp;

    HICON hicon = CreateIconIndirect(&ii);
    DeleteObject(hbmp);
    DeleteObject(hmask);
    return hicon;
}

/* ------------------------------------------------------------------ */
/*  Window procedure                                                   */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK
wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CREATE) {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          (LONG_PTR)cs->lpCreateParams);
        return 0;
    }

    traycon *tray = (traycon *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    if (msg == WM_TRAYICON && tray) {
        /* lParam = mouse message (uVersion 0) */
        if (lp == WM_LBUTTONUP) {
            if (tray->cb) tray->cb(tray, tray->userdata);
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

traycon *traycon_create(const unsigned char *rgba, int width, int height,
                        traycon_click_cb cb, void *userdata)
{
    if (!rgba || width <= 0 || height <= 0)
        return NULL;

    HINSTANCE hinst = GetModuleHandleW(NULL);

    /* Register window class once */
    if (!class_registered) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize        = sizeof wc;
        wc.lpfnWndProc   = wndproc;
        wc.hInstance      = hinst;
        wc.lpszClassName  = CLASS_NAME;
        if (!RegisterClassExW(&wc)) return NULL;
        class_registered = 1;
    }

    traycon *tray = (traycon *)calloc(1, sizeof *tray);
    if (!tray) return NULL;

    tray->cb       = cb;
    tray->userdata = userdata;

    /* Message-only window */
    tray->hwnd = CreateWindowExW(0, CLASS_NAME, L"traycon", 0,
                                  0, 0, 0, 0,
                                  HWND_MESSAGE, NULL, hinst, tray);
    if (!tray->hwnd) { free(tray); return NULL; }

    /* Icon */
    tray->hicon = icon_from_rgba(rgba, width, height);
    if (!tray->hicon) {
        DestroyWindow(tray->hwnd);
        free(tray);
        return NULL;
    }

    /* Shell notification */
    memset(&tray->nid, 0, sizeof tray->nid);
    tray->nid.cbSize           = sizeof tray->nid;
    tray->nid.hWnd             = tray->hwnd;
    tray->nid.uID              = TRAY_ICON_ID;
    tray->nid.uFlags           = NIF_ICON | NIF_MESSAGE;
    tray->nid.uCallbackMessage = WM_TRAYICON;
    tray->nid.hIcon            = tray->hicon;
    if (!Shell_NotifyIconW(NIM_ADD, &tray->nid)) {
        DestroyIcon(tray->hicon);
        DestroyWindow(tray->hwnd);
        free(tray);
        return NULL;
    }

    return tray;
}

int traycon_update_icon(traycon *tray, const unsigned char *rgba,
                        int width, int height)
{
    if (!tray || !rgba || width <= 0 || height <= 0)
        return -1;

    HICON newhicon = icon_from_rgba(rgba, width, height);
    if (!newhicon) return -1;

    DestroyIcon(tray->hicon);
    tray->hicon     = newhicon;
    tray->nid.hIcon = newhicon;
    tray->nid.uFlags = NIF_ICON;

    return Shell_NotifyIconW(NIM_MODIFY, &tray->nid) ? 0 : -1;
}

int traycon_step(traycon *tray)
{
    if (!tray) return -1;
    MSG msg;
    while (PeekMessageW(&msg, tray->hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

void traycon_destroy(traycon *tray)
{
    if (!tray) return;
    Shell_NotifyIconW(NIM_DELETE, &tray->nid);
    if (tray->hicon) DestroyIcon(tray->hicon);
    if (tray->hwnd)  DestroyWindow(tray->hwnd);
    free(tray);
}

#endif /* _WIN32 */
