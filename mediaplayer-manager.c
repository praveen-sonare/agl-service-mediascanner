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
#include <gio/gio.h>
#include <glib-object.h>
#include <sqlite3.h>

#include "mediaplayer-manager.h"

static Binding_RegisterCallback_t g_RegisterCallback = { 0 };
static stMediaPlayerManage MediaPlayerManage = { 0 };

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

GList* media_lightmediascanner_scan(void)
{
    sqlite3 *conn;
    sqlite3_stmt *res;
    GList *list;
    const char *tail;
    const gchar *db_path;
    int ret = 0;

    list = MediaPlayerManage.list;

    // Returned cached result
    if (list)
        return list;

    db_path = scanner1_get_data_base_path(MediaPlayerManage.lms_proxy);

    ret = sqlite3_open(db_path, &conn);
    if (ret) {
        LOGE("Cannot open SQLITE database: '%s'\n", db_path);
        return NULL;
    }

    ret = sqlite3_prepare_v2(conn, SQL_QUERY, strlen(SQL_QUERY) + 1, &res, &tail);
    if (ret) {
        LOGE("Cannot execute query '%s'\n", SQL_QUERY);
        return NULL;
    }

    while (sqlite3_step(res) == SQLITE_ROW) {
        struct stat buf;
        const char *path = (const char *) sqlite3_column_text(res, 0);

        ret = stat(path, &buf);
        if (ret)
            continue;

        list = g_list_append(list, g_strdup_printf("file://%s", path));
    }

    MediaPlayerManage.list = list;

    return list;
}


static void
on_interface_proxy_properties_changed (GDBusProxy *proxy,
                                    GVariant *changed_properties,
                                    const gchar* const  *invalidated_properties)
{
    GVariantIter iter;
    const gchar *key;
    GVariant *subValue;
    const gchar *pInterface;
    GList *list;

    pInterface = g_dbus_proxy_get_interface_name (proxy);

    if (0 != g_strcmp0(pInterface, LIGHTMEDIASCANNER_INTERFACE))
        return;

    g_variant_iter_init (&iter, changed_properties);
    while (g_variant_iter_next (&iter, "{&sv}", &key, &subValue))
    {
        gboolean val;
        if (0 == g_strcmp0(key,"IsScanning")) {
            g_variant_get(subValue, "b", &val);
            if (val == TRUE)
                return;
        } else if (0 == g_strcmp0(key, "WriteLocked")) {
            g_variant_get(subValue, "b", &val);
            if (val == TRUE)
                return;
        }
    }

    ListLock();

    list = media_lightmediascanner_scan();

    if (list != NULL && g_RegisterCallback.binding_device_added)
        g_RegisterCallback.binding_device_added(list);

    ListUnlock();
}

static void
on_device_removed (GDBusProxy *proxy, gpointer user_data)
{
    ListLock();

    g_list_free(MediaPlayerManage.list);
    MediaPlayerManage.list = NULL;

    if (g_RegisterCallback.binding_device_removed)
        g_RegisterCallback.binding_device_removed((const char *) user_data);

    ListUnlock();
}

static int MediaPlayerDBusInit(void)
{
    GError *error = NULL;

    MediaPlayerManage.lms_proxy = scanner1_proxy_new_for_bus_sync(
        G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, LIGHTMEDIASCANNER_SERVICE,
        LIGHTMEDIASCANNER_PATH, NULL, &error);

    if (MediaPlayerManage.lms_proxy == NULL) {
        LOGE("Create LightMediaScanner Proxy failed\n");
        return -1;
    }

    MediaPlayerManage.udisks_proxy = org_freedesktop_udisks_proxy_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, UDISKS_SERVICE,
        UDISKS_PATH, NULL, &error);

    if (MediaPlayerManage.udisks_proxy == NULL) {
        LOGE("Create UDisks Proxy failed\n");
        return -1;
    }

    g_signal_connect (MediaPlayerManage.lms_proxy,
                      "g-properties-changed",
                      G_CALLBACK (on_interface_proxy_properties_changed),
                      NULL);

    g_signal_connect (MediaPlayerManage.udisks_proxy,
                      "device-removed",
                      G_CALLBACK (on_device_removed),
                      NULL);

    return 0;
}

static void *media_event_loop_thread(void *unused)
{
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    int ret;

    ret = MediaPlayerDBusInit();
    if (ret == 0) {
        LOGD("g_main_loop_run\n");
        g_main_loop_run(loop);
    }

    g_main_loop_unref(loop);

    return NULL;
}

/*
 * Create MediaPlayer Manager Thread
 * Note: mediaplayer-api should do MediaPlayerManagerInit() before any other 
 *       API calls
 * Returns: 0 - success or error conditions
 */
int MediaPlayerManagerInit() {
    pthread_t thread_id;

    g_mutex_init(&(MediaPlayerManage.m));

    pthread_create(&thread_id, NULL, media_event_loop_thread, NULL);

    return 0;
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
