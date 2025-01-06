/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include <json-glib/json-glib.h>

#include "fwupd-security-attr-private.h"

#include "fu-console.h"

/* custom return codes */
#define EXIT_NOTHING_TO_DO 2
#define EXIT_NOT_FOUND	   3

/* this is only valid for tools */
#define FWUPD_ERROR_INVALID_ARGS (FWUPD_ERROR_LAST + 1)

typedef struct FuUtilPrivate FuUtilPrivate;
typedef gboolean (*FuUtilCmdFunc)(FuUtilPrivate *util, gchar **values, GError **error)
    G_GNUC_NON_NULL(1);
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

/* node with refcounted data */
typedef GNode FuUtilNode;
void
fu_util_print_node(FuConsole *console, FwupdClient *client, FuUtilNode *n) G_GNUC_NON_NULL(1, 2, 3);
void
fu_util_free_node(FuUtilNode *n) G_GNUC_NON_NULL(1);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUtilNode, fu_util_free_node)

gboolean
fu_util_is_interesting_device(FwupdDevice *dev) G_GNUC_NON_NULL(1);
gchar *
fu_util_get_user_cache_path(const gchar *fn) G_GNUC_NON_NULL(1);

gboolean
fu_util_prompt_warning(FuConsole *console,
		       FwupdDevice *device,
		       FwupdRelease *release,
		       const gchar *machine,
		       GError **error) G_GNUC_NON_NULL(1, 2, 3, 4);
gboolean
fu_util_prompt_warning_fde(FuConsole *console, FwupdDevice *dev, GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_util_modify_remote_warning(FuConsole *console,
			      FwupdRemote *remote,
			      gboolean assume_yes,
			      GError **error) G_GNUC_NON_NULL(1, 2);

gboolean
fu_util_prompt_complete(FuConsole *console, FwupdDeviceFlags flags, gboolean prompt, GError **error)
    G_GNUC_NON_NULL(1);

GPtrArray *
fu_util_cmd_array_new(void);
void
fu_util_cmd_array_add(GPtrArray *array,
		      const gchar *name,
		      const gchar *arguments,
		      const gchar *description,
		      FuUtilCmdFunc callback) G_GNUC_NON_NULL(1, 2, 4);
gchar *
fu_util_cmd_array_to_string(GPtrArray *array) G_GNUC_NON_NULL(1);
void
fu_util_cmd_array_sort(GPtrArray *array) G_GNUC_NON_NULL(1);
gboolean
fu_util_cmd_array_run(GPtrArray *array,
		      FuUtilPrivate *priv,
		      const gchar *command,
		      gchar **values,
		      GError **error) G_GNUC_NON_NULL(1, 2);
const gchar *
fu_util_branch_for_display(const gchar *branch);
const gchar *
fu_util_request_get_message(FwupdRequest *req) G_GNUC_NON_NULL(1);

gboolean
fu_util_parse_filter_device_flags(const gchar *filter,
				  FwupdDeviceFlags *include,
				  FwupdDeviceFlags *exclude,
				  GError **error) G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_util_parse_filter_release_flags(const gchar *filter,
				   FwupdReleaseFlags *include,
				   FwupdReleaseFlags *exclude,
				   GError **error) G_GNUC_NON_NULL(1, 2, 3);
gchar *
fu_util_device_to_string(FwupdClient *client, FwupdDevice *dev, guint idt) G_GNUC_NON_NULL(1, 2);
gchar *
fu_util_plugin_to_string(FwupdPlugin *plugin, guint idt) G_GNUC_NON_NULL(1);
gchar *
fu_util_plugin_flag_to_string(FwupdPluginFlags plugin_flag);
gint
fu_util_plugin_name_sort_cb(FwupdPlugin **item1, FwupdPlugin **item2);
gchar *
fu_util_device_problem_to_string(FwupdClient *client, FwupdDevice *dev, FwupdDeviceProblem problem)
    G_GNUC_NON_NULL(1, 2);
gchar *
fu_util_security_attrs_to_string(GPtrArray *attrs, FuSecurityAttrToStringFlags flags)
    G_GNUC_NON_NULL(1);
gchar *
fu_util_security_events_to_string(GPtrArray *events, FuSecurityAttrToStringFlags flags)
    G_GNUC_NON_NULL(1);
gchar *
fu_util_security_issues_to_string(GPtrArray *devices) G_GNUC_NON_NULL(1);
gint
fu_util_sort_devices_by_flags_cb(gconstpointer a, gconstpointer b) G_GNUC_NON_NULL(1, 2);

gboolean
fu_util_switch_branch_warning(FuConsole *console,
			      FwupdDevice *dev,
			      FwupdRelease *rel,
			      gboolean assume_yes,
			      GError **error) G_GNUC_NON_NULL(1, 2, 3);
void
fu_util_show_unsupported_warning(FuConsole *console) G_GNUC_NON_NULL(1);
gboolean
fu_util_is_url(const gchar *perhaps_url) G_GNUC_NON_NULL(1);
gboolean
fu_util_print_builder(FuConsole *console, JsonBuilder *builder, GError **error)
    G_GNUC_NON_NULL(1, 2);
void
fu_util_print_error_as_json(FuConsole *console, const GError *error) G_GNUC_NON_NULL(1);
gchar *
fu_util_project_versions_to_string(GHashTable *metadata) G_GNUC_NON_NULL(1);
gboolean
fu_util_project_versions_as_json(FuConsole *console, GHashTable *metadata, GError **error)
    G_GNUC_NON_NULL(1, 2);
const gchar *
fu_util_get_prgname(const gchar *argv0) G_GNUC_NON_NULL(1);
