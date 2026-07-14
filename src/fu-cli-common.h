/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-cli-struct.h"
#include "fu-console.h"

/* custom return codes */
#define EXIT_NOTHING_TO_DO 2
#define EXIT_NOT_FOUND	   3
#define EXIT_NOT_REACHABLE 101 /* ENETUNREACH */

/* this is only valid for tools */
#define FWUPD_ERROR_INVALID_ARGS (FWUPD_ERROR_LAST + 1)

typedef enum {
	FU_SECURITY_ATTR_TO_STRING_FLAG_NONE = 0,
	FU_SECURITY_ATTR_TO_STRING_FLAG_SHOW_OBSOLETES = 1 << 0,
	FU_SECURITY_ATTR_TO_STRING_FLAG_SHOW_URLS = 1 << 1,
	/*< private >*/
	FU_SECURITY_ATTR_TO_STRING_FLAG_LAST
} G_GNUC_FLAG_ENUM FuSecurityAttrToStringFlags;

/* node with refcounted data */
typedef GNode FuCliNode;
void
fu_cli_print_node(FuConsole *console, FwupdClient *client, FuCliNode *n) G_GNUC_NON_NULL(1, 2, 3);
void
fu_cli_free_node(FuCliNode *n) G_GNUC_NON_NULL(1);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCliNode, fu_cli_free_node)

gboolean
fu_cli_is_interesting_device(GPtrArray *devs, FwupdDevice *dev) G_GNUC_NON_NULL(1, 2);
gchar *
fu_cli_get_user_cache_path(const gchar *fn) G_GNUC_NON_NULL(1);

gboolean
fu_cli_prompt_warning(FuConsole *console,
		      FwupdDevice *device,
		      FwupdRelease *release,
		      const gchar *machine,
		      GError **error) G_GNUC_NON_NULL(1, 2, 3, 4);
gboolean
fu_cli_prompt_warning_fde(FuConsole *console, FwupdDevice *dev, GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_cli_modify_remote_warning(FuConsole *console,
			     FwupdRemote *remote,
			     gboolean assume_yes,
			     GError **error) G_GNUC_NON_NULL(1, 2);

gboolean
fu_cli_prompt_complete(FuConsole *console, FwupdDeviceFlags flags, gboolean prompt, GError **error)
    G_GNUC_NON_NULL(1);

const gchar *
fu_cli_branch_for_display(const gchar *branch);
const gchar *
fu_cli_request_get_message(FwupdRequest *req) G_GNUC_NON_NULL(1);

gchar *
fu_cli_device_to_string(FwupdClient *client, FwupdDevice *dev, guint idt) G_GNUC_NON_NULL(1, 2);
gchar *
fu_cli_plugin_to_string(FwupdPlugin *plugin, guint idt) G_GNUC_NON_NULL(1);
gchar *
fu_cli_plugin_flag_to_string(FwupdPluginFlags plugin_flag);
const gchar *
fu_cli_release_flag_to_string(FwupdReleaseFlags release_flag);
const gchar *
fu_cli_request_flag_to_string(FwupdRequestFlags request_flag);
gchar *
fu_cli_device_problem_to_string(FwupdClient *client, FwupdDevice *dev, FwupdDeviceProblem problem)
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fu_cli_device_problems_to_strings(FwupdClient *client, FwupdDevice *dev) G_GNUC_NON_NULL(1, 2);
gchar *
fu_cli_security_attrs_to_string(GPtrArray *attrs, FuSecurityAttrToStringFlags flags)
    G_GNUC_NON_NULL(1);
gchar *
fu_cli_security_events_to_string(GPtrArray *events, FuSecurityAttrToStringFlags flags)
    G_GNUC_NON_NULL(1);
gchar *
fu_cli_security_issues_to_string(GPtrArray *devices) G_GNUC_NON_NULL(1);
gint
fu_cli_sort_devices_by_flags_cb(gconstpointer a, gconstpointer b) G_GNUC_NON_NULL(1, 2);

gboolean
fu_cli_switch_branch_warning(FuConsole *console,
			     FwupdDevice *dev,
			     FwupdRelease *rel,
			     gboolean assume_yes,
			     GError **error) G_GNUC_NON_NULL(1, 2, 3);
void
fu_cli_show_unsupported_warning(FuConsole *console) G_GNUC_NON_NULL(1);
gboolean
fu_cli_is_url(const gchar *perhaps_url) G_GNUC_NON_NULL(1);
void
fu_cli_print_json_object(FuConsole *console, FwupdJsonObject *json_obj) G_GNUC_NON_NULL(1, 2);
void
fu_cli_print_error_as_json(FuConsole *console, const GError *error) G_GNUC_NON_NULL(1);
gchar *
fu_cli_project_versions_to_string(GHashTable *metadata) G_GNUC_NON_NULL(1);
void
fu_cli_project_versions_as_json(FuConsole *console, GHashTable *metadata) G_GNUC_NON_NULL(1, 2);
const gchar *
fu_cli_get_prgname(const gchar *argv0) G_GNUC_NON_NULL(1);
