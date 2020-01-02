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
    const *stype = NULL;
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
         * Considering 'audio' & 'video' scan types as default
         * if the user doesn't provide 'types' property,
         * for sake of compability with previeus versions
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
            if(t != 0) { ret |= t; } else { return -1;}
        }
        return ret;
    } else if(json_object_is_type(jtypes,json_type_string)) {
        gint t = get_scan_type(request,jtypes);
        if(t != 0) {return t;} else {return -1;}
    } else {
        afb_req_fail(request, "failed", "invalid scan-types format");
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
    gint scantype = 0;

	if(value) {
		if(!strcasecmp(value, "media_added")) {
			afb_req_subscribe(request, media_added_event);
		} else if(!strcasecmp(value, "media_removed")) {
			afb_req_subscribe(request, media_removed_event);
		} else {
			afb_req_fail(request, "failed", "Invalid event");
			return;
		}
	}

    //Get scan types & append them to scan config
    scantype = get_scan_types(request);
    if(scantype < 0) return;

    ScanTypeAppend(scantype);
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
        gint scantype = get_scan_types(request);
        if(scantype < 0) return;

        /*
         * If any scan type remained, we escape unsubscribe the event
         * otherwise continue to unsubscribe the event
         */
        if(ScanTypeRemove(scantype)) {
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

static json_object *new_json_object_from_device(GList *list)
{
    json_object *jarray = json_object_new_array();
    json_object *jresp = json_object_new_object();
    json_object *jstring = NULL;
    GList *l;

    for (l = list; l; l = l->next)
    {
        struct Media_Item *item = l->data;
        json_object *jdict = json_object_new_object();

        jstring = json_object_new_string(item->path);
        json_object_object_add(jdict, "path", jstring);

        jstring = json_object_new_string(item->type);
        json_object_object_add(jdict, "type", jstring);

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
    }

    if (jstring == NULL)
        return NULL;

    json_object_object_add(jresp, "Media", jarray);

    return jresp;
}

static void media_results_get (afb_req_t request)
{
    GList *list = NULL;
    json_object *jresp = NULL;
    gint scan_type = 0;

    scan_type = get_scan_types(request);
    if(scan_type < 0) return;

    ListLock();

    if(scan_type & LMS_AUDIO_SCAN)
        list = media_lightmediascanner_scan(list, NULL, LMS_AUDIO_SCAN);
    if(scan_type & LMS_VIDEO_SCAN)
        list = media_lightmediascanner_scan(list, NULL, LMS_VIDEO_SCAN);
    if(scan_type & LMS_IMAGE_SCAN)
        list = media_lightmediascanner_scan(list, NULL, LMS_IMAGE_SCAN);

    if (list == NULL) {
        afb_req_fail(request, "failed", "media scan error");
        ListUnlock();
        return;
    }

    jresp = new_json_object_from_device(list);
    g_list_free(list);
    ListUnlock();

    if (jresp == NULL) {
        afb_req_fail(request, "failed", "media parsing error");
        return;
    }

    afb_req_success(request, jresp, "Media Results Displayed");
}

static void media_broadcast_device_added (GList *list)
{
    json_object *jresp = new_json_object_from_device(list);

    if (jresp != NULL) {
        afb_event_push(media_added_event, jresp);
    }
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
