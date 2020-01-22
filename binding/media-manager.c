/*
 *  Copyright 2017 Konsulko Group
 *
 *  Based on bluetooth-manager.c
 *   Copyright 2016 ALPS ELECTRIC CO., LTD.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <pthread.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <sqlite3.h>

#include "media-manager.h"

const gchar *lms_scan_types[] = {
    MEDIA_AUDIO,
    MEDIA_VIDEO,
    MEDIA_IMAGE
};
typedef struct {
    sqlite3 *db;
    gboolean is_open;
}scannerDB;

static Binding_RegisterCallback_t g_RegisterCallback = { 0 };
static stMediaPlayerManage MediaPlayerManage = { 0 };
static scannerDB scanDB = { 0 };

/* ------ LOCAL  FUNCTIONS --------- */
void ListLock() {
    g_mutex_lock(&(MediaPlayerManage.m));
}

void ListUnlock() {
    g_mutex_unlock(&(MediaPlayerManage.m));
}

void DebugTraceSendMsg(int level, gchar* message)
{
#ifdef LOCAL_PRINT_DEBUG
    switch (level)
    {
            case DT_LEVEL_ERROR:
                g_print("[E]");
                break;

            case DT_LEVEL_WARNING:
                g_print("[W]");
                break;

            case DT_LEVEL_NOTICE:
                g_print("[N]");
                break;

            case DT_LEVEL_INFO:
                g_print("[I]");
                break;

            case DT_LEVEL_DEBUG:
                g_print("[D]");
                break;

            default:
                g_print("[-]");
                break;
    }

    g_print("%s",message);
#endif

    if (message) {
        g_free(message);
    }

}

GList* media_lightmediascanner_scan(GList *list, gchar *uri, int scan_type)
{
    sqlite3 *conn;
    sqlite3_stmt *res;
    const char *tail;
    const gchar *db_path;
    gchar *query;
    gchar *media_type;
    int ret = 0;

    if(!scanDB.is_open) {
        db_path = scanner1_get_data_base_path(MediaPlayerManage.lms_proxy);

        ret = sqlite3_open(db_path, &scanDB.db);
        if (ret != SQLITE_OK) {
            LOGE("Cannot open SQLITE database: '%s'\n", db_path);
            scanDB.is_open = FALSE;
            g_list_free_full(list,free_media_item);
            return NULL;
        }
        scanDB.is_open = TRUE;
    }

    switch (scan_type) {
        case LMS_VIDEO_SCAN:
            query = g_strdup_printf(VIDEO_SQL_QUERY, uri ? uri : "");
            media_type = lms_scan_types[LMS_VIDEO_ID];
            break;
        case LMS_IMAGE_SCAN:
            query = g_strdup_printf(IMAGE_SQL_QUERY, uri ? uri : "");
            media_type = lms_scan_types[LMS_IMAGE_ID];
            break;
        case LMS_AUDIO_SCAN:
        default:
            query = g_strdup_printf(AUDIO_SQL_QUERY, uri ? uri : "");
            media_type = lms_scan_types[LMS_AUDIO_ID];
    }

    if (!query) {
        LOGE("Cannot allocate memory for query\n");
        return NULL;
    }

    ret = sqlite3_prepare_v2(scanDB.db, query, (int) strlen(query), &res, &tail);
    if (ret) {
        LOGE("Cannot execute query '%s'\n", query);
        g_free(query);
        return NULL;
    }

    while (sqlite3_step(res) == SQLITE_ROW) {
        struct stat buf;
        struct Media_Item *item;
        const char *path = (const char *) sqlite3_column_text(res, 0);
        gchar *tmp;

        ret = stat(path, &buf);
        if (ret)
            continue;

        //We may check the allocation result ... but It may be a bit expensive in such a loop
        item = g_malloc0(sizeof(*item));
        tmp = g_uri_escape_string(path, "/", TRUE);
        item->path = g_strdup_printf("file://%s", tmp);
        g_free(tmp);

        item->type = media_type;
        item->metadata.title = g_strdup((gchar *) sqlite3_column_text(res, 1));
        item->metadata.artist = g_strdup((gchar *) sqlite3_column_text(res, 2));
        item->metadata.album = g_strdup((gchar *) sqlite3_column_text(res, 3));
        item->metadata.genre = g_strdup((gchar *) sqlite3_column_text(res, 4));
        item->metadata.duration = sqlite3_column_int(res, 5) * 1000;
        list = g_list_append(list, item);
    }
    g_free(query);

    return list;
}

void free_media_item(void *data)
{
    struct Media_Item *item = data;

	g_free(item->metadata.title);
	g_free(item->metadata.artist);
	g_free(item->metadata.album);
	g_free(item->metadata.genre);
	g_free(item->path);
	g_free(item);
}

static void
on_interface_proxy_properties_changed (GDBusProxy *proxy,
                                    GVariant *changed_properties,
                                    const gchar* const  *invalidated_properties)
{
    if(!(MediaPlayerManage.type_filter & LMS_ALL_SCAN))
        return;

    GVariantIter iter;
    gchar *key = NULL;
    GVariant *subValue = NULL;
    gchar *pInterface = NULL;
    GList *list = NULL;
    gboolean br = FALSE;
    pInterface = g_dbus_proxy_get_interface_name (proxy);

    if (0 != g_strcmp0(pInterface, LIGHTMEDIASCANNER_INTERFACE))
        return;

    g_variant_iter_init (&iter, changed_properties);
    while (g_variant_iter_loop(&iter, "{&sv}", &key, &subValue))
    {
        gboolean val;
        if (0 == g_strcmp0(key,"IsScanning")) {
            g_variant_get(subValue, "b", &val);
            if (val == TRUE)
                br = TRUE;
        } else if (0 == g_strcmp0(key, "WriteLocked")) {
            g_variant_get(subValue, "b", &val);
            if (val == TRUE)
                br = TRUE;
        }
    }

    if(br)
        return;

    ListLock();
    if(MediaPlayerManage.type_filter & LMS_AUDIO_SCAN)
        list = media_lightmediascanner_scan(list, MediaPlayerManage.uri_filter, LMS_AUDIO_SCAN);
    if(MediaPlayerManage.type_filter & LMS_VIDEO_SCAN)
        list = media_lightmediascanner_scan(list, MediaPlayerManage.uri_filter, LMS_VIDEO_SCAN);
    if(MediaPlayerManage.type_filter & LMS_IMAGE_SCAN)
        list = media_lightmediascanner_scan(list, MediaPlayerManage.uri_filter, LMS_IMAGE_SCAN);

    g_free(MediaPlayerManage.uri_filter);
    MediaPlayerManage.uri_filter = NULL;

    if (list != NULL && g_RegisterCallback.binding_device_added)
        g_RegisterCallback.binding_device_added(list);

    g_list_free_full(list, free_media_item);

    ListUnlock();
}

static int MediaPlayerDBusInit(void)
{
    GError *error = NULL;

    MediaPlayerManage.lms_proxy = scanner1_proxy_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, LIGHTMEDIASCANNER_SERVICE,
        LIGHTMEDIASCANNER_PATH, NULL, &error);

    if (MediaPlayerManage.lms_proxy == NULL) {
        LOGE("Create LightMediaScanner Proxy failed\n");
        return -1;
    }

    return 0;
}

static void *media_event_loop_thread(void *unused)
{
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    if (loop == NULL)
        return NULL;

    g_signal_connect (MediaPlayerManage.lms_proxy,
                      "g-properties-changed",
                      G_CALLBACK (on_interface_proxy_properties_changed),
                      NULL);

    LOGD("g_main_loop_run\n");
    g_main_loop_run(loop);

    return NULL;
}

void
unmount_cb (GFileMonitor      *mon,
            GFile             *file,
            GFile             *other_file,
            GFileMonitorEvent  event,
            gpointer           udata)
{
    gchar *path = g_file_get_path(file);
    gchar *uri = g_strconcat("file://", path, NULL);
    gint ret = 0;

    ListLock();

    if (g_RegisterCallback.binding_device_removed &&
        event == G_FILE_MONITOR_EVENT_DELETED) {

        g_RegisterCallback.binding_device_removed(uri);
        ret = sqlite3_close(scanDB.db);
        /* TODO: Release SQLite connection handle resources on the end of each session
        *
        * we should be able to handle the SQLITE_BUSY return value safely
        * and be sure the connection handle will be released finally at the end of session
        * There are a few synchronous & asynchronous libsqlite methods
        * to handle this situation properly.
        */
        if(ret == SQLITE_OK) {
            scanDB.db = NULL;
            scanDB.is_open = FALSE;
        } else {
            LOGE("Failed to release SQLite connection handle.\n");
        }
        g_free(path);
    } else if (event == G_FILE_MONITOR_EVENT_CREATED) {
        MediaPlayerManage.uri_filter = path;
    } else {
        g_free(path);
    }

    g_free(uri);
    ListUnlock();
}

/*
 * Create MediaPlayer Manager Thread
 * Note: mediaplayer-api should do MediaPlayerManagerInit() before any other
 *       API calls
 * Returns: 0 - success or error conditions
 */
int MediaPlayerManagerInit() {
    pthread_t thread_id;
    GFile *file = NULL;
    GFileMonitor *mon = MediaPlayerManage.mon;
    int ret;

    g_mutex_init(&(MediaPlayerManage.m));
    scanDB.is_open = FALSE;
    if(mon != NULL) {
        g_object_unref(mon);
        mon = NULL;
    }

    file = g_file_new_for_path("/media");
    g_assert(file != NULL);

    mon = g_file_monitor (file, G_FILE_MONITOR_NONE, NULL, NULL);
    g_object_unref(file);
    g_assert(mon != NULL);
    g_signal_connect (mon, "changed", G_CALLBACK(unmount_cb), NULL);

    ret = MediaPlayerDBusInit();
    if (ret == 0)
        pthread_create(&thread_id, NULL, media_event_loop_thread, NULL);

    return ret;
}

/*
 * Register MediaPlayer Manager Callback functions
 */
void BindingAPIRegister(const Binding_RegisterCallback_t* pstRegisterCallback)
{
    if (NULL != pstRegisterCallback)
    {
        if (NULL != pstRegisterCallback->binding_device_added)
        {
            g_RegisterCallback.binding_device_added =
                pstRegisterCallback->binding_device_added;
        }

        if (NULL != pstRegisterCallback->binding_device_removed)
        {
            g_RegisterCallback.binding_device_removed =
                pstRegisterCallback->binding_device_removed;
        }
    }
}

gint ScanTypeAppend(gint type)
{
    return MediaPlayerManage.type_filter |= (type & LMS_ALL_SCAN);
}

gint ScanTypeRemove(gint type)
{
    MediaPlayerManage.type_filter = (MediaPlayerManage.type_filter & (~type)) & LMS_ALL_SCAN;
    return MediaPlayerManage.type_filter;
}