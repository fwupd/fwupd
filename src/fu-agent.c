/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuAgent"

#include "config.h"

#include <fwupd.h>
#include <glib/gi18n.h>
#ifdef HAVE_GIO_UNIX
#include <glib-unix.h>
#endif
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>

#include "fu-common.h"
#include "fu-util-common.h"
#include "fwupd-device-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-security-attr-private.h"

#ifdef HAVE_SYSTEMD
#include "fu-systemd.h"
#endif

struct FuUtilPrivate {
	GCancellable		*cancellable;
	GOptionContext		*context;
	FwupdClient		*client;
	FwupdInstallFlags	 flags;
};

static gchar *
fu_util_agent_get_config_fn (void)
{
	g_autofree gchar *path = fu_common_get_path (FU_PATH_KIND_SYSCONFDIR_PKG);
	return g_build_filename (path, "agent.conf", NULL);
}

static GKeyFile *
fu_util_agent_get_config (GError **error)
{
	g_autofree gchar *fn = fu_util_agent_get_config_fn ();
	g_autoptr(GKeyFile) config = g_key_file_new ();
	if (!g_key_file_load_from_file (config, fn, G_KEY_FILE_KEEP_COMMENTS, error)) {
		g_prefix_error (error, "failed to load %s: ", fn);
		return FALSE;
	}
	return g_steal_pointer (&config);
}

static gboolean
fu_util_agent_set_server (FuUtilPrivate *priv, const gchar *server, GError **error)
{
	g_autofree gchar *fn = fu_util_agent_get_config_fn ();
	g_autoptr(GKeyFile) config = fu_util_agent_get_config (error);
	if (config == NULL)
		return FALSE;
	g_key_file_set_string (config, "fwupdagent", "Server", server);
	return g_key_file_save_to_file (config, fn, error);
}

static gboolean
fu_util_agent_run_action (FuUtilPrivate *priv,
			  JsonObject *json_object,
			  GError **error)
{
	const gchar *checksum;
	const gchar *device_id;
	const gchar *task;
	g_autoptr(FwupdDevice) device = NULL;
	g_autoptr(FwupdRelease) rel = NULL;
	g_autoptr(GPtrArray) releases = NULL;

	/* check object for args */
	if (!json_object_has_member (json_object, "Task")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "No task specified");
		return FALSE;
	}
	if (!json_object_has_member (json_object, FWUPD_RESULT_KEY_DEVICE_ID)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "No DeviceId specified");
		return FALSE;
	}
	if (!json_object_has_member (json_object, FWUPD_RESULT_KEY_CHECKSUM)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "No checksum specified");
		return FALSE;
	}

	/* check task */
	task = json_object_get_string_member (json_object, "Task");
	if (g_strcmp0 (task, "upgrade") != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Invalid task '%s', only 'upgrade' supported",
			     task);
		return FALSE;
	}

	/* find device */
	device_id = json_object_get_string_member (json_object, FWUPD_RESULT_KEY_DEVICE_ID);
	device = fwupd_client_get_device_by_id (priv->client, device_id,
						priv->cancellable, error);
	if (device == NULL)
		return FALSE;

	/* find release for device */
	checksum = json_object_get_string_member (json_object, FWUPD_RESULT_KEY_CHECKSUM);
	releases = fwupd_client_get_releases (priv->client, device_id,
					      priv->cancellable, error);
	for (guint i = 0; i < releases->len; i++) {
		FwupdRelease *rel_tmp = g_ptr_array_index (releases, i);
		if (fwupd_release_has_checksum (rel_tmp, checksum)) {
			rel = g_object_ref (rel_tmp);
			break;
		}
	}
	if (rel == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to find a release with a checksum of %s",
			     checksum);
		return FALSE;
	}

	/* FIXME: before I actually add this, is this really a good idea?! */
	g_debug ("will download and deploy %s", fwupd_release_get_uri (rel));
	return TRUE;
}

static gboolean
fu_util_agent_send (FuUtilPrivate *priv,
		    const gchar *endpoint,
		    JsonBuilder *builder,
		    GError **error)
{
	JsonNode *json_root;
	JsonObject *json_object;
	const gchar *server_msg = NULL;
	const gchar *uri = NULL;
	g_autofree gchar *data = NULL;
	g_autofree gchar *server = NULL;
	g_autofree gchar *str = NULL;
	g_auto(GStrv) checksums = NULL;
	g_autoptr(GBytes) upload_response = NULL;
	g_autoptr(GKeyFile) config = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_builder_root = NULL;
	g_autoptr(JsonParser) json_parser = NULL;

	/* get server */
	config = fu_util_agent_get_config (error);
	if (config == NULL)
		return FALSE;
	server = g_key_file_get_string (config, "fwupdagent", "Server", error);
	if (server == NULL)
		return FALSE;
	if (server[0] == '\0') {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Server not set in agent.conf");
		return FALSE;
	}

	/* export as a string */
	json_builder_root = json_builder_get_root (builder);
	json_generator = json_generator_new ();
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_builder_root);
	data = json_generator_to_data (json_generator, NULL);
	if (data == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to convert to JSON string");
		return FALSE;
	}

	/* POST request */
	uri = g_build_filename (server, endpoint, NULL);
	g_debug ("sending to %s: %s", uri, data);

	upload_response = fwupd_client_upload_bytes (priv->client, uri, data, NULL,
						     FWUPD_CLIENT_UPLOAD_FLAG_NONE,
						     priv->cancellable, error);
	if (upload_response == NULL)
		return FALSE;

	/* parse JSON reply */
	json_parser = json_parser_new ();
	str = g_strndup (g_bytes_get_data (upload_response, NULL),
			 g_bytes_get_size (upload_response));
	if (!json_parser_load_from_data (json_parser, str, -1, error)) {
		g_prefix_error (error, "Failed to parse JSON response from '%s': ", str);
		return FALSE;
	}
	json_root = json_parser_get_root (json_parser);
	if (json_root == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_PERMISSION_DENIED,
			     "JSON response was malformed: '%s'", str);
		return FALSE;
	}
	json_object = json_node_get_object (json_root);
	if (json_object == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_PERMISSION_DENIED,
			     "JSON response object was malformed: '%s'", str);
		return FALSE;
	}

	/* get any optional server message */
	if (json_object_has_member (json_object, "msg"))
		server_msg = json_object_get_string_member (json_object, "msg");

	/* server reported failed */
	if (!json_object_get_boolean_member (json_object, "success")) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_PERMISSION_DENIED,
			     "Server rejected request: %s",
			     server_msg != NULL ? server_msg : "unspecified");
		return FALSE;
	}

	/* set new approved list */
	if (json_object_has_member (json_object, "approved")) {
		JsonArray *json_array = json_object_get_array_member (json_object, "approved");
		checksums = g_new0 (gchar *, json_array_get_length (json_array) + 1);
		for (guint i = 0; i < json_array_get_length (json_array); i++) {
			JsonNode *csum = json_array_get_element (json_array, i);
			checksums[i] = json_node_dup_string (csum);
		}
	}
	if (checksums != NULL) {
		g_autofree gchar *tmp = g_strjoinv (",", checksums);
		g_debug ("setting approved firmware %s", tmp);
		if (!fwupd_client_set_approved_firmware (priv->client,
							 checksums,
							 priv->cancellable,
							 error))
			return FALSE;
	}

	/* perform action */
	if (json_object_has_member (json_object, "actions")) {
		JsonArray *json_array = json_object_get_array_member (json_object, "actions");
		for (guint i = 0; i < json_array_get_length (json_array); i++) {
			JsonObject *json_action = json_array_get_object_element (json_array, i);
			if (!fu_util_agent_run_action (priv, json_action, error))
				return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_util_add_devices_json (FuUtilPrivate *priv, JsonBuilder *builder, GError **error)
{
	g_autoptr(GPtrArray) devs = NULL;

	/* get results from daemon */
	devs = fwupd_client_get_devices (priv->client, priv->cancellable, error);
	if (devs == NULL)
		return FALSE;

	json_builder_set_member_name (builder, "Devices");
	json_builder_begin_array (builder);
	for (guint i = 0; i < devs->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devs, i);
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;

		/* add all releases that could be applied */
		rels = fwupd_client_get_releases (priv->client,
						  fwupd_device_get_id (dev),
						  priv->cancellable,
						  &error_local);
		if (rels == NULL) {
			g_debug ("not adding releases to device: %s",
				 error_local->message);
		} else {
			for (guint j = 0; j < rels->len; j++) {
				FwupdRelease *rel = g_ptr_array_index (rels, j);
				fwupd_device_add_release (dev, rel);
			}
		}

		/* add to builder */
		json_builder_begin_object (builder);
		fwupd_device_to_json (dev, builder);
		json_builder_end_object (builder);
	}
	json_builder_end_array (builder);
	return TRUE;
}

static gboolean
fu_util_add_updates_json (FuUtilPrivate *priv, JsonBuilder *builder, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	/* get devices from daemon */
	devices = fwupd_client_get_devices (priv->client, NULL, error);
	if (devices == NULL)
		return FALSE;
	json_builder_set_member_name (builder, "Devices");
	json_builder_begin_array (builder);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;

		/* not going to have results, so save a D-Bus round-trip */
		if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_SUPPORTED))
			continue;

		/* get the releases for this device and filter for validity */
		rels = fwupd_client_get_upgrades (priv->client,
						  fwupd_device_get_id (dev),
						  NULL, &error_local);
		if (rels == NULL) {
			g_debug ("no upgrades: %s", error_local->message);
			continue;
		}
		for (guint j = 0; j < rels->len; j++) {
			FwupdRelease *rel = g_ptr_array_index (rels, j);
			fwupd_device_add_release (dev, rel);
		}

		/* add to builder */
		json_builder_begin_object (builder);
		fwupd_device_to_json (dev, builder);
		json_builder_end_object (builder);
	}
	json_builder_end_array (builder);
	return TRUE;
}

static gboolean
fu_util_agent_sync (FuUtilPrivate *priv, GError **error)
{
	g_autofree gchar *machine_id = NULL;
	g_autoptr(JsonBuilder) builder = NULL;

	/* get a hash that represents the machine */
	machine_id = fwupd_build_machine_id ("fwupd", error);
	if (machine_id == NULL)
		return FALSE;

	/* create header */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "ReportVersion");
	json_builder_add_int_value (builder, 1);
	json_builder_set_member_name (builder, "MachineId");
	json_builder_add_string_value (builder, machine_id);
	if (!fu_util_add_devices_json (priv, builder, error))
		return FALSE;
	json_builder_end_object (builder);

	/* POST */
	return fu_util_agent_send (priv, "sync", builder, error);
}

static gboolean
fu_util_register (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *machine_id = NULL;
	g_autoptr(JsonBuilder) builder = NULL;

	/* check args */
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments, expected server URI");
		return FALSE;
	}

	/* set server if valid */
	if ((priv->flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		if (!g_str_has_prefix (values[0], "https://")) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "Invalid server name, https:// prefix required");
			return FALSE;
		}
	}

	/* show warning to the user */
	if ((priv->flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		g_autoptr(GString) str = g_string_new (NULL);
		g_string_append_printf (str, "%s\n\n",
					/* TRANSLATORS: we can see into your heart */
					_("All devices supporting firmware updates on your "
					  "local machine will be managed by the "
					  "administrators of the remote server."));
		g_string_append_printf (str, "%s\n\n",
					/* TRANSLATORS: your life is in someone elses hands */
					_("Updates may be scheduled without your "
					"permissions and WITHOUT WARNING."));
		/* TRANSLATORS: do you really trust this person? */
		g_string_append (str, _("You should only continue registering this machine "
					"if you are sure you know what you are doing."));
		fu_util_warning_box (str->str, 80);
		/* TRANSLATORS: letting another user manage your firmware updates? */
		g_print ("%s [Y|n]: ", _("Proceed with registration?"));
		if (!fu_util_prompt_for_boolean (TRUE)) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_PERMISSION_DENIED,
					     "User declined action");
			return FALSE;
		}
	}

	/* get a hash that represents the machine */
	machine_id = fwupd_build_machine_id ("fwupd", error);
	if (machine_id == NULL)
		return FALSE;

	/* create header */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "ReportVersion");
	json_builder_add_int_value (builder, 1);
	json_builder_set_member_name (builder, "MachineId");
	json_builder_add_string_value (builder, machine_id);
	json_builder_end_object (builder);

	/* send as POST */
	if (!fu_util_agent_set_server (priv, values[0], error))
		return FALSE;
	if (!fu_util_agent_send (priv, "register", builder, error))
		return FALSE;

#ifdef HAVE_SYSTEMD
	/* start unit */
	if (!fu_systemd_unit_enable ("fwupdagent.timer", error))
		return FALSE;
#endif

	/* send initial data */
	return fu_util_agent_sync (priv, error);
}

static gboolean
fu_util_unregister (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *machine_id = NULL;
	g_autoptr(JsonBuilder) builder = NULL;

	/* check args */
	if (g_strv_length (values) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	/* show warning to the user */
	if ((priv->flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		g_autoptr(GString) str = g_string_new (NULL);
		g_string_append_printf (str, "%s\n\n",
					/* TRANSLATORS: we can see into your heart */
					_("All devices supporting firmware updates on your "
					  "local machine will no longer be managed by the "
					  "administrators of the remote server."));
		g_string_append_printf (str, "%s\n\n",
					/* TRANSLATORS: your life is in someone elses hands */
					_("Updates will have to be approved and applied yourself."));
		fu_util_warning_box (str->str, 80);
		/* TRANSLATORS: letting another user manage your firmware updates? */
		g_print ("%s [Y|n]: ", _("Proceed with unregistration?"));
		if (!fu_util_prompt_for_boolean (TRUE)) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_PERMISSION_DENIED,
					     "User declined action");
			return FALSE;
		}
	}

	/* get a hash that represents the machine */
	machine_id = fwupd_build_machine_id ("fwupd", error);
	if (machine_id == NULL)
		return FALSE;

	/* create header */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "ReportVersion");
	json_builder_add_int_value (builder, 1);
	json_builder_set_member_name (builder, "MachineId");
	json_builder_add_string_value (builder, machine_id);
	json_builder_end_object (builder);

	/* send as POST */
	if (!fu_util_agent_send (priv, "unregister", builder, error))
		return FALSE;

#ifdef HAVE_SYSTEMD
	/* stop unit */
	if (!fu_systemd_unit_disable ("fwupdagent.timer", error))
		return FALSE;
#endif

	/* success */
	return fu_util_agent_set_server (priv, "", error);
}

static gboolean
fu_util_add_security_attributes_json (FuUtilPrivate *priv, JsonBuilder *builder, GError **error)
{
	g_autoptr(GPtrArray) attrs = NULL;

	/* not ready yet */
	if ((priv->flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "The HSI specification is not yet complete. "
				     "To ignore this warning, use --force");
		return FALSE;
	}

	/* get attrs from daemon */
	attrs = fwupd_client_get_host_security_attrs (priv->client, NULL, error);
	if (attrs == NULL)
		return FALSE;
	json_builder_set_member_name (builder, "HostSecurityAttributes");
	json_builder_begin_array (builder);
	for (guint i = 0; i < attrs->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index (attrs, i);
		json_builder_begin_object (builder);
		fwupd_security_attr_to_json (attr, builder);
		json_builder_end_object (builder);
	}
	json_builder_end_array (builder);
	return TRUE;
}

static gboolean
fu_util_sync (FuUtilPrivate *priv, gchar **values, GError **error)
{
	/* check args */
	if (g_strv_length (values) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	/* just proxy */
	return fu_util_agent_sync (priv, error);
}

static gboolean
fu_util_get_devices (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;

	/* check args */
	if (g_strv_length (values) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	/* create header */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	if (!fu_util_add_devices_json (priv, builder, error))
		return FALSE;
	json_builder_end_object (builder);

	/* export as a string */
	json_root = json_builder_get_root (builder);
	json_generator = json_generator_new ();
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_root);
	data = json_generator_to_data (json_generator, NULL);
	if (data == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to convert to JSON string");
		return FALSE;
	}

	/* just print */
	g_print ("%s\n", data);
	return TRUE;
}

static gboolean
fu_util_get_updates (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;

	/* check args */
	if (g_strv_length (values) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	/* create header */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	if (!fu_util_add_updates_json (priv, builder, error))
		return FALSE;
	json_builder_end_object (builder);

	/* export as a string */
	json_root = json_builder_get_root (builder);
	json_generator = json_generator_new ();
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_root);
	data = json_generator_to_data (json_generator, NULL);
	if (data == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to convert to JSON string");
		return FALSE;
	}

	/* just print */
	g_print ("%s\n", data);
	return TRUE;
}

static gboolean
fu_util_security (FuUtilPrivate *priv, gchar **values, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;

	/* check args */
	if (g_strv_length (values) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_ARGS,
				     "Invalid arguments");
		return FALSE;
	}

	/* create header */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	if (!fu_util_add_security_attributes_json (priv, builder, error))
		return FALSE;
	json_builder_end_object (builder);

	/* export as a string */
	json_root = json_builder_get_root (builder);
	json_generator = json_generator_new ();
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_root);
	data = json_generator_to_data (json_generator, NULL);
	if (data == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to convert to JSON string");
		return FALSE;
	}

	/* just print */
	g_print ("%s\n", data);
	return TRUE;
}

static void
fu_util_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
		   const gchar *message, gpointer user_data)
{
}

#ifdef HAVE_GIO_UNIX
static gboolean
fu_util_sigint_cb (gpointer user_data)
{
	FuUtilPrivate *priv = (FuUtilPrivate *) user_data;
	g_debug ("Handling SIGINT");
	g_cancellable_cancel (priv->cancellable);
	return FALSE;
}
#endif

static void
fu_util_private_free (FuUtilPrivate *priv)
{
	if (priv->client != NULL)
		g_object_unref (priv->client);
	g_object_unref (priv->cancellable);
	g_option_context_free (priv->context);
	g_free (priv);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUtilPrivate, fu_util_private_free)
#pragma clang diagnostic pop

int
main (int argc, char *argv[])
{
	gboolean ret;
	gboolean force = FALSE;
	gboolean verbose = FALSE;
	g_autoptr(FuUtilPrivate) priv = g_new0 (FuUtilPrivate, 1);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) cmd_array = fu_util_cmd_array_new ();
	g_autofree gchar *cmd_descriptions = NULL;
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			/* TRANSLATORS: command line option */
			_("Show extra debugging information"), NULL },
		{ "force", '\0', 0, G_OPTION_ARG_NONE, &force,
			/* TRANSLATORS: command line option */
			_("Override warnings and force the action"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* ensure D-Bus errors are registered */
	fwupd_error_quark ();

	/* create helper object */
	priv->client = fwupd_client_new ();

	/* add commands */
	fu_util_cmd_array_add (cmd_array,
			       "get-devices", NULL,
			       /* TRANSLATORS: command description */
			       _("Get all devices and possible releases"),
			       fu_util_get_devices);
	fu_util_cmd_array_add (cmd_array,
			       "get-updates,get-upgrades", NULL,
			       /* TRANSLATORS: command description */
			       _("Gets the list of updates for connected hardware"),
			       fu_util_get_updates);
	fu_util_cmd_array_add (cmd_array,
			       "security", NULL,
			       /* TRANSLATORS: command description */
			       _("Gets the host security attributes"),
			       fu_util_security);
	fu_util_cmd_array_add (cmd_array,
			       "sync", NULL,
			       /* TRANSLATORS: command description */
			       _("Sync the current system status with the server"),
			       fu_util_sync);
	fu_util_cmd_array_add (cmd_array,
			       "register", NULL,
			       /* TRANSLATORS: command description */
			       _("Register with a remote server"),
			       fu_util_register);
	fu_util_cmd_array_add (cmd_array,
			       "unregister", NULL,
			       /* TRANSLATORS: command description */
			       _("Unregister with a remote server"),
			       fu_util_unregister);

	/* sort by command name */
	fu_util_cmd_array_sort (cmd_array);

	/* do stuff on ctrl+c */
	priv->cancellable = g_cancellable_new ();
#ifdef HAVE_GIO_UNIX
	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGINT, fu_util_sigint_cb,
				priv, NULL);
#endif

	/* get a list of the commands */
	priv->context = g_option_context_new (NULL);
	cmd_descriptions = fu_util_cmd_array_to_string (cmd_array);
	g_option_context_set_summary (priv->context, cmd_descriptions);
	g_option_context_set_description (priv->context,
		/* TRANSLATORS: CLI description */
		_("This tool can be used from other tools and from shell scripts."));

	/* TRANSLATORS: program name */
	g_set_application_name (_("Firmware Agent"));
	g_option_context_add_main_entries (priv->context, options, NULL);
	ret = g_option_context_parse (priv->context, &argc, &argv, &error);
	if (!ret) {
		/* TRANSLATORS: the user didn't read the man page */
		g_print ("%s: %s\n", _("Failed to parse arguments"),
			 error->message);
		return EXIT_FAILURE;
	}

	/* set verbose? */
	if (verbose) {
		g_setenv ("G_MESSAGES_DEBUG", "all", FALSE);
	} else {
		g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
				   fu_util_ignore_cb, NULL);
	}

	/* set flags */
	if (force)
		priv->flags |= FWUPD_INSTALL_FLAG_FORCE;

	/* run the specified command */
	ret = fu_util_cmd_array_run (cmd_array, priv, argv[1], (gchar**) &argv[2], &error);
	if (!ret) {
		if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_ARGS)) {
			g_autofree gchar *tmp = NULL;
			tmp = g_option_context_get_help (priv->context, TRUE, NULL);
			g_print ("%s\n\n%s", error->message, tmp);
			return EXIT_FAILURE;
		}
		g_print ("%s\n", error->message);
		return EXIT_FAILURE;
	}

	/* success */
	return EXIT_SUCCESS;
}
