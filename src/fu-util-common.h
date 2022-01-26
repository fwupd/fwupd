/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupd.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include "fwupd-security-attr-private.h"

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

typedef enum {
	FU_UTIL_TERM_COLOR_BLACK = 30,
	FU_UTIL_TERM_COLOR_RED = 31,
	FU_UTIL_TERM_COLOR_GREEN = 32,
	FU_UTIL_TERM_COLOR_YELLOW = 33,
	FU_UTIL_TERM_COLOR_BLUE = 34,
	FU_UTIL_TERM_COLOR_MAGENTA = 35,
	FU_UTIL_TERM_COLOR_CYAN = 36,
	FU_UTIL_TERM_COLOR_WHITE = 37,
} FuUtilTermColor;

void
fu_util_print_data(const gchar *title, const gchar *msg);
gchar *
fu_util_term_format(const gchar *text, FuUtilTermColor fg_color);
guint
fu_util_prompt_for_number(guint maxnum);
gboolean
fu_util_prompt_for_boolean(gboolean def);

void
fu_util_print_tree(GNode *n, gpointer data);
gboolean
fu_util_is_interesting_device(FwupdDevice *dev);
gchar *
fu_util_get_user_cache_path(const gchar *fn);
gchar *
fu_util_get_versions(void);

void
fu_util_warning_box(const gchar *title, const gchar *body, guint width);
gboolean
fu_util_prompt_warning(FwupdDevice *device,
		       FwupdRelease *release,
		       const gchar *machine,
		       GError **error);
gboolean
fu_util_prompt_warning_fde(FwupdDevice *dev, GError **error);
gboolean
fu_util_prompt_complete(FwupdDeviceFlags flags, gboolean prompt, GError **error);
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
gchar *
fu_util_release_get_name(FwupdRelease *release);
const gchar *
fu_util_branch_for_display(const gchar *branch);

const gchar *
fu_util_get_systemd_unit(void);
gboolean
fu_util_using_correct_daemon(GError **error);

gboolean
fu_util_parse_filter_flags(const gchar *filter,
			   FwupdDeviceFlags *include,
			   FwupdDeviceFlags *exclude,
			   GError **error);
gchar *
fu_util_convert_description(const gchar *xml, GError **error);
gchar *
fu_util_time_to_str(guint64 tmp);

gchar *
fu_util_device_to_string(FwupdDevice *dev, guint idt);
gchar *
fu_util_plugin_to_string(FwupdPlugin *plugin, guint idt);
const gchar *
fu_util_plugin_flag_to_string(FwupdPluginFlags plugin_flag);
gchar *
fu_util_release_to_string(FwupdRelease *rel, guint idt);
gchar *
fu_util_remote_to_string(FwupdRemote *remote, guint idt);
gchar *
fu_util_security_attrs_to_string(GPtrArray *attrs, FuSecurityAttrToStringFlags flags);
gchar *
fu_util_security_events_to_string(GPtrArray *events, FuSecurityAttrToStringFlags flags);
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
fu_util_switch_branch_warning(FwupdDevice *dev,
			      FwupdRelease *rel,
			      gboolean assume_yes,
			      GError **error);
void
fu_util_show_unsupported_warn(void);
gboolean
fu_util_is_url(const gchar *perhaps_url);
gboolean
fu_util_setup_interactive_console(GError **error);
gboolean
fu_util_print_builder(JsonBuilder *builder, GError **error);
