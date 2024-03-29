/*
 *  Copyright 2017 Konsulko Group
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

#ifndef MEDIAPLAYER_MANAGER_H
#define MEDIAPLAYER_MANAGER_H

#include <glib.h>
#include <gio/gio.h>
#include <glib-object.h>

#include "gdbus/lightmediascanner_interface.h"

    /* Debug Trace Level */
#define DT_LEVEL_ERROR          (1 << 1)
#define DT_LEVEL_WARNING        (1 << 2)
#define DT_LEVEL_NOTICE         (1 << 3)
#define DT_LEVEL_INFO           (1 << 4)
#define DT_LEVEL_DEBUG          (1 << 5)
//#define _DEBUG

#define LOGE(fmt, args...)   \
    DebugTraceSendMsg(DT_LEVEL_ERROR, g_strdup_printf("[%d:%s]" fmt, __LINE__, __FUNCTION__, ## args))
#define LOGW(fmt, args...)   \
    DebugTraceSendMsg(DT_LEVEL_WARNING, g_strdup_printf("[%d:%s]" fmt, __LINE__, __FUNCTION__, ## args))
#define LOGN(fmt, args...)   \
    DebugTraceSendMsg(DT_LEVEL_NOTICE,  g_strdup_printf("[%d:%s]" fmt, __LINE__, __FUNCTION__, ## args))
#define LOGI(fmt, args...)   \
    DebugTraceSendMsg(DT_LEVEL_INFO, g_strdup_printf("[%d:%s]" fmt, __LINE__, __FUNCTION__, ## args))
#define LOGD(fmt, args...)   \
    DebugTraceSendMsg(DT_LEVEL_DEBUG,  g_strdup_printf("[%d:%s]" fmt, __LINE__, __FUNCTION__, ## args))

#ifdef _DEBUG
 #define _DEBUG_PRINT_DBUS
 #define LOCAL_PRINT_DEBUG
#endif

#ifdef LOCAL_PRINT_DEBUG
#define D_PRINTF(fmt, args...) \
	g_print("[DEBUG][%d:%s]"fmt,  __LINE__, __FUNCTION__, ## args)
#define D_PRINTF_RAW(fmt, args...) \
	g_print(""fmt, ## args)
#else
#define D_PRINTF(fmt, args...)
#define D_PRINTF_RAW(fmt, args...)
#endif	/* ifdef _DEBUG */

void DebugTraceSendMsg(int level, gchar* message);

//service
#define AGENT_SERVICE               "org.agent"

//remote service
#define LIGHTMEDIASCANNER_SERVICE   "org.lightmediascanner"

//object path
#define LIGHTMEDIASCANNER_PATH      "/org/lightmediascanner/Scanner1"

//interface
#define LIGHTMEDIASCANNER_INTERFACE "org.lightmediascanner.Scanner1"
#define UDISKS_INTERFACE            "org.freedesktop.UDisks"
#define FREEDESKTOP_PROPERTIES      "org.freedesktop.DBus.Properties"

//sqlite
#define AUDIO_SQL_QUERY \
                  "SELECT files.path, audios.title, audio_artists.name, " \
                  "audio_albums.name, audio_genres.name, audios.length " \
                  "FROM files INNER JOIN audios " \
                  "ON files.id = audios.id " \
                  "LEFT JOIN audio_artists " \
                  "ON audio_artists.id = audios.artist_id " \
                  "LEFT JOIN audio_albums " \
                  "ON audio_albums.id = audios.album_id " \
                  "LEFT JOIN audio_genres " \
                  "ON audio_genres.id = audios.genre_id " \
                  "WHERE files.path LIKE '%s/%%' " \
                  "ORDER BY " \
                  "audios.artist_id, audios.album_id, audios.trackno"

#define VIDEO_SQL_QUERY \
                  "SELECT files.path, videos.title, videos.artist, \"\", \"\", " \
                  "videos.length FROM files " \
                  "INNER JOIN videos ON videos.id = files.id " \
                  "WHERE files.path LIKE '%s/%%' " \
                  "ORDER BY " \
                  "videos.title"

#define IMAGE_SQL_QUERY \
                "SELECT files.path, images.title, \"\", \"\", " \
                " \"\" FROM files " \
                "INNER JOIN images ON images.id = files.id " \
                "WHERE files.path LIKE '%s/%%' " \
                "ORDER BY " \
                "images.title"

enum {
    LMS_MIN_ID = 0,
    LMS_AUDIO_ID = 0,
    LMS_VIDEO_ID,
    LMS_IMAGE_ID,
    LMS_SCAN_COUNT
};

#define MEDIA_LIST_VIEW_DEFAULT  1u
#define MEDIA_LIST_VIEW_CLUSTERD 2u

#define LMS_AUDIO_SCAN (1 << LMS_AUDIO_ID)
#define LMS_VIDEO_SCAN (1 << LMS_VIDEO_ID)
#define LMS_IMAGE_SCAN (1 << LMS_IMAGE_ID)

#define LMS_ALL_SCAN   ( LMS_AUDIO_SCAN | LMS_VIDEO_SCAN | LMS_IMAGE_SCAN )

#define MEDIA_AUDIO "audio"
#define MEDIA_VIDEO "video"
#define MEDIA_IMAGE "image"
#define MEDIA_ALL   "all"

extern const char *lms_scan_types[LMS_SCAN_COUNT];

#define SCAN_URI_DEFAULT NULL

typedef struct {
    gint listview_type;
    gint scan_types;
    gchar *scan_uri;
}ScanFilter_t;

typedef struct {
    ScanFilter_t filters;
    GMutex m;
    GFileMonitor *mon;
    Scanner1 *lms_proxy;
} stMediaPlayerManage;


typedef struct {
    gchar *path;
    struct {
        gchar *title;
        gchar *artist;
        gchar *album;
        gchar *genre;
        gint  duration;
    } metadata;
}MediaItem_t;

typedef struct {
    GList *list;
    gchar* scan_type_str;
    gint scan_type_id;
} MediaList_t;

typedef struct {
    MediaList_t *lists[LMS_SCAN_COUNT];
    ScanFilter_t *filters;
} MediaDevice_t;

typedef struct tagBinding_RegisterCallback
{
    void (*binding_device_added)(ScanFilter_t *filters);
    void (*binding_device_removed)(const char *obj_path);
} Binding_RegisterCallback_t;

/* ------ PUBLIC PLUGIN FUNCTIONS --------- */
void BindingAPIRegister(const Binding_RegisterCallback_t* pstRegisterCallback);
int MediaPlayerManagerInit(void);

gint ScanTypeAppend(gint);
gint ScanTypeRemove(gint);
void setAPIMediaListView(gint view);

void ListLock();
void ListUnlock();

gint media_lists_get(MediaDevice_t* mdev, gchar **error);
void media_device_free(MediaDevice_t *mdev);

#endif