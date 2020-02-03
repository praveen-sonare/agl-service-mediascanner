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

static void media_item_free(gpointer data)
{
    MediaItem_t *item = data;

	g_free(item->metadata.title);
	g_free(item->metadata.artist);
	g_free(item->metadata.album);
	g_free(item->metadata.genre);
	g_free(item->path);
	g_free(item);
}

gint media_lightmediascanner_scan(MediaList_t *mlist, gchar* uri, gchar **error)
{
    sqlite3_stmt *res;
    const char *tail;
    const gchar *db_path;
    gchar *query;
    int ret = 0;
    gint num = 0;

    if(!scanDB.is_open) {
        db_path = scanner1_get_data_base_path(MediaPlayerManage.lms_proxy);

        ret = sqlite3_open(db_path, &scanDB.db);
        if (ret != SQLITE_OK) {
            LOGD("Cannot open SQLITE database: '%s'\n", db_path);
            scanDB.is_open = FALSE;
            g_list_free_full(mlist,media_item_free);
            return -1;
        }
        scanDB.is_open = TRUE;
    }

    switch (mlist->scan_type_id) {
        case LMS_VIDEO_ID:
            query = g_strdup_printf(VIDEO_SQL_QUERY, uri ? uri : "");
            break;
        case LMS_IMAGE_ID:
            query = g_strdup_printf(IMAGE_SQL_QUERY, uri ? uri : "");
            break;
        case LMS_AUDIO_ID:
        default:
            query = g_strdup_printf(AUDIO_SQL_QUERY, uri ? uri : "");
    }

    if (!query) {
        *error = g_strdup_printf("Cannot allocate memory for query");
        return -1;
    }

    ret = sqlite3_prepare_v2(scanDB.db, query, (int) strlen(query), &res, &tail);
    if (ret) {
        *error = g_strdup("Cannot execute query");
        g_free(query);
        return -1;
    }

    while (sqlite3_step(res) == SQLITE_ROW) {
        struct stat buf;
        MediaItem_t *item = NULL;
        const char *path = (const char *) sqlite3_column_text(res, 0);
        gchar *tmp;

        ret = stat(path, &buf);
        if (ret)
            continue;

        //We may check the allocation result ... but It maybe a bit expensive in such a loop
        item = g_malloc0(sizeof(*item));

        tmp = g_uri_escape_string(path, "/", TRUE);
        item->path = g_strdup_printf("file://%s", tmp);
        g_free(tmp);

        item->metadata.title = g_strdup((gchar *) sqlite3_column_text(res, 1));
        item->metadata.artist = g_strdup((gchar *) sqlite3_column_text(res, 2));
        item->metadata.album = g_strdup((gchar *) sqlite3_column_text(res, 3));
        item->metadata.genre = g_strdup((gchar *) sqlite3_column_text(res, 4));
        item->metadata.duration = sqlite3_column_int(res, 5) * 1000;
        mlist->list = g_list_append(mlist->list, item);
        num++;
    }
    g_free(query);

    return num;
}

void media_device_free(MediaDevice_t *mdev)
{
    gint i;

    if(mdev){
        for(i = LMS_MIN_ID; i < LMS_SCAN_COUNT; ++i)
        {
            if(mdev->lists[i] != NULL) {
                g_list_free_full(mdev->lists[i]->list,media_item_free);
                g_free(mdev->lists[i]);
            }
        }
        g_free(mdev->filters->scan_uri);
        mdev->filters->scan_uri = NULL;
        g_free(mdev);
    }
}

static void
on_interface_proxy_properties_changed (GDBusProxy *proxy,
                                    GVariant *changed_properties,
                                    const gchar* const  *invalidated_properties)
{
    GVariantIter iter;
    gchar *key = NULL;
    GVariant *subValue = NULL;
    const gchar *pInterface;
    ScanFilter_t *filter = &MediaPlayerManage.filters;
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

    if (filter->scan_types &&
        filter->scan_uri &&
        g_RegisterCallback.binding_device_added)
        g_RegisterCallback.binding_device_added(filter);
}

static int MediaPlayerDBusInit(void)
{
    GError *error = NULL;

    MediaPlayerManage.lms_proxy = NULL;
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
        MediaPlayerManage.filters.scan_uri = path;
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

void setAPIMediaListView(gint view) {
    MediaPlayerManage.filters.listview_type = view;
}

gint ScanTypeAppend(gint type)
{
    return MediaPlayerManage.filters.scan_types |= (type & LMS_ALL_SCAN);
}

gint ScanTypeRemove(gint type)
{
    MediaPlayerManage.filters.scan_types =
        ( MediaPlayerManage.filters.scan_types & (~type)) & LMS_ALL_SCAN;
    return MediaPlayerManage.filters.scan_types;
}

gint media_lists_get(MediaDevice_t* mdev, gchar **error)
{
    MediaList_t *mlist = NULL;
    ScanFilter_t *filters = NULL;
    gint ret = -1;
    gint scaned_media = 0;
    gint i = 0;

    if(!mdev)
    {
        *error = g_strdup("'MediaDevice' object is NULL!");
        return -1;
    }
    filters = mdev->filters;

    for( i = LMS_MIN_ID; i < LMS_SCAN_COUNT; ++i)
    {
        if(filters->scan_types & (1 << i))
        {
            mlist = mdev->lists[i];
            mlist->scan_type_str = lms_scan_types[i];
            mlist->scan_type_id = i;

            ret = media_lightmediascanner_scan(mlist,filters->scan_uri,error);
            if(ret < 0){
                return ret;
            } else if(ret == 0){
                free(mdev->lists[i]);
                mdev->lists[i] = NULL;
            } else {
                scaned_media += ret;
            }
        }
    }

    if(scaned_media == 0)
    {
        *error = g_strdup("No media found!");
        return -1;
    }
    LOGD("\n\tscanned media: %d\n",scaned_media);
    return scaned_media;
}