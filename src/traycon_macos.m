/*
 * traycon – macOS implementation
 *
 * Uses NSStatusBar / NSStatusItem (AppKit) to display a menu-bar icon
 * and receive left-click events.
 *
 * Depends on: -framework Cocoa
 *
 * The translation unit that defines TRAYCON_IMPLEMENTATION must be
 * compiled as Objective-C (i.e. have a .m extension, or be compiled
 * with -x objective-c).
 */

#include "traycon.h"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal data                                                      */
/* ------------------------------------------------------------------ */

@interface TrayconClickHandler : NSObject
@property (nonatomic, assign) traycon *tray_ptr;
- (void)handleClick:(id)sender;
@end

struct traycon {
    NSStatusItem        *item;     /* retained by NSStatusBar */
    TrayconClickHandler *handler;
    traycon_click_cb     cb;
    void                *userdata;
};

/* ------------------------------------------------------------------ */
/*  Click handler                                                       */
/* ------------------------------------------------------------------ */

@implementation TrayconClickHandler
- (void)handleClick:(id)sender
{
    (void)sender;
    traycon *t = self.tray_ptr;
    if (t && t->cb) t->cb(t, t->userdata);
}
@end

/* ------------------------------------------------------------------ */
/*  RGBA → NSImage conversion                                          */
/* ------------------------------------------------------------------ */

static NSImage *image_from_rgba(const unsigned char *rgba, int w, int h)
{
    NSBitmapImageRep *rep =
        [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:NULL
                          pixelsWide:w
                          pixelsHigh:h
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSDeviceRGBColorSpace
                         bytesPerRow:w * 4
                        bitsPerPixel:32];
    if (!rep) return nil;

    memcpy([rep bitmapData], rgba, (size_t)w * h * 4);

    NSImage *img = [[NSImage alloc] initWithSize:NSMakeSize(w, h)];
    [img addRepresentation:rep];
    return img;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

traycon *traycon_create(const unsigned char *rgba, int width, int height,
                        traycon_click_cb cb, void *userdata)
{
    if (!rgba || width <= 0 || height <= 0)
        return NULL;

    /* Ensure an NSApplication instance exists (needed for the status bar). */
    if (!NSApp) {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    }

    traycon *tray = (traycon *)calloc(1, sizeof *tray);
    if (!tray) return NULL;

    tray->cb       = cb;
    tray->userdata = userdata;

    NSStatusBar *bar = [NSStatusBar systemStatusBar];
    tray->item = [bar statusItemWithLength:NSSquareStatusItemLength];
    if (!tray->item) { free(tray); return NULL; }

    NSImage *img = image_from_rgba(rgba, width, height);
    if (!img) {
        [bar removeStatusItem:tray->item];
        free(tray);
        return NULL;
    }
    [img setTemplate:NO];
    CGFloat thickness = [bar thickness];
    [img setSize:NSMakeSize(thickness, thickness)];
    tray->item.button.image = img;

    tray->handler             = [[TrayconClickHandler alloc] init];
    tray->handler.tray_ptr    = tray;
    tray->item.button.target  = tray->handler;
    tray->item.button.action  = @selector(handleClick:);
    [tray->item.button sendActionOn:NSEventMaskLeftMouseUp];

    return tray;
}

int traycon_update_icon(traycon *tray, const unsigned char *rgba,
                        int width, int height)
{
    if (!tray || !rgba || width <= 0 || height <= 0)
        return -1;

    NSImage *img = image_from_rgba(rgba, width, height);
    if (!img) return -1;

    [img setTemplate:NO];
    CGFloat thickness = [[NSStatusBar systemStatusBar] thickness];
    [img setSize:NSMakeSize(thickness, thickness)];
    tray->item.button.image = img;
    return 0;
}

int traycon_step(traycon *tray)
{
    if (!tray) return -1;

    /* Drain pending AppKit events without blocking. */
    @autoreleasepool {
        NSEvent *ev;
        while ((ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:nil
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES]) != nil) {
            [NSApp sendEvent:ev];
        }
    }
    return 0;
}

void traycon_destroy(traycon *tray)
{
    if (!tray) return;
    if (tray->item)
        [[NSStatusBar systemStatusBar] removeStatusItem:tray->item];
    tray->item    = nil;
    tray->handler = nil;
    free(tray);
}

int traycon_set_visible(traycon *tray, int visible)
{
    if (!tray || !tray->item) return -1;
    tray->item.visible = visible ? YES : NO;
    return 0;
}

#endif /* __APPLE__ */
