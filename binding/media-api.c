/* 
 *   Copyright 2017 Konsulko Group
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

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <json-c/json.h>

#define AFB_BINDING_VERSION 3
#include <afb/afb-binding.h>

#include "media-manager.h"

static afb_event_t media_added_event;
static afb_event_t media_removed_event;

static gint get_scan_type(afb_req_t request, json_object *jtype) {
    gint ret = 0;
    const char *stype = NULL;
    if(!json_object_is_type(jtype,json_type_string)) {
        afb_req_fail(request,"failed", "invalid scan-type type");
        return ret;
    }
    stype = json_object_get_string(jtype);

    if(!strcasecmp(stype,MEDIA_ALL)) {
        ret = LMS_ALL_SCAN;
    } else if(!strcasecmp(stype,MEDIA_AUDIO)) {
        ret = LMS_AUDIO_SCAN;
    } else if(!strcasecmp(stype,MEDIA_VIDEO)) {
        ret = LMS_VIDEO_SCAN;
    } else if(!strcasecmp(stype,MEDIA_IMAGE)) {
        ret = LMS_IMAGE_SCAN;
    } else {
        afb_req_fail(request,"failed","invalid scan-type value");
    }
    return ret;
}

static gint get_scan_types(afb_req_t request) {

    json_object *jtypes = NULL;
    gint ret = 0;

    if(!afb_req_is_valid(request)) {
        afb_req_fail(request, "failed", "invalid request");
        return -1;
    }

    if(!json_object_object_get_ex(afb_req_json(request),"types",&jtypes)) {
        /*
         * Considered 'audio' & 'video' scan types as default types,
         * if the user doesn't provide 'types' property.
         * (for sake of backward compatibility)
         */
        ret = (LMS_AUDIO_SCAN | LMS_VIDEO_SCAN);
        return ret;
    }

    if(json_object_is_type(jtypes,json_type_array)) {
        const gint len = json_object_array_length(jtypes);
        size_t i;

        if(len > LMS_SCAN_COUNT) {
            afb_req_fail(request, "failed", "too many scan-types");
            return -1;
        }

        for(i=0; i<len; ++i){
            gint t = get_scan_type(request, json_object_array_get_idx(jtypes,i));
            if(t != 0) { ret |= t; } else { return -1; }
        }
        return ret;
    } else if(json_object_is_type(jtypes,json_type_string)) {
        gint t = get_scan_type(request,jtypes);
        if(t != 0) { return t; } else { return -1; }
    } else {
        afb_req_fail(request, "failed", "invalid scan-types format");
        return -1;
    }
}

static int get_scan_view(afb_req_t request) {
    json_object *jview = NULL;
    const char* sview = NULL;

    if(!afb_req_is_valid(request)) {
        afb_req_fail(request, "failed", "invalid request");
        return -1;
    }

    if(!json_object_object_get_ex(afb_req_json(request),"view",&jview)){
        return MEDIA_LIST_VIEW_DEFAULT;
    }

    if(!json_object_is_type(jview,json_type_string)) {
        afb_req_fail(request,"failed", "invalid media-list-view value");
        return -1;
    }
    sview = json_object_get_string(jview);

    if(!strcasecmp(sview,"clustered")) {
        return MEDIA_LIST_VIEW_CLUSTERD;
    } else if(!strcasecmp(sview,"default")){
        return MEDIA_LIST_VIEW_DEFAULT;
    } else {
        afb_req_fail(request,"failed","Unknown media-list-view type");
        return -1;
    }
}
/*
 * @brief Subscribe for an event
 *
 * @param struct afb_req : an afb request structure
 *
 */
static void subscribe(afb_req_t request)
{
    const char *value = afb_req_value(request, "value");

    if(value) {
        if(!strcasecmp(value, "media_added")) {
            gint scan_type = 0;
            gint view_type = 0;

            afb_req_subscribe(request, media_added_event);
            //Get scan types & append them to mediascan config
            scan_type = get_scan_types(request);
            if(scan_type < 0)
            return;
            ScanTypeAppend(scan_type);
            view_type = get_scan_view(request);
            if(view_type < 1)
            return;
            setAPIMediaListView(view_type);
        } else if(!strcasecmp(value, "media_removed")) {
            afb_req_subscribe(request, media_removed_event);
        } else {
            afb_req_fail(request, "failed", "Invalid event");
            return;
        }
    }
    afb_req_success(request, NULL, NULL);
}

/*
 * @brief Unsubscribe for an event
 *
 * @param struct afb_req : an afb request structure
 *
 */
static void unsubscribe(afb_req_t request)
{
    json_object *jrequest = afb_req_json(request);
    char *value = NULL;

    if( json_object_object_get_ex(jrequest,"value",NULL) &&
        json_object_object_get_ex(jrequest,"types",NULL) ) {
        //If 'types' is provided, We just remove the specified types.
        gint scan_type = get_scan_types(request);
        if(scan_type < 0)
            return;

        /*
         * If any scan type remained, we escape unsubscribing the event
         * otherwise continue to unsubscribe the event
         */
        if(ScanTypeRemove(scan_type)) {
            afb_req_success(request, NULL, NULL);
            return;
        }
    }

	value = afb_req_value(request, "value");
	if(value) {
		if(!strcasecmp(value, "media_added")) {
			afb_req_unsubscribe(request, media_added_event);
		} else if(!strcasecmp(value, "media_removed")) {
			afb_req_unsubscribe(request, media_removed_event);
		} else {
			afb_req_fail(request, "failed", "Invalid event");
			return;
		}
	}
	afb_req_success(request, NULL, NULL);
}

static gint
media_jlist_from_media_list(MediaList_t *mlist, const gint view, json_object *jarray)
{
    json_object *jstring = NULL;
    GList *l;
    gint num = 0;
    const GList *list = mlist->list;
    const char *scan_type = mlist->scan_type_str;

    for (l = list; l; l = l->next)
    {
        MediaItem_t *item = l->data;
        json_object *jdict = json_object_new_object();

        jstring = json_object_new_string(item->path);
        json_object_object_add(jdict, "path", jstring);
        if(view == MEDIA_LIST_VIEW_DEFAULT) {
            jstring = json_object_new_string(scan_type);
            json_object_object_add(jdict, "type", jstring);
        }

        if (item->metadata.title) {
            jstring = json_object_new_string(item->metadata.title);
            json_object_object_add(jdict, "title", jstring);
        }

        if (item->metadata.artist) {
            jstring = json_object_new_string(item->metadata.artist);
            json_object_object_add(jdict, "artist", jstring);
        }

        if (item->metadata.album) {
            jstring = json_object_new_string(item->metadata.album);
            json_object_object_add(jdict, "album", jstring);
        }

        if (item->metadata.genre) {
            jstring = json_object_new_string(item->metadata.genre);
            json_object_object_add(jdict, "genre", jstring);
        }

        if (item->metadata.duration) {
            json_object *jint = json_object_new_int(item->metadata.duration);
            json_object_object_add(jdict, "duration", jint);
        }

        json_object_array_add(jarray, jdict);
        num++;
    }

    if (jstring == NULL)
        return -1;

    return num;
}

static json_object* media_device_scan(ScanFilter_t *filter, gchar **error)
{
    json_object *jresp = NULL;
    json_object *jlist = NULL;
    MediaList_t *mlist = NULL;
    MediaDevice_t *mdev = NULL;
    gint res = -1;
    gint num = 0;
    gint i;

    if(!filter){
        *error = g_strdup("NULL filter!");
        return NULL;
    }
    if(!filter->scan_types)
        return NULL;

    mdev = g_malloc0(sizeof(*mdev));
    if(!mdev){
        *error = g_strdup("Cannot allocate memory.");
        return NULL;
    }

    for( i = LMS_MIN_ID; i < LMS_SCAN_COUNT; ++i)
    {
        if(filter->scan_types & (1 << i))
        {
            mdev->lists[i] = g_malloc0(sizeof(MediaList_t));
            if(!mdev->lists[i]){
                media_device_free(mdev);
                *error = g_strdup("Cannot allocate memory to media-list object");
                return NULL;
            }
        }
    }
    mdev->filters = filter;

    res = media_lists_get(mdev,error);
    if(res < 0)
    {
        media_device_free(mdev);
        return NULL;
    }

    if(filter->listview_type == MEDIA_LIST_VIEW_CLUSTERD)
    {
        jlist = json_object_new_object();
        for(i = LMS_MIN_ID; i < LMS_SCAN_COUNT; ++i)
        {
            json_object *typed_arr = json_object_new_array();
            mlist = mdev->lists[i];
            if(mlist != NULL)
            {
                res = media_jlist_from_media_list(mlist,MEDIA_LIST_VIEW_CLUSTERD,typed_arr);
                if(res < 0)
                {
                    *error = g_strdup("media parsing error");
                    media_device_free(mdev);
                    json_object_put(jlist);
                    return NULL;
                }
                json_object_object_add(jlist,lms_scan_types[i],typed_arr);
                num += res;
            }
        }
    }
    else
    {
        jlist = json_object_new_array();
        for(i = LMS_MIN_ID; i < LMS_SCAN_COUNT; ++i)
        {
            mlist = mdev->lists[i];
            if(mlist != NULL)
            {
                res = media_jlist_from_media_list(mlist,MEDIA_LIST_VIEW_DEFAULT,jlist);
                if(res < 0)
                {
                    *error = g_strdup("media parsing error");
                    media_device_free(mdev);
                    json_object_put(jlist);
                    return NULL;
                }
                num += res;
            }
        }
    }
    media_device_free(mdev);

    jresp = json_object_new_object();
    json_object_object_add(jresp, "Media", jlist);
    return jresp;
}

static void media_results_get (afb_req_t request)
{
    json_object *jresp = NULL;
    gchar *error = NULL;
    gint scan_types = 0;
    ScanFilter_t filter;

    filter.scan_types = get_scan_types(request);
    if(filter.scan_types < 0)
        return;
    filter.listview_type = get_scan_view(request);
    filter.scan_uri = SCAN_URI_DEFAULT;

    ListLock();
    jresp = media_device_scan(&filter,&error);
    ListUnlock();

    if (jresp == NULL)
    {
        afb_req_fail(request, "failed", error);
        LOGE(" %s",error);
        g_free(error);
        return;
    }

    afb_req_success(request, jresp, "Media Results Displayed");
}

static void media_broadcast_device_added (ScanFilter_t *filters)
{
    json_object *jresp = NULL;
    gchar *error = NULL;

    ListLock();
    jresp = media_device_scan(filters,&error);
    ListUnlock();

    if (jresp == NULL)
    {
        LOGE("ERROR:%s\n",error);
        g_free(error);
        return;
    }

    afb_event_push(media_added_event, jresp);
}

static void media_broadcast_device_removed (const char *obj_path)
{
    json_object *jresp = json_object_new_object();
    json_object *jstring = json_object_new_string(obj_path);

    json_object_object_add(jresp, "Path", jstring);

    afb_event_push(media_removed_event, jresp);
}

static const afb_verb_t binding_verbs[] = {
    { .verb = "media_result", .callback = media_results_get, .info = "Media scan result" },
    { .verb = "subscribe",    .callback = subscribe,         .info = "Subscribe for an event" },
    { .verb = "unsubscribe",  .callback = unsubscribe,       .info = "Unsubscribe for an event" },
    { }
};

static int init(afb_api_t api)
{
    Binding_RegisterCallback_t API_Callback;
    API_Callback.binding_device_added = media_broadcast_device_added;
    API_Callback.binding_device_removed = media_broadcast_device_removed;
    BindingAPIRegister(&API_Callback);

    media_added_event = afb_daemon_make_event("media_added");
    media_removed_event = afb_daemon_make_event("media_removed");

    return MediaPlayerManagerInit();
}

const afb_binding_t afbBindingV3 = {
    .api = "mediascanner",
    .specification = "mediaplayer API",
    .init = init,
    .verbs = binding_verbs,
};
