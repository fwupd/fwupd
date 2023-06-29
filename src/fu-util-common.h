/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include <json-glib/json-glib.h>

#include "fwupd-bios-setting-private.h"
#include "fwupd-security-attr-private.h"

#include "fu-console.h"

/* this is only valid for tools */
#define FWUPD_ERROR_INVALID_ARGS (FWUPD_ERROR_LAST + 1)

typedef struct FuUtilPrivate FuUtilPrivate;
typedef gboolean (*FuUtilCmdFunc)(FuUtilPrivate *util, gchar **values, GError **error);
typedef struct {
	gchar *name;
	gchar *arguments;
	gchar *description;
	FuUtilCmdFunc callback;
} FuUtilCmd;

typedef enum {
	FU_SECURITY_ATTR_TO_STRING_FLAG_NONE = 0,
	FU_SECURITY_ATTR_TO_STRING_FLAG_SHOW_OBSOLETES = 1 << 0,
	FU_SECURITY_ATTR_TO_STRING_FLAG_SHOW_URLS = 1 << 1,
	/*< private >*/
	FU_SECURITY_ATTR_TO_STRING_FLAG_LAST
} FuSecurityAttrToStringFlags;

void
fu_util_print_tree(FuConsole *console, FwupdClient *client, GNode *n);
gboolean
fu_util_is_interesting_device(FwupdDevice *dev);
gchar *
fu_util_get_user_cache_path(const gchar *fn);

gboolean
fu_util_prompt_warning(FuConsole *console,
		       FwupdDevice *device,
		       FwupdRelease *release,
		       const gchar *machine,
		       GError **error);
gboolean
fu_util_prompt_warning_fde(FuConsole *console, FwupdDevice *dev, GError **error);
gboolean
fu_util_prompt_complete(FuConsole *console,
			FwupdDeviceFlags flags,
			gboolean prompt,
			GError **error);
gboolean
fu_util_update_reboot(GError **error);

GPtrArray *
fu_util_cmd_array_new(void);
void
fu_util_cmd_array_add(GPtrArray *array,
		      const gchar *name,
		      const gchar *arguments,
		      const gchar *description,
		      FuUtilCmdFunc callback);
gchar *
fu_util_cmd_array_to_string(GPtrArray *array);
void
fu_util_cmd_array_sort(GPtrArray *array);
gboolean
fu_util_cmd_array_run(GPtrArray *array,
		      FuUtilPrivate *priv,
		      const gchar *command,
		      gchar **values,
		      GError **error);
const gchar *
fu_util_branch_for_display(const gchar *branch);
const gchar *
fu_util_request_get_message(FwupdRequest *req);

const gchar *
fu_util_get_systemd_unit(void);
gboolean
fu_util_using_correct_daemon(GError **error);

gboolean
fu_util_parse_filter_device_flags(const gchar *filter,
				  FwupdDeviceFlags *include,
				  FwupdDeviceFlags *exclude,
				  GError **error);
gboolean
fu_util_parse_filter_release_flags(const gchar *filter,
				   FwupdReleaseFlags *include,
				   FwupdReleaseFlags *exclude,
				   GError **error);
gchar *
fu_util_convert_description(const gchar *xml, GError **error);
gchar *
fu_util_time_to_str(guint64 tmp);

gchar *
fu_util_device_to_string(FwupdClient *client, FwupdDevice *dev, guint idt);
gchar *
fu_util_plugin_to_string(FwupdPlugin *plugin, guint idt);
const gchar *
fu_util_plugin_flag_to_string(FwupdPluginFlags plugin_flag);
gchar *
fu_util_security_attrs_to_string(GPtrArray *attrs, FuSecurityAttrToStringFlags flags);
gchar *
fu_util_security_events_to_string(GPtrArray *events, FuSecurityAttrToStringFlags flags);
gchar *
fu_util_security_issues_to_string(GPtrArray *devices);
gboolean
fu_util_send_report(FwupdClient *client,
		    const gchar *report_uri,
		    const gchar *data,
		    const gchar *sig,
		    gchar **uri,
		    GError **error);
gint
fu_util_sort_devices_by_flags_cb(gconstpointer a, gconstpointer b);
gint
fu_util_device_order_sort_cb(gconstpointer a, gconstpointer b);

gboolean
fu_util_switch_branch_warning(FuConsole *console,
			      FwupdDevice *dev,
			      FwupdRelease *rel,
			      gboolean assume_yes,
			      GError **error);
void
fu_util_show_unsupported_warning(FuConsole *console);
gboolean
fu_util_is_url(const gchar *perhaps_url);
gboolean
fu_util_print_builder(FuConsole *console, JsonBuilder *builder, GError **error);
void
fu_util_print_error_as_json(FuConsole *console, const GError *error);
gchar *
fu_util_project_versions_to_string(GHashTable *metadata);
gboolean
fu_util_project_versions_as_json(FuConsole *console, GHashTable *metadata, GError **error);
