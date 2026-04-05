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

#import <Cocoa/Cocoa.h>
#import <UserNotifications/UserNotifications.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal menu helpers                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char *label;   /* heap copy; NULL = separator */
    int   id;
    int   flags;
} traycon__menu_entry;

static traycon__menu_entry *traycon__copy_menu(const traycon_menu_item *items,
                                               int count)
{
    if (count <= 0 || !items) return NULL;
    traycon__menu_entry *e = (traycon__menu_entry *)calloc(count, sizeof *e);
    if (!e) return NULL;
    for (int i = 0; i < count; i++) {
        e[i].id    = items[i].id;
        e[i].flags = items[i].flags;
        e[i].label = items[i].label ? strdup(items[i].label) : NULL;
    }
    return e;
}

static void traycon__free_menu(traycon__menu_entry *e, int count)
{
    if (!e) return;
    for (int i = 0; i < count; i++) free(e[i].label);
    free(e);
}

/* ------------------------------------------------------------------ */
/*  Internal data                                                      */
/* ------------------------------------------------------------------ */

@interface TrayconClickHandler : NSObject <UNUserNotificationCenterDelegate>
@property (nonatomic, assign) traycon *tray_ptr;
- (void)handleClick:(id)sender;
- (void)handleMenuItem:(id)sender;
@end

struct traycon {
    NSStatusItem        *item;     /* retained by NSStatusBar */
    TrayconClickHandler *handler;
    traycon_click_cb     cb;
    void                *userdata;

    /* context menu */
    NSMenu              *ns_menu;
    traycon__menu_entry *menu_items;
    int                  menu_count;
    traycon_menu_cb      menu_cb;
    void                *menu_userdata;

    /* notification */
    traycon_notification_cb  notify_cb;
    void                    *notify_userdata;
    int                      notify_auth_requested;
};

/* ------------------------------------------------------------------ */
/*  Click handler                                                       */
/* ------------------------------------------------------------------ */

@implementation TrayconClickHandler
- (void)handleClick:(id)sender
{
    (void)sender;
    traycon *t = self.tray_ptr;
    if (!t) return;

    NSEvent *event = [NSApp currentEvent];
    if (event.type == NSEventTypeRightMouseUp && t->ns_menu) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [t->item popUpStatusItemMenu:t->ns_menu];
#pragma clang diagnostic pop
        return;
    }

    if (t->cb) t->cb(t, t->userdata);
}

- (void)handleMenuItem:(id)sender
{
    NSMenuItem *mi = (NSMenuItem *)sender;
    int index = (int)[mi tag];
    traycon *t = self.tray_ptr;
    if (t && t->menu_cb && index >= 0 && index < t->menu_count)
        t->menu_cb(t, t->menu_items[index].id, t->menu_userdata);
}

/* ---- UNUserNotificationCenterDelegate ---- */

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       didReceiveNotificationResponse:(UNNotificationResponse *)response
                 withCompletionHandler:(void (^)(void))completionHandler
    API_AVAILABLE(macos(10.14))
{
    (void)center;
    traycon *t = self.tray_ptr;
    if (t && t->notify_cb) {
        NSString *actionId = response.actionIdentifier;
        if ([actionId isEqualToString:UNNotificationDefaultActionIdentifier]) {
            t->notify_cb(t, "default", t->notify_userdata);
        } else if (![actionId isEqualToString:UNNotificationDismissActionIdentifier]) {
            t->notify_cb(t, [actionId UTF8String], t->notify_userdata);
        }
    }
    completionHandler();
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler
    API_AVAILABLE(macos(10.14))
{
    (void)center;
    (void)notification;
    /* Show the notification even when the app is in the foreground. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    completionHandler(UNNotificationPresentationOptionAlert |
                      UNNotificationPresentationOptionSound);
#pragma clang diagnostic pop
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
    [tray->item.button sendActionOn:(NSEventMaskLeftMouseUp |
                                     NSEventMaskRightMouseUp)];

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
    /* Remove notification delegate */
    if (@available(macOS 10.14, *)) {
        UNUserNotificationCenter *center =
            [UNUserNotificationCenter currentNotificationCenter];
        if (center.delegate == (id)tray->handler)
            center.delegate = nil;
        [center removeDeliveredNotificationsWithIdentifiers:
            @[@"traycon_notification"]];
    }
    if (tray->item)
        [[NSStatusBar systemStatusBar] removeStatusItem:tray->item];
    tray->item    = nil;
    tray->handler = nil;
    tray->ns_menu = nil;
    traycon__free_menu(tray->menu_items, tray->menu_count);
    free(tray);
}

int traycon_set_visible(traycon *tray, int visible)
{
    if (!tray || !tray->item) return -1;
    tray->item.visible = visible ? YES : NO;
    return 0;
}

int traycon_set_menu(traycon *tray, const traycon_menu_item *items,
                     int count, traycon_menu_cb cb, void *userdata)
{
    if (!tray) return -1;

    traycon__free_menu(tray->menu_items, tray->menu_count);
    tray->menu_items    = traycon__copy_menu(items, count);
    tray->menu_count    = (items && count > 0) ? count : 0;
    tray->menu_cb       = cb;
    tray->menu_userdata = userdata;

    /* (Re)build the NSMenu */
    tray->ns_menu = nil;
    if (tray->menu_count > 0) {
        NSMenu *menu = [[NSMenu alloc] init];
        [menu setAutoenablesItems:NO];
        for (int i = 0; i < tray->menu_count; i++) {
            traycon__menu_entry *e = &tray->menu_items[i];
            if (!e->label) {
                [menu addItem:[NSMenuItem separatorItem]];
            } else {
                NSString *title = [NSString stringWithUTF8String:e->label];
                NSMenuItem *mi = [[NSMenuItem alloc]
                    initWithTitle:title
                           action:@selector(handleMenuItem:)
                    keyEquivalent:@""];
                [mi setTarget:tray->handler];
                [mi setTag:i];
                [mi setEnabled:!(e->flags & TRAYCON_MENU_DISABLED)];
                if (e->flags & TRAYCON_MENU_CHECKED)
                    [mi setState:NSControlStateValueOn];
                [menu addItem:mi];
            }
        }
        tray->ns_menu = menu;
    }
    return 0;
}

void traycon_set_preferred_backend(int backend) { (void)backend; }

int traycon_notify(traycon *tray, const char *title, const char *body,
                   const traycon_notification_action *actions, int count,
                   traycon_notification_cb cb, void *userdata)
{
    if (!tray || !title) return -1;

    if (@available(macOS 10.14, *)) {
        UNUserNotificationCenter *center =
            [UNUserNotificationCenter currentNotificationCenter];
        center.delegate = tray->handler;

        /* Request authorisation on first call (non-blocking).
         * The system shows a dialog; the first notification may be
         * silently dropped if the user hasn't responded yet. */
        if (!tray->notify_auth_requested) {
            tray->notify_auth_requested = 1;
            [center requestAuthorizationWithOptions:
                (UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                                  completionHandler:^(BOOL granted,
                                                      NSError *error) {
                (void)granted; (void)error;
            }];
        }

        /* Store callback */
        tray->notify_cb       = cb;
        tray->notify_userdata = userdata;

        /* Build content */
        UNMutableNotificationContent *content =
            [[UNMutableNotificationContent alloc] init];
        content.title = [NSString stringWithUTF8String:title];
        if (body)
            content.body = [NSString stringWithUTF8String:body];
        content.sound = [UNNotificationSound defaultSound];

        /* Build category with action buttons */
        NSString *categoryId =
            [NSString stringWithFormat:@"traycon_cat_%u", arc4random()];

        if (actions && count > 0) {
            NSMutableArray<UNNotificationAction *> *nsActions =
                [NSMutableArray array];
            for (int i = 0; i < count; i++) {
                NSString *aid =
                    [NSString stringWithUTF8String:
                        actions[i].id ? actions[i].id : ""];
                NSString *alabel =
                    [NSString stringWithUTF8String:
                        actions[i].label ? actions[i].label : ""];
                UNNotificationAction *action = [UNNotificationAction
                    actionWithIdentifier:aid
                                   title:alabel
                                 options:UNNotificationActionOptionForeground];
                [nsActions addObject:action];
            }

            UNNotificationCategory *category = [UNNotificationCategory
                categoryWithIdentifier:categoryId
                               actions:nsActions
                     intentIdentifiers:@[]
                               options:UNNotificationCategoryOptionNone];

            [center setNotificationCategories:
                [NSSet setWithObject:category]];
            content.categoryIdentifier = categoryId;
        }

        /* Create and submit the request */
        UNNotificationRequest *request = [UNNotificationRequest
            requestWithIdentifier:@"traycon_notification"
                          content:content
                          trigger:nil];   /* immediate delivery */

        __block BOOL success = NO;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        [center addNotificationRequest:request
                 withCompletionHandler:^(NSError *error) {
            success = (error == nil);
            if (error)
                fprintf(stderr, "traycon: notification error: %s\n",
                        [[error localizedDescription] UTF8String]);
            dispatch_semaphore_signal(sem);
        }];
        dispatch_semaphore_wait(sem,
            dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));

        return success ? 0 : -1;
    }

    /* macOS < 10.14: UNUserNotificationCenter not available */
    return -1;
}

int traycon_dismiss_notification(traycon *tray)
{
    if (!tray) return -1;

    if (@available(macOS 10.14, *)) {
        [[UNUserNotificationCenter currentNotificationCenter]
            removeDeliveredNotificationsWithIdentifiers:
                @[@"traycon_notification"]];
        return 0;
    }

    return -1;
}

