/*
 * traycon – Linux implementation
 *
 * Implements the StatusNotifierItem D-Bus protocol used by KDE Plasma
 * (and other DEs with an SNI-compatible system tray) to display a tray
 * icon and receive click events.
 *
 * Depends on libdbus-1.
 */

#include "traycon.h"

#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/*  Internal data                                                      */
/* ------------------------------------------------------------------ */

struct traycon {
    DBusConnection  *conn;
    traycon_click_cb cb;
    void            *userdata;

    unsigned char   *icon_argb;   /* ARGB32 network-byte-order pixels  */
    int              icon_w;
    int              icon_h;
    int              icon_len;    /* icon_w * icon_h * 4               */

    char             bus_name[128];
};

static int traycon_id_counter = 0;

/* ------------------------------------------------------------------ */
/*  RGBA → ARGB-network-byte-order conversion                         */
/* ------------------------------------------------------------------ */

static unsigned char *rgba_to_argb_nbo(const unsigned char *rgba,
                                       int w, int h)
{
    int n = w * h;
    unsigned char *out = (unsigned char *)malloc((size_t)n * 4);
    if (!out) return NULL;
    for (int i = 0; i < n; i++) {
        out[i * 4 + 0] = rgba[i * 4 + 3]; /* A */
        out[i * 4 + 1] = rgba[i * 4 + 0]; /* R */
        out[i * 4 + 2] = rgba[i * 4 + 1]; /* G */
        out[i * 4 + 3] = rgba[i * 4 + 2]; /* B */
    }
    return out;
}

/* ------------------------------------------------------------------ */
/*  Introspection XML                                                  */
/* ------------------------------------------------------------------ */

static const char INTROSPECT_XML[] =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object "
    "Introspection 1.0//EN\"\n"
    "  \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    "  <interface name=\"org.kde.StatusNotifierItem\">\n"
    "    <method name=\"Activate\">\n"
    "      <arg name=\"x\" type=\"i\" direction=\"in\"/>\n"
    "      <arg name=\"y\" type=\"i\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"SecondaryActivate\">\n"
    "      <arg name=\"x\" type=\"i\" direction=\"in\"/>\n"
    "      <arg name=\"y\" type=\"i\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"ContextMenu\">\n"
    "      <arg name=\"x\" type=\"i\" direction=\"in\"/>\n"
    "      <arg name=\"y\" type=\"i\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"Scroll\">\n"
    "      <arg name=\"delta\" type=\"i\" direction=\"in\"/>\n"
    "      <arg name=\"orientation\" type=\"s\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <signal name=\"NewIcon\"/>\n"
    "    <signal name=\"NewTitle\"/>\n"
    "    <signal name=\"NewStatus\">\n"
    "      <arg type=\"s\"/>\n"
    "    </signal>\n"
    "    <property name=\"Category\"           type=\"s\"          access=\"read\"/>\n"
    "    <property name=\"Id\"                 type=\"s\"          access=\"read\"/>\n"
    "    <property name=\"Title\"              type=\"s\"          access=\"read\"/>\n"
    "    <property name=\"Status\"             type=\"s\"          access=\"read\"/>\n"
    "    <property name=\"WindowId\"           type=\"u\"          access=\"read\"/>\n"
    "    <property name=\"ItemIsMenu\"         type=\"b\"          access=\"read\"/>\n"
    "    <property name=\"IconName\"           type=\"s\"          access=\"read\"/>\n"
    "    <property name=\"IconPixmap\"         type=\"a(iiay)\"    access=\"read\"/>\n"
    "    <property name=\"OverlayIconName\"    type=\"s\"          access=\"read\"/>\n"
    "    <property name=\"OverlayIconPixmap\"  type=\"a(iiay)\"    access=\"read\"/>\n"
    "    <property name=\"AttentionIconName\"  type=\"s\"          access=\"read\"/>\n"
    "    <property name=\"AttentionIconPixmap\" type=\"a(iiay)\"   access=\"read\"/>\n"
    "    <property name=\"AttentionMovieName\" type=\"s\"          access=\"read\"/>\n"
    "    <property name=\"ToolTip\"            type=\"(sa(iiay)ss)\" access=\"read\"/>\n"
    "    <property name=\"IconThemePath\"      type=\"s\"          access=\"read\"/>\n"
    "    <property name=\"Menu\"               type=\"o\"          access=\"read\"/>\n"
    "  </interface>\n"
    "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
    "    <method name=\"Get\">\n"
    "      <arg name=\"interface\" type=\"s\" direction=\"in\"/>\n"
    "      <arg name=\"property\"  type=\"s\" direction=\"in\"/>\n"
    "      <arg name=\"value\"     type=\"v\" direction=\"out\"/>\n"
    "    </method>\n"
    "    <method name=\"GetAll\">\n"
    "      <arg name=\"interface\"  type=\"s\"    direction=\"in\"/>\n"
    "      <arg name=\"properties\" type=\"a{sv}\" direction=\"out\"/>\n"
    "    </method>\n"
    "  </interface>\n"
    "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
    "    <method name=\"Introspect\">\n"
    "      <arg name=\"data\" type=\"s\" direction=\"out\"/>\n"
    "    </method>\n"
    "  </interface>\n"
    "</node>\n";

/* ------------------------------------------------------------------ */
/*  D-Bus variant helpers                                              */
/* ------------------------------------------------------------------ */

static void var_string(DBusMessageIter *parent, const char *val)
{
    DBusMessageIter v;
    dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "s", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &val);
    dbus_message_iter_close_container(parent, &v);
}

static void var_uint32(DBusMessageIter *parent, dbus_uint32_t val)
{
    DBusMessageIter v;
    dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "u", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_UINT32, &val);
    dbus_message_iter_close_container(parent, &v);
}

static void var_bool(DBusMessageIter *parent, dbus_bool_t val)
{
    DBusMessageIter v;
    dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "b", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &val);
    dbus_message_iter_close_container(parent, &v);
}

static void var_objectpath(DBusMessageIter *parent, const char *val)
{
    DBusMessageIter v;
    dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "o", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_OBJECT_PATH, &val);
    dbus_message_iter_close_container(parent, &v);
}

/* Write variant a(iiay).  If data is NULL an empty array is written. */
static void var_icon_pixmap(DBusMessageIter *parent,
                            const unsigned char *argb, int w, int h)
{
    DBusMessageIter v, arr;
    dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT,
                                     "a(iiay)", &v);
    dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY,
                                     "(iiay)", &arr);
    if (argb && w > 0 && h > 0) {
        DBusMessageIter st, da;
        int len = w * h * 4;
        dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &st);
        dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &w);
        dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &h);
        dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "y", &da);
        dbus_message_iter_append_fixed_array(&da, DBUS_TYPE_BYTE, &argb, len);
        dbus_message_iter_close_container(&st, &da);
        dbus_message_iter_close_container(&arr, &st);
    }
    dbus_message_iter_close_container(&v, &arr);
    dbus_message_iter_close_container(parent, &v);
}

/* Write variant (sa(iiay)ss) — empty tooltip. */
static void var_tooltip(DBusMessageIter *parent)
{
    DBusMessageIter v, st, arr;
    const char *empty = "";
    dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT,
                                     "(sa(iiay)ss)", &v);
    dbus_message_iter_open_container(&v, DBUS_TYPE_STRUCT, NULL, &st);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &empty);
    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "(iiay)", &arr);
    dbus_message_iter_close_container(&st, &arr);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &empty);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &empty);
    dbus_message_iter_close_container(&v, &st);
    dbus_message_iter_close_container(parent, &v);
}

/* ------------------------------------------------------------------ */
/*  Property dispatch                                                  */
/* ------------------------------------------------------------------ */

/* Append a single property as a variant to *iter.  Returns 0 on
   success, -1 if the property is unknown.                            */
static int append_property(DBusMessageIter *iter, const char *prop,
                           traycon *tray)
{
    if      (!strcmp(prop, "Category"))            var_string(iter, "ApplicationStatus");
    else if (!strcmp(prop, "Id"))                  var_string(iter, "traycon");
    else if (!strcmp(prop, "Title"))               var_string(iter, "traycon");
    else if (!strcmp(prop, "Status"))              var_string(iter, "Active");
    else if (!strcmp(prop, "WindowId"))            var_uint32(iter, 0);
    else if (!strcmp(prop, "IconThemePath"))       var_string(iter, "");
    else if (!strcmp(prop, "IconName"))            var_string(iter, "");
    else if (!strcmp(prop, "IconPixmap"))          var_icon_pixmap(iter, tray->icon_argb,
                                                                   tray->icon_w,
                                                                   tray->icon_h);
    else if (!strcmp(prop, "OverlayIconName"))     var_string(iter, "");
    else if (!strcmp(prop, "OverlayIconPixmap"))   var_icon_pixmap(iter, NULL, 0, 0);
    else if (!strcmp(prop, "AttentionIconName"))   var_string(iter, "");
    else if (!strcmp(prop, "AttentionIconPixmap")) var_icon_pixmap(iter, NULL, 0, 0);
    else if (!strcmp(prop, "AttentionMovieName"))  var_string(iter, "");
    else if (!strcmp(prop, "ToolTip"))             var_tooltip(iter);
    else if (!strcmp(prop, "ItemIsMenu"))          var_bool(iter, FALSE);
    else if (!strcmp(prop, "Menu"))                var_objectpath(iter, "/NO_DBUSMENU");
    else return -1;
    return 0;
}

static const char *ALL_PROPS[] = {
    "Category", "Id", "Title", "Status", "WindowId",
    "IconThemePath", "IconName", "IconPixmap",
    "OverlayIconName", "OverlayIconPixmap",
    "AttentionIconName", "AttentionIconPixmap", "AttentionMovieName",
    "ToolTip", "ItemIsMenu", "Menu",
    NULL
};

/* ------------------------------------------------------------------ */
/*  D-Bus message handler for /StatusNotifierItem                      */
/* ------------------------------------------------------------------ */

static DBusHandlerResult
handle_message(DBusConnection *conn, DBusMessage *msg, void *data)
{
    traycon *tray = (traycon *)data;

    /* --- Introspect ------------------------------------------------ */
    if (dbus_message_is_method_call(msg,
            "org.freedesktop.DBus.Introspectable", "Introspect")) {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        const char *xml = INTROSPECT_XML;
        dbus_message_append_args(reply,
            DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    /* --- Properties.Get -------------------------------------------- */
    if (dbus_message_is_method_call(msg,
            "org.freedesktop.DBus.Properties", "Get")) {
        const char *iface = NULL, *prop = NULL;
        if (!dbus_message_get_args(msg, NULL,
                DBUS_TYPE_STRING, &iface,
                DBUS_TYPE_STRING, &prop,
                DBUS_TYPE_INVALID))
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        DBusMessage *reply = dbus_message_new_method_return(msg);
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        if (append_property(&iter, prop, tray) < 0) {
            dbus_message_unref(reply);
            reply = dbus_message_new_error_printf(msg,
                DBUS_ERROR_UNKNOWN_PROPERTY,
                "Unknown property: %s", prop);
        }
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    /* --- Properties.GetAll ----------------------------------------- */
    if (dbus_message_is_method_call(msg,
            "org.freedesktop.DBus.Properties", "GetAll")) {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        DBusMessageIter iter, dict;
        dbus_message_iter_init_append(reply, &iter);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                                         "{sv}", &dict);
        for (int i = 0; ALL_PROPS[i]; i++) {
            DBusMessageIter entry;
            dbus_message_iter_open_container(&dict,
                DBUS_TYPE_DICT_ENTRY, NULL, &entry);
            dbus_message_iter_append_basic(&entry,
                DBUS_TYPE_STRING, &ALL_PROPS[i]);
            append_property(&entry, ALL_PROPS[i], tray);
            dbus_message_iter_close_container(&dict, &entry);
        }
        dbus_message_iter_close_container(&iter, &dict);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    /* --- Activate (left click) ------------------------------------- */
    if (dbus_message_is_method_call(msg,
            "org.kde.StatusNotifierItem", "Activate") ||
        dbus_message_is_method_call(msg,
            "org.freedesktop.StatusNotifierItem", "Activate")) {
        if (tray->cb) tray->cb(tray, tray->userdata);
        DBusMessage *reply = dbus_message_new_method_return(msg);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    /* --- Other SNI methods (no-ops) -------------------------------- */
    {
        const char *iface = dbus_message_get_interface(msg);
        if (iface &&
            (!strcmp(iface, "org.kde.StatusNotifierItem") ||
             !strcmp(iface, "org.freedesktop.StatusNotifierItem"))) {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            dbus_connection_send(conn, reply, NULL);
            dbus_message_unref(reply);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

traycon *traycon_create(const unsigned char *rgba, int width, int height,
                        traycon_click_cb cb, void *userdata)
{
    if (!rgba || width <= 0 || height <= 0)
        return NULL;

    traycon *tray = (traycon *)calloc(1, sizeof *tray);
    if (!tray) return NULL;

    tray->cb       = cb;
    tray->userdata = userdata;

    /* Convert icon -------------------------------------------------- */
    tray->icon_argb = rgba_to_argb_nbo(rgba, width, height);
    if (!tray->icon_argb) { free(tray); return NULL; }
    tray->icon_w   = width;
    tray->icon_h   = height;
    tray->icon_len = width * height * 4;

    /* Connect to the session bus ------------------------------------ */
    DBusError err;
    dbus_error_init(&err);
    tray->conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "traycon: D-Bus connection failed: %s\n", err.message);
        dbus_error_free(&err);
        free(tray->icon_argb);
        free(tray);
        return NULL;
    }

    /* Request a well-known name ------------------------------------- */
    int id = ++traycon_id_counter;
    snprintf(tray->bus_name, sizeof tray->bus_name,
             "org.kde.StatusNotifierItem-%d-%d", (int)getpid(), id);

    int ret = dbus_bus_request_name(tray->conn, tray->bus_name,
                                    DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (dbus_error_is_set(&err) || ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "traycon: could not acquire bus name %s: %s\n",
                tray->bus_name,
                dbus_error_is_set(&err) ? err.message : "name taken");
        dbus_error_free(&err);
        free(tray->icon_argb);
        free(tray);
        return NULL;
    }

    /* Register object path ------------------------------------------ */
    static const DBusObjectPathVTable vtable = {
        .unregister_function = NULL,
        .message_function    = handle_message,
    };
    if (!dbus_connection_register_object_path(tray->conn,
            "/StatusNotifierItem", &vtable, tray)) {
        fprintf(stderr, "traycon: failed to register object path\n");
        free(tray->icon_argb);
        free(tray);
        return NULL;
    }

    /* Register with StatusNotifierWatcher --------------------------- */
    DBusMessage *reg = dbus_message_new_method_call(
        "org.kde.StatusNotifierWatcher",
        "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher",
        "RegisterStatusNotifierItem");
    const char *svc = tray->bus_name;
    dbus_message_append_args(reg,
        DBUS_TYPE_STRING, &svc, DBUS_TYPE_INVALID);

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        tray->conn, reg, 5000, &err);
    dbus_message_unref(reg);

    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "traycon: watcher registration failed: %s\n",
                err.message);
        dbus_error_free(&err);
        /* Non-fatal: the icon may still appear if a host is listening. */
    }
    if (reply) dbus_message_unref(reply);

    return tray;
}

int traycon_update_icon(traycon *tray, const unsigned char *rgba,
                        int width, int height)
{
    if (!tray || !rgba || width <= 0 || height <= 0)
        return -1;

    unsigned char *new_argb = rgba_to_argb_nbo(rgba, width, height);
    if (!new_argb) return -1;

    free(tray->icon_argb);
    tray->icon_argb = new_argb;
    tray->icon_w    = width;
    tray->icon_h    = height;
    tray->icon_len  = width * height * 4;

    /* Tell the tray host to re-read the icon */
    DBusMessage *sig = dbus_message_new_signal(
        "/StatusNotifierItem",
        "org.kde.StatusNotifierItem",
        "NewIcon");
    if (sig) {
        dbus_connection_send(tray->conn, sig, NULL);
        dbus_message_unref(sig);
        dbus_connection_flush(tray->conn);
    }
    return 0;
}

int traycon_step(traycon *tray)
{
    if (!tray || !tray->conn) return -1;

    /* Non-blocking read/write */
    if (!dbus_connection_read_write(tray->conn, 0))
        return -1;                    /* disconnected */

    /* Dispatch all queued incoming messages */
    while (dbus_connection_dispatch(tray->conn) ==
           DBUS_DISPATCH_DATA_REMAINS)
        ;

    return 0;
}

void traycon_destroy(traycon *tray)
{
    if (!tray) return;

    if (tray->conn) {
        dbus_connection_unregister_object_path(tray->conn,
                                               "/StatusNotifierItem");
        /* Release bus name – the tray host will notice the name vanished. */
        DBusError err;
        dbus_error_init(&err);
        dbus_bus_release_name(tray->conn, tray->bus_name, &err);
        dbus_error_free(&err);
        dbus_connection_flush(tray->conn);
        dbus_connection_unref(tray->conn);
    }
    free(tray->icon_argb);
    free(tray);
}
