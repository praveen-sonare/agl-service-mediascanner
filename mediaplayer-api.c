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
#include <afb/afb-binding.h>

#include "mediaplayer-manager.h"

const struct afb_binding_interface *afbitf;

static struct afb_event media_added_event;
static struct afb_event media_removed_event;

/*
 * @brief Subscribe for an event
 *
 * @param struct afb_req : an afb request structure
 *
 */
static void subscribe(struct afb_req request)
{
	const char *value = afb_req_value(request, "value");
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
	afb_req_success(request, NULL, NULL);
}

/*
 * @brief Unsubscribe for an event
 *
 * @param struct afb_req : an afb request structure
 *
 */
static void unsubscribe(struct afb_req request)
{
	const char *value = afb_req_value(request, "value");
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
        jstring = json_object_new_string(l->data);
        json_object_array_add(jarray, jstring);
    }

    if (jstring == NULL)
        return NULL;

    json_object_object_add(jresp, "Media", jarray);

    // TODO: Add media path
    jstring = json_object_new_string("");
    json_object_object_add(jresp, "Path", jstring);

    return jresp;
}

static void media_results_get (struct afb_req request)
{
    GList *list;
    json_object *jresp = NULL;

    ListLock();
    list = media_lightmediascanner_scan();
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

static const struct afb_verb_desc_v1 binding_verbs[] = {
    { "media_result",	AFB_SESSION_CHECK, media_results_get,	"Media scan result" },
    { "subscribe",	AFB_SESSION_CHECK, subscribe,		"Subscribe for an event" },
    { "unsubscribe",	AFB_SESSION_CHECK, unsubscribe,		"Unsubscribe for an event" },
    { NULL }
};

static const struct afb_binding binding_description = {
    .type = AFB_BINDING_VERSION_1,
    .v1 = {
        .prefix = "media-manager",
        .info = "mediaplayer API",
        .verbs = binding_verbs,
    }
};

const struct afb_binding
*afbBindingV1Register(const struct afb_binding_interface *itf)
{
    afbitf = itf;

    Binding_RegisterCallback_t API_Callback;
    API_Callback.binding_device_added = media_broadcast_device_added;
    API_Callback.binding_device_removed = media_broadcast_device_removed;
    BindingAPIRegister(&API_Callback);

    MediaPlayerManagerInit();

    return &binding_description;
}

int afbBindingV1ServiceInit(struct afb_service service)
{
    media_added_event = afb_daemon_make_event(afbitf->daemon, "media_added");
    media_removed_event = afb_daemon_make_event(afbitf->daemon, "media_removed");

    return 0;
}
