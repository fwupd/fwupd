/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <fwupdplugin.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#ifdef HAVE_GIO_UNIX
#include <glib-unix.h>
#endif
#include <fcntl.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>

#include "fwupd-client-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-jcat-file.h"
#include "fwupd-remote-private.h"

#include "fu-bios-settings-private.h"
#include "fu-cabinet.h"
#include "fu-cli-common.h"
#include "fu-console.h"
#include "fu-context-private.h"
#include "fu-debug.h"
#include "fu-device-private.h"
#include "fu-engine-cli.h"
#include "fu-engine-helper.h"
#include "fu-engine-requirements.h"
#include "fu-engine.h"
#include "fu-history.h"
#include "fu-jcat-context.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"
#include "fu-smbios-private.h"

#ifdef HAVE_SYSTEMD
#include "fu-systemd.h"
#define SYSTEMD_FWUPD_UNIT	"fwupd.service"
#define SYSTEMD_SNAP_FWUPD_UNIT "snap.fwupd.fwupd.service"
#endif

struct _FuEngineCli {
	FuCli parent_instance;
	FuContext *ctx;
	FuEngine *engine;
	FuEngineRequest *request;
	FuProgress *progress;
	gboolean prepare_blob;
	gboolean cleanup_blob;
	gchar *destdir;
	FuFirmwareParseFlags parse_flags;
	gint lock_fd;
	FuJcatContext *jcat_context;
};

G_DEFINE_TYPE(FuEngineCli, fu_engine_cli, FU_TYPE_CLI)

static gboolean
fu_engine_cli_lock(FuEngineCli *self, GError **error)
{
#ifdef HAVE_WRLCK
	struct flock lockp = {
	    .l_type = F_WRLCK,
	    .l_whence = SEEK_SET,
	};
	g_autofree gchar *lockfn = NULL;
	gboolean use_user = FALSE;

#ifdef HAVE_GETUID
	if (getuid() != 0 || geteuid() != 0)
		use_user = TRUE;
#endif

	/* open file */
	if (use_user) {
		lockfn = fu_cli_get_user_cache_path("fwupdtool");
	} else {
		lockfn = fu_context_build_filename(self->ctx,
						   error,
						   FU_PATH_KIND_LOCKDIR,
						   "fwupdtool",
						   NULL);
		if (lockfn == NULL)
			return FALSE;
	}
	if (!fu_path_mkdir_parent(lockfn, error))
		return FALSE;
	self->lock_fd = g_open(lockfn, O_RDWR | O_CREAT, S_IRWXU);
	if (self->lock_fd < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to open %s",
			    lockfn);
		return FALSE;
	}

	/* write lock */
#ifdef HAVE_OFD
	if (fcntl(self->lock_fd, F_OFD_SETLK, &lockp) < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "another instance has locked %s",
			    lockfn);
		return FALSE;
	}
#else
	if (fcntl(self->lock_fd, F_SETLK, &lockp) < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "another instance has locked %s",
			    lockfn);
		return FALSE;
	}
#endif

	/* success */
	g_debug("locked %s", lockfn);
#endif
	return TRUE;
}

#ifdef HAVE_SYSTEMD
static const gchar *
fu_engine_cli_get_systemd_unit(void)
{
	if (g_strcmp0(g_getenv("SNAP_NAME"), "fwupd") == 0)
		return SYSTEMD_SNAP_FWUPD_UNIT;
	return SYSTEMD_FWUPD_UNIT;
}
#endif

static gboolean
fu_engine_cli_start_engine(FuEngineCli *self,
			   FuEngineLoadFlags flags,
			   FuProgress *progress,
			   GError **error)
{
#ifdef HAVE_SYSTEMD
	if (getuid() != 0 || geteuid() != 0) {
		g_info("not attempting to stop daemon when running as user");
	} else {
		g_autoptr(GError) error_local = NULL;
		if (!fu_systemd_unit_stop(fu_engine_cli_get_systemd_unit(), &error_local))
			g_info("failed to stop daemon: %s", error_local->message);
	}
#endif
	flags |= FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS;
	flags |= FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS;
	flags |= FU_ENGINE_LOAD_FLAG_PATH_STORE_DEFAULTS;
	if (!fu_engine_load(self->engine, flags, progress, error))
		return FALSE;

	if (!fu_engine_cli_lock(self, error)) {
		/* TRANSLATORS: another fwupdtool instance is already running */
		g_prefix_error(error, "%s: ", _("Failed to lock"));
		return FALSE;
	}

	/* copy properties from engine to client */
	if (flags & FU_ENGINE_LOAD_FLAG_HWINFO) {
		g_object_set(fu_cli_get_client(FU_CLI(self)),
			     "host-vendor",
			     fu_engine_get_host_vendor(self->engine),
			     "host-product",
			     fu_engine_get_host_product(self->engine),
			     "battery-level",
			     fu_context_get_battery_level(fu_engine_get_context(self->engine)),
			     "battery-threshold",
			     fu_context_get_battery_threshold(fu_engine_get_context(self->engine)),
			     NULL);
	}

	/* success */
	return TRUE;
}

static void
fu_engine_cli_maybe_prefix_sandbox_error(const gchar *value, GError **error) /* nocheck:error */
{
	g_autofree gchar *path = g_path_get_dirname(value);
	if (!g_file_test(path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
		/* nocheck:error */
		g_prefix_error(error,
			       "Unable to access %s. You may need to copy %s to %s: ",
			       path,
			       value,
			       g_getenv("HOME"));
	}
}

static gboolean
fu_engine_cli_smbios_dump(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuPathStore *pstore = fu_context_get_path_store(self->ctx);
	g_autofree gchar *tmp = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	if (g_strv_length(values) < 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}
	smbios = fu_smbios_new(pstore);
	if (!fu_smbios_setup_from_file(smbios, values[0], error))
		return FALSE;
	tmp = fu_firmware_to_string(FU_FIRMWARE(smbios));
	fu_console_print_literal(console, tmp);
	return TRUE;
}

static void
fu_engine_cli_context_flags_notify_cb(FuContext *ctx, GParamSpec *pspec, FuEngineCli *self)
{
	if (fu_context_has_flag(ctx, FU_CONTEXT_FLAG_SYSTEM_INHIBIT)) {
		fu_cli_watch_sigint_start(FU_CLI(self));
	} else {
		fu_cli_watch_sigint_stop(FU_CLI(self));
	}
}

static void
fu_engine_cli_finalize(GObject *obj)
{
	FuEngineCli *self = FU_ENGINE_CLI(obj);
	g_free(self->destdir);
	if (self->ctx != NULL)
		g_object_unref(self->ctx);
	if (self->engine != NULL)
		g_object_unref(self->engine);
	if (self->request != NULL)
		g_object_unref(self->request);
	if (self->progress != NULL)
		g_object_unref(self->progress);
	if (self->jcat_context != NULL)
		g_object_unref(self->jcat_context);
	if (self->lock_fd >= 0)
		g_close(self->lock_fd, NULL);
	G_OBJECT_CLASS(fu_engine_cli_parent_class)->finalize(obj);
}

static void
fu_engine_cli_update_device_request_cb(FuEngine *engine, FwupdRequest *request, FuEngineCli *self)
{
	fwupd_client_emit_device_request(fu_cli_get_client(FU_CLI(self)), request);
}

static void
fu_engine_cli_engine_device_added_cb(FuEngine *engine, FwupdDevice *device, FuEngineCli *self)
{
	fwupd_client_emit_device_added(fu_cli_get_client(FU_CLI(self)), device);
}

static void
fu_engine_cli_engine_device_removed_cb(FuEngine *engine, FwupdDevice *device, FuEngineCli *self)
{
	fwupd_client_emit_device_removed(fu_cli_get_client(FU_CLI(self)), device);
}

static void
fu_engine_cli_engine_status_changed_cb(FuEngine *engine, FwupdStatus status, FuEngineCli *self)
{
	FuConsole *console = fu_cli_get_console(FU_CLI(self));
	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_AS_JSON))
		return;
	fu_console_set_progress(console, status, 0);
}

static void
fu_engine_cli_progress_percentage_changed_cb(FuProgress *progress,
					     gdouble percentage,
					     FuEngineCli *self)
{
	FuConsole *console = fu_cli_get_console(FU_CLI(self));
	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_AS_JSON))
		return;
	fu_console_set_progress(console, fu_progress_get_status(progress), percentage);
}

static void
fu_engine_cli_progress_status_changed_cb(FuProgress *progress,
					 FwupdStatus status,
					 FuEngineCli *self)
{
	FuConsole *console = fu_cli_get_console(FU_CLI(self));
	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_AS_JSON))
		return;
	fu_console_set_progress(console, status, fu_progress_get_percentage(progress));
}

static gint
fu_engine_cli_verfmt_sort_cb(gconstpointer a, gconstpointer b)
{
	return g_strcmp0(*(const gchar **)a, *(const gchar **)b);
}

static gboolean
fu_engine_cli_get_verfmts(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(GPtrArray) verfmts = g_ptr_array_new_with_free_func((GDestroyNotify)g_free);

	for (guint i = FWUPD_VERSION_FORMAT_PLAIN; i < FWUPD_VERSION_FORMAT_LAST; i++) {
		g_autofree gchar *format = g_strdup(fwupd_version_format_to_string(i));
		if (format == NULL)
			continue;
		g_ptr_array_add(verfmts, g_steal_pointer(&format));
	}
	g_ptr_array_sort(verfmts, (GCompareFunc)fu_engine_cli_verfmt_sort_cb);

	/* print */
	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_AS_JSON)) {
		g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();
		g_autoptr(GString) str = NULL;
		for (guint i = 0; i < verfmts->len; i++) {
			const gchar *verfmt = g_ptr_array_index(verfmts, i);
			fwupd_json_array_add_string(json_arr, verfmt);
		}
		str = fwupd_json_array_to_string(json_arr, FWUPD_JSON_EXPORT_FLAG_INDENT);
		fu_console_print_literal(console, str->str);
		return TRUE;
	}

	/* print */
	for (guint i = 0; i < verfmts->len; i++) {
		const gchar *verfmt = g_ptr_array_index(verfmts, i);
		fu_console_print_literal(console, verfmt);
	}

	return TRUE;
}

static GPtrArray *
fu_engine_cli_get_devices_with_filter(FuEngineCli *self, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	devices = fwupd_client_get_devices(fu_cli_get_client(FU_CLI(self)),
					   fu_cli_get_cancellable(FU_CLI(self)),
					   error);
	if (devices == NULL)
		return NULL;
	return fu_cli_device_array_filter(FU_CLI(self), devices, error);
}

static FwupdDevice *
fu_engine_cli_prompt_for_device(FuEngineCli *self, GPtrArray *devices_opt, GError **error)
{
	FuConsole *console = fu_cli_get_console(FU_CLI(self));
	FwupdDevice *dev;
	guint idx;
	g_autoptr(GPtrArray) devices = NULL;

	/* get devices from daemon */
	if (devices_opt != NULL) {
		devices = g_ptr_array_ref(devices_opt);
	} else {
		devices = fu_engine_cli_get_devices_with_filter(self, error);
		if (devices == NULL)
			return NULL;
	}
	fwupd_device_array_ensure_parents(devices);

	/* exactly one */
	if (devices->len == 1) {
		dev = g_ptr_array_index(devices, 0);
		if (!fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_AS_JSON)) {
			fu_console_print(
			    console,
			    "%s: %s",
			    /* TRANSLATORS: device has been chosen by the daemon for the user */
			    _("Selected device"),
			    fu_device_get_name(dev));
		}
		return g_object_ref(dev);
	}

	/* no questions */
	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_NO_DEVICE_PROMPT)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "can't prompt for devices");
		return NULL;
	}

	/* TRANSLATORS: this is to abort the interactive prompt */
	fu_console_print(console, "0.\t%s", _("Cancel"));
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *device_tmp = g_ptr_array_index(devices, i);
		g_autofree gchar *id_display = fwupd_device_get_id_display(device_tmp);
		fu_console_print(console, "%u.\t%s", i + 1, id_display);
	}

	/* TRANSLATORS: get interactive prompt */
	idx = fu_console_input_uint(console, devices->len, "%s", _("Choose device"));
	if (idx == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Request canceled");
		return NULL;
	}
	dev = g_ptr_array_index(devices, idx - 1);
	return g_object_ref(dev);
}

static FwupdDevice *
fu_engine_cli_get_device(FuEngineCli *self, const gchar *id, GError **error)
{
	if (fwupd_guid_is_valid(id)) {
		g_autoptr(GPtrArray) devices = NULL;
		devices = fwupd_client_get_devices_by_guid(fu_cli_get_client(FU_CLI(self)),
							   id,
							   fu_cli_get_cancellable(FU_CLI(self)),
							   error);
		if (devices == NULL)
			return NULL;
		return fu_engine_cli_prompt_for_device(self, devices, error);
	}

	/* did this look like a GUID? */
	for (guint i = 0; id[i] != '\0'; i++) {
		if (id[i] == '-') {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_ARGS,
					    "Invalid arguments");
			return NULL;
		}
	}
	return fwupd_client_get_device_by_id(fu_cli_get_client(FU_CLI(self)),
					     id,
					     fu_cli_get_cancellable(FU_CLI(self)),
					     error);
}

static gboolean
fu_engine_cli_get_device_flags(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	g_autoptr(GString) str = g_string_new(NULL);

	for (FwupdDeviceFlags i = FWUPD_DEVICE_FLAG_INTERNAL; i < FWUPD_DEVICE_FLAG_UNKNOWN;
	     i <<= 1) {
		const gchar *tmp = fwupd_device_flag_to_string(i);
		if (tmp == NULL)
			break;
		if (i != FWUPD_DEVICE_FLAG_INTERNAL)
			g_string_append(str, " ");
		g_string_append(str, tmp);
		g_string_append(str, " ~");
		g_string_append(str, tmp);
	}
	fu_console_print_literal(console, str->str);

	return TRUE;
}

static void
fu_engine_cli_update_device_changed_cb(FuEngine *engine, FwupdDevice *device, FuEngineCli *self)
{
	fwupd_client_emit_device_changed(fu_cli_get_client(FU_CLI(self)), device);
}

static gboolean
fu_engine_cli_install_blob(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autofree gchar *firmware_basename = NULL;
	g_autoptr(FwupdDevice) device = NULL;
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuInputStream) stream_fw = NULL;

	/* progress */
	fu_progress_set_id(self->progress, G_STRLOC);
	fu_progress_add_flag(self->progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(self->progress, FWUPD_STATUS_LOADING, 2, "parse");
	fu_progress_add_step(self->progress, FWUPD_STATUS_LOADING, 30, "start-engine");
	fu_progress_add_step(self->progress, FWUPD_STATUS_DEVICE_WRITE, 68, NULL);

	/* invalid args */
	if (g_strv_length(values) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* parse blob */
	stream_fw = fu_input_stream_from_path(values[0], error);
	if (stream_fw == NULL) {
		fu_engine_cli_maybe_prefix_sandbox_error(values[0], error);
		return FALSE;
	}
	fu_release_set_stream(release, stream_fw);
	fu_progress_step_done(self->progress);

	/* some plugins need the firmware name */
	firmware_basename = g_path_get_basename(values[0]);
	fu_release_set_firmware_basename(release, firmware_basename);

	/* load engine */
	if (!fu_engine_cli_start_engine(
		self,
		FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_DEVICE_HOTPLUG |
		    FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_HWINFO |
		    FU_ENGINE_LOAD_FLAG_HISTORY,
		fu_progress_get_child(self->progress),
		error))
		return FALSE;
	fu_progress_step_done(self->progress);

	/* get device */
	fu_cli_add_filter_device_include(cli, FWUPD_DEVICE_FLAG_UPDATABLE);
	if (g_strv_length(values) >= 2) {
		device = fu_engine_cli_get_device(self, values[1], error);
		if (device == NULL)
			return FALSE;
	} else {
		device = fu_engine_cli_prompt_for_device(self, NULL, error);
		if (device == NULL)
			return FALSE;
	}

	/* optional version */
	if (g_strv_length(values) >= 3)
		fu_release_set_version(release, values[2]);

	fu_cli_set_current_operation(FU_CLI(self), FU_CLI_OPERATION_INSTALL);
	g_signal_connect(FU_ENGINE(self->engine),
			 "device-changed",
			 G_CALLBACK(fu_engine_cli_update_device_changed_cb),
			 self);

	/* write bare firmware */
	if (self->prepare_blob) {
		g_autoptr(GPtrArray) devices = NULL;
		devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
		g_ptr_array_add(devices, g_object_ref(device));
		if (!fu_engine_composite_prepare(self->engine, devices, error)) {
			g_prefix_error_literal(error, "failed to prepare composite action: ");
			return FALSE;
		}
	}
	if (!fu_engine_install_blob(self->engine,
				    FU_DEVICE(device),
				    release,
				    fu_progress_get_child(self->progress),
				    fu_cli_get_install_flags(FU_CLI(self)) |
					FWUPD_INSTALL_FLAG_NO_HISTORY,
				    fu_engine_request_get_feature_flags(self->request),
				    error))
		return FALSE;
	fu_progress_step_done(self->progress);

	/* cleanup */
	if (self->cleanup_blob) {
		g_autoptr(FwupdDevice) device_new = NULL;
		g_autoptr(GError) error_local = NULL;

		/* get the possibly new device from the old ID */
		device_new = fu_engine_cli_get_device(self, fu_device_get_id(device), &error_local);
		if (device_new == NULL) {
			g_debug("failed to find new device: %s", error_local->message);
		} else {
			g_autoptr(GPtrArray) devices_new =
			    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
			g_ptr_array_add(devices_new, g_steal_pointer(&device_new));
			if (!fu_engine_composite_cleanup(self->engine, devices_new, error)) {
				g_prefix_error_literal(error,
						       "failed to cleanup composite action: ");
				return FALSE;
			}
		}
	}

	fu_cli_display_current_message(FU_CLI(self));

	/* success */
	return fu_cli_prompt_complete(FU_CLI(self), TRUE, error);
}

static gboolean
fu_engine_cli_firmware_sign(FuCli *cli, gchar **values, GError **error)
{
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) archive_blob_new = NULL;
	g_autoptr(GBytes) cert = NULL;
	g_autoptr(GBytes) privkey = NULL;
	g_autoptr(GFile) archive_file_old = NULL;

	/* invalid args */
	if (g_strv_length(values) != 3) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments, expected firmware.cab "
				    "certificate.pem privatekey.pfx");
		return FALSE;
	}

	/* load arguments */
	cert = fu_bytes_get_contents(values[1], error);
	if (cert == NULL)
		return FALSE;
	privkey = fu_bytes_get_contents(values[2], error);
	if (privkey == NULL)
		return FALSE;

	/* load, sign, export */
	archive_file_old = g_file_new_for_path(values[0]);
	if (!fu_firmware_parse_file(FU_FIRMWARE(cabinet),
				    archive_file_old,
				    FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
				    error))
		return FALSE;
	if (!fu_cabinet_sign(cabinet, cert, privkey, FU_CABINET_SIGN_FLAG_NONE, error))
		return FALSE;
	archive_blob_new = fu_firmware_write(FU_FIRMWARE(cabinet), error);
	if (archive_blob_new == NULL)
		return FALSE;
	return fu_bytes_set_contents(values[0], archive_blob_new, error);
}

static gboolean
fu_engine_cli_firmware_dump(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(FwupdDevice) device = NULL;
	g_autoptr(GBytes) blob_empty = g_bytes_new(NULL, 0);
	g_autoptr(GBytes) blob_fw = NULL;

	/* progress */
	fu_progress_set_id(self->progress, G_STRLOC);
	fu_progress_add_flag(self->progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(self->progress, FWUPD_STATUS_LOADING, 5, "start-engine");
	fu_progress_add_step(self->progress, FWUPD_STATUS_DEVICE_READ, 95, NULL);

	/* invalid args */
	if (g_strv_length(values) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* file already exists */
	if (!fu_cli_has_arg_flag(cli, FU_CLI_ARG_FLAG_FORCE) &&
	    g_file_test(values[0], G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Filename already exists");
		return FALSE;
	}

	/* write a zero length file to ensure the destination is writable to
	 * avoid failing at the end of a potentially lengthy operation */
	if (!fu_bytes_set_contents(values[0], blob_empty, error))
		return FALSE;

	/* load engine */
	if (!fu_engine_cli_start_engine(self,
					FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_HWINFO,
					fu_progress_get_child(self->progress),
					error))
		return FALSE;
	fu_progress_step_done(self->progress);

	/* get device */
	fu_cli_add_filter_device_include(cli, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	if (g_strv_length(values) >= 2) {
		device = fu_engine_cli_get_device(self, values[1], error);
		if (device == NULL)
			return FALSE;
	} else {
		device = fu_engine_cli_prompt_for_device(self, NULL, error);
		if (device == NULL)
			return FALSE;
	}
	fu_cli_set_current_operation(FU_CLI(self), FU_CLI_OPERATION_READ);
	g_signal_connect(FU_ENGINE(self->engine),
			 "device-changed",
			 G_CALLBACK(fu_engine_cli_update_device_changed_cb),
			 self);

	/* dump firmware */
	blob_fw = fu_engine_firmware_dump(self->engine,
					  FU_DEVICE(device),
					  fu_progress_get_child(self->progress),
					  fu_cli_get_install_flags(FU_CLI(self)),
					  error);
	if (blob_fw == NULL)
		return FALSE;
	fu_progress_step_done(self->progress);
	return fu_bytes_set_contents(values[0], blob_fw, error);
}

static gboolean
fu_engine_cli_firmware_read(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(FwupdDevice) device = NULL;
	g_autoptr(FuFirmware) fw = NULL;
	g_autoptr(GBytes) blob_empty = g_bytes_new(NULL, 0);
	g_autoptr(GBytes) blob_fw = NULL;

	/* progress */
	fu_progress_set_id(self->progress, G_STRLOC);
	fu_progress_add_flag(self->progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(self->progress, FWUPD_STATUS_LOADING, 5, "start-engine");
	fu_progress_add_step(self->progress, FWUPD_STATUS_DEVICE_READ, 95, NULL);

	/* invalid args */
	if (g_strv_length(values) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* file already exists */
	if (!fu_cli_has_arg_flag(cli, FU_CLI_ARG_FLAG_FORCE) &&
	    g_file_test(values[0], G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Filename already exists");
		return FALSE;
	}

	/* write a zero length file to ensure the destination is writable to
	 * avoid failing at the end of a potentially lengthy operation */
	if (!fu_bytes_set_contents(values[0], blob_empty, error))
		return FALSE;

	/* load engine */
	if (!fu_engine_cli_start_engine(self,
					FU_ENGINE_LOAD_FLAG_COLDPLUG |
					    FU_ENGINE_LOAD_FLAG_DEVICE_HOTPLUG |
					    FU_ENGINE_LOAD_FLAG_HWINFO,
					fu_progress_get_child(self->progress),
					error))
		return FALSE;
	fu_progress_step_done(self->progress);

	/* get device */
	fu_cli_add_filter_device_include(cli, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	if (g_strv_length(values) >= 2) {
		device = fu_engine_cli_get_device(self, values[1], error);
		if (device == NULL)
			return FALSE;
	} else {
		device = fu_engine_cli_prompt_for_device(self, NULL, error);
		if (device == NULL)
			return FALSE;
	}
	fu_cli_set_current_operation(FU_CLI(self), FU_CLI_OPERATION_READ);
	g_signal_connect(FU_ENGINE(self->engine),
			 "device-changed",
			 G_CALLBACK(fu_engine_cli_update_device_changed_cb),
			 self);

	/* read firmware into the container format */
	fw = fu_engine_firmware_read(self->engine,
				     FU_DEVICE(device),
				     fu_progress_get_child(self->progress),
				     fu_cli_get_install_flags(FU_CLI(self)),
				     error);
	if (fw == NULL)
		return FALSE;
	blob_fw = fu_firmware_write(fw, error);
	if (blob_fw == NULL)
		return FALSE;
	fu_progress_step_done(self->progress);
	return fu_bytes_set_contents(values[0], blob_fw, error);
}

static gint
fu_engine_cli_release_sort_cb(gconstpointer a, gconstpointer b)
{
	FuRelease *release1 = *((FuRelease **)a);
	FuRelease *release2 = *((FuRelease **)b);
	return fu_release_compare(release1, release2);
}

static gboolean
fu_engine_cli_install_stream(FuEngineCli *self,
			     FuInputStream *stream,
			     GPtrArray *devices,
			     FuProgress *progress,
			     GError **error)
{
	g_autoptr(FuCabinet) cabinet = NULL;
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GPtrArray) errors = NULL;
	g_autoptr(GPtrArray) releases = NULL;

	cabinet = fu_engine_build_cabinet_from_stream(self->engine, stream, error);
	if (cabinet == NULL)
		return FALSE;
	components = fu_cabinet_get_components(cabinet, error);
	if (components == NULL)
		return FALSE;

	/* for each component in the silo */
	errors = g_ptr_array_new_with_free_func((GDestroyNotify)g_error_free);
	releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index(components, i);

		/* do any devices pass the requirements */
		for (guint j = 0; j < devices->len; j++) {
			FwupdDevice *device = g_ptr_array_index(devices, j);
			g_autoptr(FuRelease) release = fu_release_new();
			g_autoptr(GError) error_local = NULL;

			/* is this component valid for the device */
			fu_release_set_device(release, FU_DEVICE(device));
			fu_release_set_request(release, self->request);
			if (!fu_engine_load_release(self->engine,
						    release,
						    cabinet,
						    component,
						    NULL,
						    fu_cli_get_install_flags(FU_CLI(self)),
						    &error_local)) {
				g_debug("loading release failed on %s:%s failed: %s",
					fu_device_get_id(device),
					xb_node_query_text(component, "id", NULL),
					error_local->message);
				g_ptr_array_add(errors, g_steal_pointer(&error_local));
				continue;
			}
			if (!fu_engine_requirements_check(self->engine,
							  release,
							  fu_cli_get_install_flags(FU_CLI(self)),
							  &error_local)) {
				g_debug("requirement on %s:%s failed: %s",
					fu_device_get_id(device),
					xb_node_query_text(component, "id", NULL),
					error_local->message);
				g_ptr_array_add(errors, g_steal_pointer(&error_local));
				continue;
			}

			/* if component should have an update message from CAB */
			fu_device_ensure_from_component(FU_DEVICE(device), component);
			fu_device_incorporate_from_component(FU_DEVICE(device), component);

			/* success */
			g_ptr_array_add(releases, g_steal_pointer(&release));
		}
	}

	/* order the install tasks by the device priority */
	g_ptr_array_sort(releases, fu_engine_cli_release_sort_cb);

	/* nothing suitable */
	if (releases->len == 0) {
		GError *error_tmp = fu_engine_error_array_get_best(errors);
		g_propagate_error(error, error_tmp);
		return FALSE;
	}

	fu_cli_set_current_operation(FU_CLI(self), FU_CLI_OPERATION_INSTALL);
	g_signal_connect(FU_ENGINE(self->engine),
			 "device-changed",
			 G_CALLBACK(fu_engine_cli_update_device_changed_cb),
			 self);

	/* install all the tasks */
	fu_progress_reset(self->progress);
	return fu_engine_install_releases(self->engine,
					  self->request,
					  releases,
					  self->progress,
					  fu_cli_get_install_flags(FU_CLI(self)),
					  error);
}

static gboolean
fu_engine_cli_detach(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(FwupdDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* progress */
	fu_progress_set_id(self->progress, G_STRLOC);
	fu_progress_add_step(self->progress, FWUPD_STATUS_LOADING, 95, "start-engine");
	fu_progress_add_step(self->progress, FWUPD_STATUS_DEVICE_BUSY, 5, NULL);

	/* load engine */
	if (!fu_engine_cli_start_engine(
		self,
		FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_DEVICE_HOTPLUG |
		    FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_HWINFO,
		fu_progress_get_child(self->progress),
		error))
		return FALSE;
	fu_progress_step_done(self->progress);

	/* get device */
	fu_cli_add_filter_device_exclude(cli, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	if (g_strv_length(values) >= 1) {
		device = fu_engine_cli_get_device(self, values[0], error);
		if (device == NULL)
			return FALSE;
	} else {
		device = fu_engine_cli_prompt_for_device(self, NULL, error);
		if (device == NULL)
			return FALSE;
	}

	/* run vfunc */
	locker = fu_device_locker_new(FU_DEVICE(device), error);
	if (locker == NULL)
		return FALSE;
	if (!fu_device_detach_full(FU_DEVICE(device), fu_progress_get_child(self->progress), error))
		return FALSE;
	fu_progress_step_done(self->progress);
	return TRUE;
}

static gboolean
fu_engine_cli_unbind_driver(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(FwupdDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* load engine */
	if (!fu_engine_cli_start_engine(
		self,
		FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_DEVICE_HOTPLUG |
		    FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_HWINFO,
		self->progress,
		error))
		return FALSE;

	/* get device */
	if (g_strv_length(values) == 1) {
		device = fu_engine_cli_get_device(self, values[0], error);
	} else {
		device = fu_engine_cli_prompt_for_device(self, NULL, error);
	}
	if (device == NULL)
		return FALSE;

	/* run vfunc */
	locker = fu_device_locker_new(FU_DEVICE(device), error);
	if (locker == NULL)
		return FALSE;
	return fu_device_unbind_driver(FU_DEVICE(device), error);
}

static gboolean
fu_engine_cli_bind_driver(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(FwupdDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* load engine */
	if (!fu_engine_cli_start_engine(
		self,
		FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_DEVICE_HOTPLUG |
		    FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_HWINFO,
		self->progress,
		error))
		return FALSE;

	/* get device */
	if (g_strv_length(values) == 3) {
		device = fu_engine_cli_get_device(self, values[2], error);
		if (device == NULL)
			return FALSE;
	} else if (g_strv_length(values) == 2) {
		device = fu_engine_cli_prompt_for_device(self, NULL, error);
		if (device == NULL)
			return FALSE;
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* run vfunc */
	locker = fu_device_locker_new(FU_DEVICE(device), error);
	if (locker == NULL)
		return FALSE;
	return fu_device_bind_driver(FU_DEVICE(device), values[0], values[1], error);
}

static gboolean
fu_engine_cli_attach(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(FwupdDevice) device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* progress */
	fu_progress_set_id(self->progress, G_STRLOC);
	fu_progress_add_step(self->progress, FWUPD_STATUS_LOADING, 95, "start-engine");
	fu_progress_add_step(self->progress, FWUPD_STATUS_DEVICE_BUSY, 5, NULL);

	/* load engine */
	if (!fu_engine_cli_start_engine(
		self,
		FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_DEVICE_HOTPLUG |
		    FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_HWINFO,
		fu_progress_get_child(self->progress),
		error))
		return FALSE;
	fu_progress_step_done(self->progress);

	/* get device */
	if (!fu_cli_has_arg_flag(cli, FU_CLI_ARG_FLAG_FORCE))
		fu_cli_add_filter_device_include(cli, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	if (g_strv_length(values) >= 1) {
		device = fu_engine_cli_get_device(self, values[0], error);
		if (device == NULL)
			return FALSE;
	} else {
		device = fu_engine_cli_prompt_for_device(self, NULL, error);
		if (device == NULL)
			return FALSE;
	}

	/* run vfunc */
	locker = fu_device_locker_new(FU_DEVICE(device), error);
	if (locker == NULL)
		return FALSE;
	if (!fu_device_attach_full(FU_DEVICE(device), fu_progress_get_child(self->progress), error))
		return FALSE;
	fu_progress_step_done(self->progress);

	/* success */
	return TRUE;
}

static void
fu_engine_cli_report_metadata_to_string(GHashTable *metadata, guint idt, GString *str)
{
	g_autoptr(GList) keys =
	    g_list_sort(g_hash_table_get_keys(metadata), (GCompareFunc)g_strcmp0);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup(metadata, key);
		fwupd_codec_string_append(str, idt, key, value);
	}
}

static gboolean
fu_engine_cli_get_report_metadata_as_json(FuEngineCli *self,
					  FwupdJsonObject *json_obj,
					  GError **error)
{
	g_autoptr(FwupdJsonArray) json_array_devices = fwupd_json_array_new();
	g_autoptr(FwupdJsonArray) json_array_plugins = fwupd_json_array_new();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GHashTable) metadata = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) plugins = NULL;

	/* daemon metadata */
	metadata = fwupd_client_get_report_metadata(fu_cli_get_client(FU_CLI(self)),
						    fu_cli_get_cancellable(FU_CLI(self)),
						    error);
	if (metadata == NULL)
		return FALSE;
	fwupd_json_object_add_object_map(json_obj, "daemon", metadata);

	/* device metadata */
	devices = fu_engine_cli_get_devices_with_filter(self, &error_local);
	if (devices == NULL)
		g_debug("ignoring no devices: %s", error_local->message);
	for (guint i = 0; devices != NULL && i < devices->len; i++) {
		FwupdDevice *device = g_ptr_array_index(devices, i);
		g_autoptr(FuDeviceLocker) locker = NULL;
		g_autoptr(GHashTable) metadata_post = NULL;
		g_autoptr(GHashTable) metadata_pre = NULL;
		g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();
		g_autoptr(FwupdJsonObject) json_object_device = fwupd_json_object_new();

		locker = fu_device_locker_new(FU_DEVICE(device), error);
		if (locker == NULL)
			return FALSE;
		metadata_pre = fu_device_report_metadata_pre(FU_DEVICE(device));
		metadata_post = fu_device_report_metadata_post(FU_DEVICE(device));
		if (metadata_pre == NULL && metadata_post == NULL)
			continue;

		if (metadata_pre != NULL) {
			g_autoptr(FwupdJsonObject) json_obj_tmp = fwupd_json_object_new();
			fwupd_json_object_add_object_map(json_obj_tmp, "pre", metadata_pre);
			fwupd_json_array_add_object(json_arr, json_obj_tmp);
		}
		if (metadata_post != NULL) {
			g_autoptr(FwupdJsonObject) json_obj_tmp = fwupd_json_object_new();
			fwupd_json_object_add_object_map(json_obj_tmp, "post", metadata_post);
			fwupd_json_array_add_object(json_arr, json_obj_tmp);
		}
		fwupd_json_object_add_array(json_object_device, fu_device_get_id(device), json_arr);
		fwupd_json_array_add_object(json_array_devices, json_object_device);
	}
	fwupd_json_object_add_array(json_obj, "devices", json_array_devices);

	/* plugin metadata */
	plugins = fwupd_client_get_plugins(fu_cli_get_client(FU_CLI(self)),
					   fu_cli_get_cancellable(FU_CLI(self)),
					   error);
	if (plugins == NULL)
		return FALSE;
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		g_autoptr(FwupdJsonObject) json_obj_tmp = fwupd_json_object_new();
		if (fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED))
			continue;
		if (fu_plugin_get_report_metadata(plugin) == NULL)
			continue;
		fwupd_json_object_add_object_map(json_obj,
						 fu_plugin_get_name(plugin),
						 fu_plugin_get_report_metadata(plugin));
		fwupd_json_array_add_object(json_array_plugins, json_obj_tmp);
	}
	fwupd_json_object_add_array(json_obj, "plugins", json_array_plugins);

	/* success */
	return TRUE;
}

static gboolean
fu_engine_cli_get_report_metadata(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuConsole *console = fu_cli_get_console(cli);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GHashTable) metadata = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) plugins = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	/* not for human consumption */
	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_AS_JSON)) {
		g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
		if (!fu_engine_cli_get_report_metadata_as_json(self, json_obj, error))
			return FALSE;
		fu_cli_print_json_object(console, json_obj);
		return TRUE;
	}

	/* daemon metadata */
	metadata = fwupd_client_get_report_metadata(fu_cli_get_client(FU_CLI(self)),
						    fu_cli_get_cancellable(FU_CLI(self)),
						    error);
	if (metadata == NULL)
		return FALSE;
	fu_engine_cli_report_metadata_to_string(metadata, 0, str);

	/* device metadata */
	devices = fu_engine_cli_get_devices_with_filter(self, &error_local);
	if (devices == NULL)
		g_debug("ignoring no devices: %s", error_local->message);
	for (guint i = 0; devices != NULL && i < devices->len; i++) {
		FwupdDevice *device = g_ptr_array_index(devices, i);
		g_autoptr(FuDeviceLocker) locker = NULL;
		g_autoptr(GHashTable) metadata_post = NULL;
		g_autoptr(GHashTable) metadata_pre = NULL;

		locker = fu_device_locker_new(FU_DEVICE(device), error);
		if (locker == NULL)
			return FALSE;
		metadata_pre = fu_device_report_metadata_pre(FU_DEVICE(device));
		metadata_post = fu_device_report_metadata_post(FU_DEVICE(device));
		if (metadata_pre != NULL || metadata_post != NULL) {
			fwupd_codec_string_append(str,
						  0,
						  FWUPD_RESULT_KEY_DEVICE_ID,
						  fu_device_get_id(device));
		}
		if (metadata_pre != NULL) {
			fwupd_codec_string_append(str, 1, "pre", "");
			fu_engine_cli_report_metadata_to_string(metadata_pre, 3, str);
		}
		if (metadata_post != NULL) {
			fwupd_codec_string_append(str, 1, "post", "");
			fu_engine_cli_report_metadata_to_string(metadata_post, 3, str);
		}
	}

	/* plugin metadata */
	plugins = fwupd_client_get_plugins(fu_cli_get_client(FU_CLI(self)),
					   fu_cli_get_cancellable(FU_CLI(self)),
					   error);
	if (plugins == NULL)
		return FALSE;
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		if (fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED))
			continue;
		if (fu_plugin_get_report_metadata(plugin) == NULL)
			continue;
		fwupd_codec_string_append(str, 1, fu_plugin_get_name(plugin), "");
		fu_engine_cli_report_metadata_to_string(fu_plugin_get_report_metadata(plugin),
							3,
							str);
	}

	/* display */
	fu_console_print_literal(console, str->str);

	/* success */
	return TRUE;
}

static gboolean
fu_engine_cli_crc(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuCrcKind kind;

	/* sanity check */
	if (g_strv_length(values) < 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments, expected KIND FILENAME [FILENAME]");
		return FALSE;
	}

	/* get kind */
	kind = fu_crc_kind_from_string(values[0]);
	if (kind == FU_CRC_KIND_UNKNOWN) {
		g_autofree gchar *str = NULL;
		g_autoptr(GPtrArray) crc_kinds = g_ptr_array_new();
		for (guint i = 1; i < FU_CRC_KIND_LAST; i++)
			g_ptr_array_add(crc_kinds, (gpointer)fu_crc_kind_to_string(i));
		str = fu_strjoin("|", crc_kinds);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_ARGS,
			    "Invalid CRC kind, expected %s",
			    str);
		return FALSE;
	}

	/* get CRC of each file */
	for (guint i = 1; values[i] != NULL; i++) {
		g_autoptr(GBytes) blob = NULL;

		blob = fu_bytes_get_contents(values[i], error);
		if (blob == NULL)
			return FALSE;
		if (fu_crc_size(kind) == 8) {
			guint8 crc = fu_crc8_bytes(kind, blob);
			fu_console_print(console, "%s: 0x%02x", values[i], crc);
		} else if (fu_crc_size(kind) == 16) {
			guint16 crc = fu_crc16_bytes(kind, blob);
			fu_console_print(console, "%s: 0x%04x", values[i], crc);
		} else if (fu_crc_size(kind) == 32) {
			guint32 crc = fu_crc32_bytes(kind, blob);
			fu_console_print(console, "%s: 0x%08x", values[i], crc);
		}
	}

	/* success */
	return TRUE;
}

static gint
fu_engine_cli_tpm_eventlog_sort_cb(gconstpointer a, gconstpointer b)
{
	FuTpmEventlogItem *item_a = *((FuTpmEventlogItem **)a);
	FuTpmEventlogItem *item_b = *((FuTpmEventlogItem **)b);
	if (fu_tpm_eventlog_item_get_pcr(item_a) > fu_tpm_eventlog_item_get_pcr(item_b))
		return 1;
	if (fu_tpm_eventlog_item_get_pcr(item_a) < fu_tpm_eventlog_item_get_pcr(item_b))
		return -1;
	return 0;
}

static gboolean
fu_engine_cli_tpm_eventlog(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	guint64 pcr = G_MAXUINT64;
	guint8 max_pcr = 0;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuFirmware) eventlog = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(GPtrArray) items = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	/* optional PCR */
	if (g_strv_length(values) > 0) {
		if (!fu_strtoull(values[0], &pcr, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
	}

	/* parse file */
	if (g_strv_length(values) > 1) {
		fn = g_strdup(values[1]);
	} else {
		fn = fu_context_build_filename(self->ctx,
					       error,
					       FU_PATH_KIND_SYSFSDIR_SECURITY,
					       "tpm0",
					       "binary_bios_measurements",
					       NULL);
		if (fn == NULL)
			return FALSE;
	}
	blob = fu_bytes_get_contents(fn, error);
	if (blob == NULL)
		return FALSE;
	stream = fu_memory_input_stream_new_from_bytes(blob);
	eventlog = fu_firmware_new_from_gtypes(stream,
					       0x0,
					       FU_FIRMWARE_PARSE_FLAG_NONE,
					       error,
					       FU_TYPE_TPM_EVENTLOG_V2,
					       FU_TYPE_TPM_EVENTLOG_V1,
					       G_TYPE_INVALID);
	if (eventlog == NULL)
		return FALSE;
	items = fu_firmware_get_images(eventlog);
	g_ptr_array_sort(items, fu_engine_cli_tpm_eventlog_sort_cb);
	for (guint i = 0; i < items->len; i++) {
		FuTpmEventlogItem *item = g_ptr_array_index(items, i);
		g_autofree gchar *tmp = NULL;
		if (fu_tpm_eventlog_item_get_pcr(item) > max_pcr)
			max_pcr = fu_tpm_eventlog_item_get_pcr(item);
		if (pcr != G_MAXUINT64 && fu_tpm_eventlog_item_get_pcr(item) != pcr)
			continue;
		tmp = fu_firmware_to_string(FU_FIRMWARE(item));
		g_string_append_printf(str, "%s", tmp);
	}
	if (pcr != G_MAXUINT64 && pcr > max_pcr) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid PCR specified: %u",
			    (guint)pcr);
		return FALSE;
	}
	fwupd_codec_string_append(str, 0, "Reconstructed PCRs", "");
	for (guint8 i = 0; i <= max_pcr; i++) {
		g_autoptr(GPtrArray) pcrs =
		    fu_tpm_eventlog_calc_checksums(FU_TPM_EVENTLOG(eventlog), i, NULL);
		if (pcrs == NULL)
			continue;
		for (guint j = 0; j < pcrs->len; j++) {
			const gchar *csum = g_ptr_array_index(pcrs, j);
			g_autofree gchar *title = NULL;
			g_autofree gchar *pretty = NULL;
			if (pcr != G_MAXUINT64 && i != (guint)pcr)
				continue;
			title = g_strdup_printf("PCR %u", i);
			pretty = fwupd_checksum_format_for_display(csum);
			fwupd_codec_string_append(str, 1, title, pretty);
		}
	}

	/* success */
	fu_console_print_literal(console, str->str);
	return TRUE;
}

static gboolean
fu_engine_cli_crc_find(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuCrcKind kind;
	guint64 crc_target = 0;
	g_autoptr(GBytes) blob = NULL;

	/* sanity check */
	if (g_strv_length(values) < 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments, expected CRC FILENAME");
		return FALSE;
	}

	/* parse CRC */
	if (!fu_strtoull(values[0], &crc_target, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
		return FALSE;

	/* find the first CRC that matches */
	blob = fu_bytes_get_contents(values[1], error);
	if (blob == NULL)
		return FALSE;
	if (!fu_crc_find(g_bytes_get_data(blob, NULL),
			 g_bytes_get_size(blob),
			 crc_target,
			 &kind,
			 error))
		return FALSE;
	fu_console_print_literal(console, fu_crc_kind_to_string(kind));

	/* success */
	return TRUE;
}

static gboolean
fu_engine_cli_vercmp(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FwupdVersionFormat verfmt = FWUPD_VERSION_FORMAT_UNKNOWN;
	gint rc;

	/* sanity check */
	if (g_strv_length(values) < 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments, expected VER1 VER2");
		return FALSE;
	}

	/* optional version format */
	if (g_strv_length(values) > 2) {
		verfmt = fwupd_version_format_from_string(values[2]);
		if (verfmt == FWUPD_VERSION_FORMAT_UNKNOWN) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Version format %s not supported",
				    values[2]);
			return FALSE;
		}
	}

	/* compare */
	rc = fu_version_compare(values[0], values[1], verfmt);
	if (rc > 0) {
		fu_console_print(console, "%s > %s", values[0], values[1]);
	} else if (rc < 0) {
		fu_console_print(console, "%s < %s", values[0], values[1]);
	} else {
		fu_console_print(console, "%s == %s", values[0], values[1]);
	}
	return TRUE;
}

static gboolean
fu_engine_cli_set_test_devices_enabled(FuEngineCli *self, gboolean enable, GError **error)
{
	return fwupd_client_modify_config(fu_cli_get_client(FU_CLI(self)),
					  "fwupd",
					  "TestDevices",
					  enable ? "true" : "false",
					  fu_cli_get_cancellable(FU_CLI(self)),
					  error);
}

static gboolean
fu_engine_cli_disable_test_devices(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);

	if (!fu_engine_cli_set_test_devices_enabled(self, FALSE, error))
		return FALSE;

	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_AS_JSON))
		return TRUE;

	/* TRANSLATORS: comment explaining result of command */
	fu_console_print_literal(console, _("Successfully disabled test devices"));

	return TRUE;
}

static gboolean
fu_engine_cli_enable_test_devices(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	gboolean found = FALSE;
	g_autoptr(GPtrArray) remotes = NULL;

	if (!fu_engine_cli_set_test_devices_enabled(self, TRUE, error))
		return FALSE;

	/* verify remote is present */
	remotes = fwupd_client_get_remotes(fu_cli_get_client(FU_CLI(self)),
					   fu_cli_get_cancellable(FU_CLI(self)),
					   error);
	if (remotes == NULL)
		return FALSE;
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(remotes, i);
		if (!fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED))
			continue;
		if (g_strcmp0(fwupd_remote_get_id(remote), "fwupd-tests") == 0) {
			found = TRUE;
			break;
		}
	}
	if (!found) {
		if (!fu_engine_cli_set_test_devices_enabled(self, FALSE, error))
			return FALSE;
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to enable fwupd-tests remote");
		return FALSE;
	}

	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_AS_JSON))
		return TRUE;

	/* TRANSLATORS: comment explaining result of command */
	fu_console_print_literal(console, _("Successfully enabled test devices"));

	return TRUE;
}

static gboolean
fu_engine_cli_export_hwids(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuContext *ctx = fu_engine_get_context(self->engine);
	FuHwids *hwids = fu_context_get_hwids(ctx);
	g_autoptr(GKeyFile) kf = g_key_file_new();
	g_autoptr(GPtrArray) hwid_keys = NULL;

	/* check args */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments, expected HWIDS-FILE");
		return FALSE;
	}

	/* setup default hwids */
	if (!fu_engine_load(self->engine,
			    FU_ENGINE_LOAD_FLAG_PATH_STORE_DEFAULTS | FU_ENGINE_LOAD_FLAG_READONLY |
				FU_ENGINE_LOAD_FLAG_HWINFO,
			    self->progress,
			    error))
		return FALSE;

	/* save all keys */
	hwid_keys = fu_hwids_get_keys(hwids);
	for (guint i = 0; i < hwid_keys->len; i++) {
		const gchar *hwid_key = g_ptr_array_index(hwid_keys, i);
		const gchar *value = fu_hwids_get_value(hwids, hwid_key);
		if (value == NULL)
			continue;
		g_key_file_set_string(kf, "fwupd", hwid_key, value);
	}

	/* success */
	return g_key_file_save_to_file(kf, values[0], error);
}

static gboolean
fu_engine_cli_self_sign(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autofree gchar *sig = NULL;

	/* check args */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: value expected");
		return FALSE;
	}

	/* start engine */
	if (!fu_engine_cli_start_engine(self,
					FU_ENGINE_LOAD_FLAG_ENSURE_CLIENT_CERT,
					self->progress,
					error))
		return FALSE;
	sig = fu_engine_self_sign(self->engine,
				  values[0],
				  FU_JCAT_SIGN_FLAG_ADD_TIMESTAMP | FU_JCAT_SIGN_FLAG_ADD_CERT,
				  error);
	if (sig == NULL)
		return FALSE;

	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_AS_JSON))
		fu_console_print(console, "{\"signature\": \"%s\"}", sig);
	else
		fu_console_print(console, "%s", sig);

	return TRUE;
}

static gboolean
fu_engine_cli_get_firmware_types(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(GPtrArray) firmware_types = NULL;

	/* load engine */
	if (!fu_engine_load(self->engine,
			    FU_ENGINE_LOAD_FLAG_PATH_STORE_DEFAULTS | FU_ENGINE_LOAD_FLAG_READONLY |
				FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS |
				FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    self->progress,
			    error))
		return FALSE;

	firmware_types = fu_context_get_firmware_gtype_ids(fu_engine_get_context(self->engine));
	for (guint i = 0; i < firmware_types->len; i++) {
		const gchar *id = g_ptr_array_index(firmware_types, i);
		fu_console_print_literal(console, id);
	}
	if (firmware_types->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: nothing found */
				    _("No firmware IDs found"));
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_engine_cli_get_firmware_gtypes(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(GArray) firmware_types = NULL;

	/* load engine */
	if (!fu_engine_load(self->engine,
			    FU_ENGINE_LOAD_FLAG_PATH_STORE_DEFAULTS | FU_ENGINE_LOAD_FLAG_READONLY |
				FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS |
				FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    self->progress,
			    error))
		return FALSE;

	firmware_types = fu_context_get_firmware_gtypes(fu_engine_get_context(self->engine));
	for (guint i = 0; i < firmware_types->len; i++) {
		GType gtype = g_array_index(firmware_types, GType, i);
		fu_console_print_literal(console, g_type_name(gtype));
	}
	if (firmware_types->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: nothing found */
				    _("No firmware found"));
		return FALSE;
	}

	return TRUE;
}

static gchar *
fu_engine_cli_prompt_for_firmware_type(FuEngineCli *self, GPtrArray *firmware_types, GError **error)
{
	FuConsole *console = fu_cli_get_console(FU_CLI(self));
	guint idx;

	/* no detected types */
	if (firmware_types->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "No detected firmware types");
		return NULL;
	}

	/* there is no point asking */
	if (firmware_types->len == 1) {
		const gchar *id = g_ptr_array_index(firmware_types, 0);
		return g_strdup(id);
	}

	/* TRANSLATORS: this is to abort the interactive prompt */
	fu_console_print(console, "0.\t%s", _("Cancel"));
	for (guint i = 0; i < firmware_types->len; i++) {
		const gchar *id = g_ptr_array_index(firmware_types, i);
		fu_console_print(console, "%u.\t%s", i + 1, id);
	}
	/* TRANSLATORS: get interactive prompt */
	idx = fu_console_input_uint(console, firmware_types->len, "%s", _("Choose firmware"));
	if (idx == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Request canceled");
		return NULL;
	}

	return g_strdup(g_ptr_array_index(firmware_types, idx - 1));
}

static gboolean
fu_engine_cli_firmware_parse(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuContext *ctx = fu_engine_get_context(self->engine);
	GType gtype;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autofree gchar *firmware_type = NULL;
	g_autofree gchar *str = NULL;

	/* check args */
	if (g_strv_length(values) == 0 || g_strv_length(values) > 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: filename required");
		return FALSE;
	}

	/* load file */
	stream = fu_input_stream_from_path(values[0], error);
	if (stream == NULL)
		return FALSE;

	/* load engine */
	if (!fu_engine_load(self->engine,
			    FU_ENGINE_LOAD_FLAG_PATH_STORE_DEFAULTS | FU_ENGINE_LOAD_FLAG_READONLY |
				FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS |
				FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    self->progress,
			    error))
		return FALSE;

	/* find the GType to use */
	if (g_strv_length(values) == 1) {
		g_autoptr(GPtrArray) firmware_types = fu_context_get_firmware_gtype_ids(ctx);
		firmware_type = fu_engine_cli_prompt_for_firmware_type(self, firmware_types, error);
		if (firmware_type == NULL)
			return FALSE;
	} else if (g_strcmp0(values[1], "auto") == 0) {
		g_autoptr(GPtrArray) gtype_ids = fu_context_get_firmware_gtype_ids(ctx);
		g_autoptr(GPtrArray) firmware_auto_types = g_ptr_array_new_with_free_func(g_free);
		for (guint i = 0; i < gtype_ids->len; i++) {
			const gchar *gtype_id = g_ptr_array_index(gtype_ids, i);
			GType gtype_tmp;
			g_autofree gchar *firmware_str = NULL;
			g_autoptr(FuFirmware) firmware_tmp = NULL;
			g_autoptr(GError) error_local = NULL;

			if (g_strcmp0(gtype_id, "raw") == 0)
				continue;
			g_debug("parsing as %s", gtype_id);
			gtype_tmp = fu_context_get_firmware_gtype_by_id(ctx, gtype_id);
			if (gtype_tmp == G_TYPE_INVALID) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "GType %s not supported",
					    gtype_id);
				return FALSE;
			}
			firmware_tmp = g_object_new(gtype_tmp, NULL);
			if (fu_firmware_has_flag(firmware_tmp, FU_FIRMWARE_FLAG_NO_AUTO_DETECTION))
				continue;
			if (!fu_firmware_parse_stream(firmware_tmp,
						      stream,
						      0x0,
						      FU_FIRMWARE_PARSE_FLAG_NO_SEARCH,
						      &error_local)) {
				g_debug("failed to parse as %s: %s",
					gtype_id,
					error_local->message);
				continue;
			}
			firmware_str = fu_firmware_to_string(firmware_tmp);
			g_debug("parsed as %s: %s", gtype_id, firmware_str);
			g_ptr_array_add(firmware_auto_types, g_strdup(gtype_id));
		}
		firmware_type =
		    fu_engine_cli_prompt_for_firmware_type(self, firmware_auto_types, error);
		if (firmware_type == NULL)
			return FALSE;
	} else {
		firmware_type = g_strdup(values[1]);
	}
	gtype = fu_context_get_firmware_gtype_by_id(ctx, firmware_type);
	if (gtype == G_TYPE_INVALID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "GType %s not supported",
			    firmware_type);
		return FALSE;
	}

	/* match the behavior of the daemon as we're printing the children */
	self->parse_flags |= FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM;

	/* does firmware specify an internal size */
	firmware = g_object_new(gtype, NULL);
	if (fu_firmware_has_flag(firmware, FU_FIRMWARE_FLAG_ALLOW_LINEAR)) {
		g_autoptr(FuFirmware) firmware_linear = fu_linear_firmware_new(gtype);
		g_autoptr(GPtrArray) imgs = NULL;
		if (!fu_firmware_parse_stream(firmware_linear,
					      stream,
					      0x0,
					      self->parse_flags,
					      error))
			return FALSE;
		imgs = fu_firmware_get_images(firmware_linear);
		if (imgs->len == 1) {
			g_set_object(&firmware, g_ptr_array_index(imgs, 0));
		} else {
			g_set_object(&firmware, firmware_linear);
		}
	} else {
		if (!fu_firmware_parse_stream(firmware, stream, 0x0, self->parse_flags, error))
			return FALSE;
	}

	str = fu_firmware_to_string(firmware);
	fu_console_print_literal(console, str);
	return TRUE;
}

static gboolean
fu_engine_cli_firmware_export(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuContext *ctx = fu_engine_get_context(self->engine);
	FuFirmwareExportFlags flags = FU_FIRMWARE_EXPORT_FLAG_NONE;
	GType gtype;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;
	g_autofree gchar *firmware_type = NULL;
	g_autofree gchar *str = NULL;

	/* check args */
	if (g_strv_length(values) == 0 || g_strv_length(values) > 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: filename required");
		return FALSE;
	}

	if (g_strv_length(values) == 2)
		firmware_type = g_strdup(values[1]);

	/* load engine */
	if (!fu_engine_load(self->engine,
			    FU_ENGINE_LOAD_FLAG_PATH_STORE_DEFAULTS | FU_ENGINE_LOAD_FLAG_READONLY |
				FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS |
				FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    self->progress,
			    error))
		return FALSE;

	/* find the GType to use */
	if (firmware_type == NULL) {
		g_autoptr(GPtrArray) firmware_types = fu_context_get_firmware_gtype_ids(ctx);
		firmware_type = fu_engine_cli_prompt_for_firmware_type(self, firmware_types, error);
	}
	if (firmware_type == NULL)
		return FALSE;
	gtype = fu_context_get_firmware_gtype_by_id(ctx, firmware_type);
	if (gtype == G_TYPE_INVALID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "GType %s not supported",
			    firmware_type);
		return FALSE;
	}
	firmware = g_object_new(gtype, NULL);
	file = g_file_new_for_path(values[0]);
	if (!fu_firmware_parse_file(firmware, file, self->parse_flags, error))
		return FALSE;
	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_SHOW_ALL))
		flags |= FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG;
	str = fu_firmware_export_to_xml(firmware, flags | FU_FIRMWARE_EXPORT_FLAG_SORTED, error);
	if (str == NULL)
		return FALSE;
	fu_console_print_literal(console, str);
	return TRUE;
}

static gboolean
fu_engine_cli_firmware_extract_image(FuEngineCli *self,
				     FuFirmware *firmware,
				     const gchar *idxstr,
				     GError **error)
{
	FuConsole *console = fu_cli_get_console(FU_CLI(self));
	g_autofree gchar *fn = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* get raw image without generated header, footer or crc */
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL) {
		g_prefix_error(error,
			       "failed to get bytes for image %s: ",
			       fu_firmware_get_id(firmware));
		return FALSE;
	}
	if (g_bytes_get_size(blob) == 0)
		return TRUE;

	/* use suitable filename */
	if (fu_firmware_get_filename(firmware) != NULL) {
		fn = g_strdup(fu_firmware_get_filename(firmware));
	} else if (fu_firmware_get_id(firmware) != NULL) {
		fn = g_strdup_printf("id-%s.fw", fu_firmware_get_id(firmware));
	} else if (fu_firmware_get_idx(firmware) != 0x0) {
		fn = g_strdup_printf("idx-0x%x.fw", (guint)fu_firmware_get_idx(firmware));
	} else {
		fn = g_strdup_printf("img-%s.fw", idxstr);
	}
	if (!fu_path_verify_safe(fn, error))
		return FALSE;

	/* TRANSLATORS: decompressing images from a container firmware */
	fu_console_print(console, "%s : %s", _("Writing file:"), fn);
	return fu_bytes_set_contents(fn, blob, error);
}

static gboolean
fu_engine_cli_firmware_extract_images(FuEngineCli *self,
				      FuFirmware *firmware,
				      const gchar *idxstr,
				      GError **error)
{
	g_autoptr(GPtrArray) images = NULL;

	images = fu_firmware_get_images(firmware);
	if (images->len == 0)
		return fu_engine_cli_firmware_extract_image(self, firmware, idxstr, error);
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autofree gchar *idxstr_new = idxstr != NULL
						   ? g_strdup_printf("%s:0x%x", idxstr, i)
						   : g_strdup_printf("0x%x", i);
		if (!fu_engine_cli_firmware_extract_images(self, img, idxstr_new, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_cli_firmware_extract(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuContext *ctx = fu_engine_get_context(self->engine);
	GType gtype;
	g_autofree gchar *firmware_type = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length(values) == 0 || g_strv_length(values) > 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: filename required");
		return FALSE;
	}
	if (g_strv_length(values) == 2)
		firmware_type = g_strdup(values[1]);

	/* load engine */
	if (!fu_engine_load(self->engine,
			    FU_ENGINE_LOAD_FLAG_PATH_STORE_DEFAULTS | FU_ENGINE_LOAD_FLAG_READONLY |
				FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS |
				FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    self->progress,
			    error))
		return FALSE;

	/* find the GType to use */
	if (firmware_type == NULL) {
		g_autoptr(GPtrArray) firmware_types = fu_context_get_firmware_gtype_ids(ctx);
		firmware_type = fu_engine_cli_prompt_for_firmware_type(self, firmware_types, error);
	}
	if (firmware_type == NULL)
		return FALSE;
	gtype = fu_context_get_firmware_gtype_by_id(ctx, firmware_type);
	if (gtype == G_TYPE_INVALID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "GType %s not supported",
			    firmware_type);
		return FALSE;
	}
	firmware = g_object_new(gtype, NULL);
	file = g_file_new_for_path(values[0]);
	if (!fu_firmware_parse_file(firmware, file, self->parse_flags, error))
		return FALSE;
	str = fu_firmware_to_string(firmware);
	fu_console_print_literal(console, str);
	return fu_engine_cli_firmware_extract_images(self, firmware, NULL, error);
}

static gboolean
fu_engine_cli_firmware_build(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	GType gtype = FU_TYPE_FIRMWARE;
	const gchar *tmp;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) firmware_dst = NULL;
	g_autoptr(GBytes) blob_dst = NULL;
	g_autoptr(GBytes) blob_src = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbNode) n = NULL;
	g_autoptr(XbSilo) silo = NULL;

	/* check args */
	if (g_strv_length(values) != 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: filename required");
		return FALSE;
	}

	/* load file */
	blob_src = fu_bytes_get_contents(values[0], error);
	if (blob_src == NULL)
		return FALSE;

	/* load engine */
	if (!fu_engine_load(self->engine,
			    FU_ENGINE_LOAD_FLAG_PATH_STORE_DEFAULTS | FU_ENGINE_LOAD_FLAG_READONLY |
				FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS |
				FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    self->progress,
			    error))
		return FALSE;

	/* parse XML */
	if (!xb_builder_source_load_bytes(source, blob_src, XB_BUILDER_SOURCE_FLAG_NONE, error)) {
		g_prefix_error_literal(error, "could not parse XML: ");
		fwupd_error_convert(error);
		return FALSE;
	}
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}

	/* create FuFirmware of specific GType */
	n = xb_silo_query_first(silo, "firmware", error);
	if (n == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}
	tmp = xb_node_get_attr(n, "gtype");
	if (tmp != NULL) {
		gtype = g_type_from_name(tmp);
		if (gtype == G_TYPE_INVALID) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "GType %s not registered",
				    tmp);
			return FALSE;
		}
	}
	tmp = xb_node_get_attr(n, "id");
	if (tmp != NULL) {
		gtype =
		    fu_context_get_firmware_gtype_by_id(fu_engine_get_context(self->engine), tmp);
		if (gtype == G_TYPE_INVALID) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "GType %s not supported",
				    tmp);
			return FALSE;
		}
	}
	firmware = g_object_new(gtype, NULL);
	if (!fu_firmware_build(firmware, n, error))
		return FALSE;

	/* write new file */
	blob_dst = fu_firmware_write(firmware, error);
	if (blob_dst == NULL)
		return FALSE;
	if (!fu_bytes_set_contents(values[1], blob_dst, error))
		return FALSE;

	/* show what we wrote */
	firmware_dst = g_object_new(gtype, NULL);
	if (!fu_firmware_parse_bytes(firmware_dst, blob_dst, 0x0, self->parse_flags, error))
		return FALSE;
	str = fu_firmware_to_string(firmware_dst);
	fu_console_print_literal(console, str);

	/* success */
	return TRUE;
}

static gboolean
fu_engine_cli_firmware_convert(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuContext *ctx = fu_engine_get_context(self->engine);
	GType gtype_dst;
	GType gtype_src;
	g_autofree gchar *firmware_type_dst = NULL;
	g_autofree gchar *firmware_type_src = NULL;
	g_autofree gchar *str_dst = NULL;
	g_autofree gchar *str_src = NULL;
	g_autoptr(FuFirmware) firmware_dst = NULL;
	g_autoptr(FuFirmware) firmware_src = NULL;
	g_autoptr(GBytes) blob_dst = NULL;
	g_autoptr(GFile) file_src = NULL;
	g_autoptr(GPtrArray) images = NULL;

	/* check args */
	if (g_strv_length(values) < 2 || g_strv_length(values) > 4) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: filename required");
		return FALSE;
	}

	if (g_strv_length(values) > 2)
		firmware_type_src = g_strdup(values[2]);
	if (g_strv_length(values) > 3)
		firmware_type_dst = g_strdup(values[3]);

	/* load engine */
	if (!fu_engine_load(self->engine,
			    FU_ENGINE_LOAD_FLAG_PATH_STORE_DEFAULTS | FU_ENGINE_LOAD_FLAG_READONLY |
				FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS |
				FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    self->progress,
			    error))
		return FALSE;

	/* find the GType to use */
	if (firmware_type_src == NULL) {
		g_autoptr(GPtrArray) firmware_types = fu_context_get_firmware_gtype_ids(ctx);
		firmware_type_src =
		    fu_engine_cli_prompt_for_firmware_type(self, firmware_types, error);
	}
	if (firmware_type_src == NULL)
		return FALSE;
	if (firmware_type_dst == NULL) {
		g_autoptr(GPtrArray) firmware_types = fu_context_get_firmware_gtype_ids(ctx);
		firmware_type_dst =
		    fu_engine_cli_prompt_for_firmware_type(self, firmware_types, error);
	}
	if (firmware_type_dst == NULL)
		return FALSE;

	/* this makes no sense */
	if (g_strcmp0(firmware_type_src, firmware_type_dst) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Source and destination types were the same");
		return FALSE;
	}

	gtype_src = fu_context_get_firmware_gtype_by_id(ctx, firmware_type_src);
	if (gtype_src == G_TYPE_INVALID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "GType %s not supported",
			    firmware_type_src);
		return FALSE;
	}
	firmware_src = g_object_new(gtype_src, NULL);
	file_src = g_file_new_for_path(values[0]);
	if (!fu_firmware_parse_file(firmware_src, file_src, self->parse_flags, error))
		return FALSE;
	gtype_dst = fu_context_get_firmware_gtype_by_id(ctx, firmware_type_dst);
	if (gtype_dst == G_TYPE_INVALID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "GType %s not supported",
			    firmware_type_dst);
		return FALSE;
	}
	str_src = fu_firmware_to_string(firmware_src);
	fu_console_print_literal(console, str_src);

	/* copy images */
	firmware_dst = g_object_new(gtype_dst, NULL);
	images = fu_firmware_get_images(firmware_src);
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		if (!fu_firmware_add_image(firmware_dst, img, error))
			return FALSE;
	}

	/* copy data as fallback, preferring a binary blob to the export */
	if (images->len == 0) {
		g_autoptr(GBytes) fw = NULL;

		fw = fu_firmware_get_bytes(firmware_src, NULL);
		if (fw == NULL) {
			fw = fu_firmware_write(firmware_src, error);
			if (fw == NULL)
				return FALSE;
		}
		fu_firmware_set_bytes(firmware_dst, fw);
	}

	/* write new file */
	blob_dst = fu_firmware_write(firmware_dst, error);
	if (blob_dst == NULL)
		return FALSE;
	if (!fu_bytes_set_contents(values[1], blob_dst, error))
		return FALSE;
	str_dst = fu_firmware_to_string(firmware_dst);
	fu_console_print_literal(console, str_dst);

	/* success */
	return TRUE;
}

static GBytes *
fu_engine_cli_hex_string_to_bytes(const gchar *val, GError **error)
{
	gsize valsz;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* sanity check */
	if (val == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "nothing to parse");
		return NULL;
	}

	/* parse each hex byte */
	valsz = strlen(val);
	for (guint i = 0; i < valsz; i += 2) {
		guint8 tmp = 0;
		if (!fu_firmware_strparse_uint8_safe(val, valsz, i, &tmp, error))
			return NULL;
		fu_byte_array_append_uint8(buf, tmp);
	}
	return g_bytes_new(buf->data, buf->len);
}

static gboolean
fu_engine_cli_firmware_patch(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuContext *ctx = fu_engine_get_context(self->engine);
	GType gtype;
	g_autofree gchar *firmware_type = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) blob_dst = NULL;
	g_autoptr(GBytes) patch = NULL;
	g_autoptr(GFile) file_src = NULL;
	guint64 offset = 0;

	/* check args */
	if (g_strv_length(values) != 3 && g_strv_length(values) != 4) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_ARGS,
			    "Invalid arguments, expected %s",
			    "FILENAME OFFSET DATA [FIRMWARE-TYPE]");
		return FALSE;
	}

	/* hardcoded */
	if (g_strv_length(values) == 4)
		firmware_type = g_strdup(values[3]);

	/* parse offset */
	if (!fu_strtoull(values[1], &offset, 0x0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error)) {
		g_prefix_error_literal(error, "failed to parse offset: ");
		return FALSE;
	}

	/* parse blob */
	patch = fu_engine_cli_hex_string_to_bytes(values[2], error);
	if (patch == NULL)
		return FALSE;
	if (g_bytes_get_size(patch) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "no data provided");
		return FALSE;
	}

	/* load engine */
	if (!fu_engine_load(self->engine,
			    FU_ENGINE_LOAD_FLAG_PATH_STORE_DEFAULTS | FU_ENGINE_LOAD_FLAG_READONLY |
				FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS |
				FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS,
			    self->progress,
			    error))
		return FALSE;

	/* find the GType to use */
	if (firmware_type == NULL) {
		g_autoptr(GPtrArray) firmware_types = fu_context_get_firmware_gtype_ids(ctx);
		firmware_type = fu_engine_cli_prompt_for_firmware_type(self, firmware_types, error);
	}
	if (firmware_type == NULL)
		return FALSE;
	gtype = fu_context_get_firmware_gtype_by_id(ctx, firmware_type);
	if (gtype == G_TYPE_INVALID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "GType %s not supported",
			    firmware_type);
		return FALSE;
	}
	firmware = g_object_new(gtype, NULL);
	file_src = g_file_new_for_path(values[0]);
	if (!fu_firmware_parse_file(firmware, file_src, self->parse_flags, error))
		return FALSE;

	/* add patch */
	fu_firmware_add_patch(firmware, offset, patch);

	/* write new file */
	blob_dst = fu_firmware_write(firmware, error);
	if (blob_dst == NULL)
		return FALSE;
	if (!fu_bytes_set_contents(values[0], blob_dst, error))
		return FALSE;
	str = fu_firmware_to_string(firmware);
	fu_console_print_literal(console, str);

	/* success */
	return TRUE;
}

static FuVolume *
fu_engine_cli_prompt_for_volume(FuEngineCli *self, GError **error)
{
	FuConsole *console = fu_cli_get_console(FU_CLI(self));
	FuContext *ctx = fu_engine_get_context(self->engine);
	FuVolume *volume;
	guint idx;
	g_autoptr(GPtrArray) volumes = NULL;

	/* exactly one */
	volumes = fu_context_get_esp_volumes(ctx, error);
	if (volumes == NULL)
		return NULL;
	if (volumes->len == 1) {
		volume = g_ptr_array_index(volumes, 0);
		if (fu_volume_get_id(volume) != NULL) {
			fu_console_print(console,
					 "%s: %s",
					 /* TRANSLATORS: Volume has been chosen by the user */
					 _("Selected volume"),
					 fu_volume_get_id(volume));
		}
		return g_object_ref(volume);
	}

	/* TRANSLATORS: this is to abort the interactive prompt */
	fu_console_print(console, "0.\t%s", _("Cancel"));
	for (guint i = 0; i < volumes->len; i++) {
		volume = g_ptr_array_index(volumes, i);
		fu_console_print(console, "%u.\t%s", i + 1, fu_volume_get_id(volume));
	}
	/* TRANSLATORS: get interactive prompt */
	idx = fu_console_input_uint(console, volumes->len, "%s", _("Choose volume"));
	if (idx == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Request canceled");
		return NULL;
	}
	volume = g_ptr_array_index(volumes, idx - 1);
	return g_object_ref(volume);
}

static gboolean
fu_engine_cli_esp_mount(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(FuVolume) volume = NULL;
	volume = fu_engine_cli_prompt_for_volume(self, error);
	if (volume == NULL)
		return FALSE;
	return fu_volume_mount(volume, error);
}

static gboolean
fu_engine_cli_esp_unmount(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(FuVolume) volume = NULL;
	volume = fu_engine_cli_prompt_for_volume(self, error);
	if (volume == NULL)
		return FALSE;
	return fu_volume_unmount(volume, error);
}

static gboolean
fu_engine_cli_esp_list_as_json(FuEngineCli *self, GError **error)
{
	FuConsole *console = fu_cli_get_console(FU_CLI(self));
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(GPtrArray) volumes = NULL;

	volumes = fu_context_get_esp_volumes(fu_engine_get_context(self->engine), error);
	if (volumes == NULL)
		return FALSE;
	fwupd_codec_array_to_json(volumes, "Volumes", json_obj, FWUPD_CODEC_FLAG_TRUSTED);
	fu_cli_print_json_object(console, json_obj);
	return TRUE;
}

static gboolean
fu_engine_cli_esp_list(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autofree gchar *mount_point = NULL;
	g_autoptr(FuVolumeLocker) locker = NULL;
	g_autoptr(FuVolume) volume = NULL;
	g_autoptr(GPtrArray) files = NULL;

	if (!fu_engine_cli_start_engine(self,
					FU_ENGINE_LOAD_FLAG_HWINFO | FU_ENGINE_LOAD_FLAG_NO_CACHE,
					self->progress,
					error))
		return FALSE;
	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_AS_JSON))
		return fu_engine_cli_esp_list_as_json(self, error);

	volume = fu_engine_cli_prompt_for_volume(self, error);
	if (volume == NULL)
		return FALSE;
	locker = fu_volume_locker_new(volume, error);
	if (locker == NULL)
		return FALSE;
	mount_point = fu_volume_get_mount_point(volume);
	if (mount_point == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no mountpoint for ESP");
		return FALSE;
	}
	files = fu_path_get_files(mount_point, error);
	if (files == NULL)
		return FALSE;
	for (guint i = 0; i < files->len; i++) {
		const gchar *fn = g_ptr_array_index(files, i);
		fu_console_print_literal(console, fn);
	}
	return TRUE;
}

static gboolean
fu_engine_cli_reboot_cleanup(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuPlugin *plugin;
	g_autoptr(FwupdDevice) device = NULL;

	if (!fu_engine_cli_start_engine(self,
					FU_ENGINE_LOAD_FLAG_COLDPLUG |
					    FU_ENGINE_LOAD_FLAG_DEVICE_HOTPLUG |
					    FU_ENGINE_LOAD_FLAG_HWINFO,
					self->progress,
					error))
		return FALSE;

	/* both arguments are optional */
	if (g_strv_length(values) >= 1) {
		device = fu_engine_cli_get_device(self, values[0], error);
		if (device == NULL)
			return FALSE;
	} else {
		device = fu_engine_cli_prompt_for_device(self, NULL, error);
		if (device == NULL)
			return FALSE;
	}
	plugin = fu_engine_get_plugin_by_name(self->engine, fu_device_get_plugin(device), error);
	if (plugin == NULL)
		return FALSE;
	return fu_plugin_runner_reboot_cleanup(plugin, FU_DEVICE(device), error);
}

static void
fu_engine_cli_efiboot_info_as_json(FuEngineCli *self, GPtrArray *entries)
{
	FuConsole *console = fu_cli_get_console(FU_CLI(self));
	FuEfivars *efivars = fu_context_get_efivars(self->ctx);
	guint16 idx = 0;
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();

	if (fu_efivars_get_boot_current(efivars, &idx, NULL))
		fwupd_json_object_add_integer(json_obj, "BootCurrent", idx);
	if (fu_efivars_get_boot_next(efivars, &idx, NULL))
		fwupd_json_object_add_integer(json_obj, "BootNext", idx);

	for (guint i = 0; i < entries->len; i++) {
		FuEfiLoadOption *entry = g_ptr_array_index(entries, i);
		g_autoptr(FwupdJsonObject) json_entry = fwupd_json_object_new();
		g_autoptr(FwupdJsonObject) json_obj_tmp = fwupd_json_object_new();
		g_autofree gchar *title =
		    g_strdup_printf("Boot%04X", (guint)fu_firmware_get_idx(FU_FIRMWARE(entry)));
		fwupd_codec_to_json(FWUPD_CODEC(entry), json_obj_tmp, FWUPD_CODEC_FLAG_TRUSTED);
		fwupd_json_object_add_object(json_entry, title, json_obj_tmp);
		fwupd_json_array_add_object(json_arr, json_entry);
	}
	fwupd_json_object_add_array(json_obj, "Entries", json_arr);

	fu_cli_print_json_object(console, json_obj);
}

static gboolean
fu_engine_cli_efiboot_next(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuEfivars *efivars = fu_context_get_efivars(self->ctx);
	guint64 value = 0;

	/* just show */
	if (values[0] == NULL) {
		guint16 idx = 0;
		if (!fu_efivars_get_boot_next(efivars, &idx, error))
			return FALSE;
		fu_console_print(console, "Boot%04X", idx);
		return TRUE;
	}

	/* modify */
	if (!fu_strtoull(values[0], &value, 0x0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
		return FALSE;
	return fu_efivars_set_boot_next(efivars, (guint16)value, error);
}

static gboolean
fu_engine_cli_efiboot_order(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuEfivars *efivars = fu_context_get_efivars(self->ctx);
	g_auto(GStrv) split = NULL;
	g_autoptr(GArray) order = NULL;

	/* just show */
	if (values[0] == NULL) {
		order = fu_efivars_get_boot_order(efivars, error);
		if (order == NULL)
			return FALSE;
		for (guint i = 0; i < order->len; i++) {
			guint16 idx = g_array_index(order, guint16, i);
			fu_console_print(console, "Boot%04X", idx);
		}
		return TRUE;
	}

	/* modify */
	order = g_array_new(FALSE, FALSE, sizeof(guint16));
	split = g_strsplit(values[0], ",", -1);
	for (guint i = 0; split[i] != NULL; i++) {
		guint64 value = 0;
		guint16 value_as_u16;
		if (!fu_strtoull(split[i], &value, 0x0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
			return FALSE;
		value_as_u16 = (guint16)value;
		g_array_append_val(order, value_as_u16);
	}
	return fu_efivars_set_boot_order(efivars, order, error);
}

static gboolean
fu_engine_cli_efiboot_create(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuEfivars *efivars = fu_context_get_efivars(self->ctx);
	g_autoptr(FuVolume) volume = NULL;
	guint64 idx = 0;

	/* check args */
	if (g_strv_length(values) < 3) {
		g_set_error_literal(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOTHING_TO_DO,
		    /* TRANSLATORS: error message */
		    _("Invalid arguments, expected INDEX NAME TARGET [MOUNTPOINT]"));
		return FALSE;
	}

	/* check the index does not already exist */
	if (!fu_strtoull(values[0], &idx, 0x0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
		return FALSE;
	if (!fu_cli_has_arg_flag(cli, FU_CLI_ARG_FLAG_FORCE)) {
		g_autoptr(GBytes) blob = fu_efivars_get_boot_data(efivars, (guint16)idx, NULL);
		if (blob != NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOTHING_TO_DO,
					    /* TRANSLATORS: error message */
					    _("Already exists, and no --force specified"));
			return FALSE;
		}
	}

	/* get volume */
	if (values[3] == NULL) {
		volume = fu_engine_cli_prompt_for_volume(self, error);
		if (volume == NULL)
			return FALSE;
	} else {
		g_autoptr(GPtrArray) volumes = NULL;
		volumes = fu_context_get_esp_volumes(self->ctx, error);
		if (volumes == NULL)
			return FALSE;
		for (guint i = 0; i < volumes->len; i++) {
			FuVolume *volume_tmp = g_ptr_array_index(volumes, i);
			g_autofree gchar *mount_point = fu_volume_get_mount_point(volume_tmp);
			if (g_strcmp0(mount_point, values[3]) == 0) {
				volume = g_object_ref(volume_tmp);
				break;
			}
		}
		if (volume == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    /* TRANSLATORS: error message */
				    _("No volume matched %s"),
				    values[3]);
			return FALSE;
		}
	}
	return fu_efivars_create_boot_entry_for_volume(efivars,
						       (guint16)idx,
						       volume,
						       values[1],
						       values[2],
						       error);
}

static gboolean
fu_engine_cli_efiboot_delete(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuEfivars *efivars = fu_context_get_efivars(self->ctx);
	guint64 value = 0;

	if (values[0] == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: error message */
				    _("Invalid arguments, expected base-16 integer"));
		return FALSE;
	}
	if (!fu_strtoull(values[0], &value, 0x0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
		return FALSE;

	/* success */
	return fu_efivars_set_boot_data(efivars, (guint16)value, NULL, error);
}

static gboolean
fu_engine_cli_efiboot_hive_check_loadopt_is_shim(FuEfiLoadOption *loadopt, GError **error)
{
	gboolean seen_shim = FALSE;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GPtrArray) dps = NULL;

	/* get FuEfiDevicePathList */
	firmware = fu_firmware_get_image_by_idx(FU_FIRMWARE(loadopt), 0x0, error);
	if (firmware == NULL)
		return FALSE;
	dps = fu_firmware_get_images(firmware);
	for (guint i = 0; i < dps->len; i++) {
		FuFirmware *dp = g_ptr_array_index(dps, i);
		if (FU_IS_EFI_FILE_PATH_DEVICE_PATH(dp)) {
			g_autofree gchar *name =
			    fu_efi_file_path_device_path_get_name(FU_EFI_FILE_PATH_DEVICE_PATH(dp),
								  error);
			if (name == NULL)
				return FALSE;
			if (g_pattern_match_simple("*shim*.efi", name)) {
				seen_shim = TRUE;
				break;
			}
		}
	}
	if (!seen_shim) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Only the shim bootloader supports the hive format");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_cli_efiboot_hive(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuEfivars *efivars = fu_context_get_efivars(self->ctx);
	g_autoptr(FuEfiLoadOption) loadopt = NULL;
	guint64 idx = 0;

	/* check args */
	if (g_strv_length(values) < 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: error message */
				    _("Invalid arguments, expected INDEX KEY [VALUE]"));
		return FALSE;
	}

	/* load the boot entry */
	if (!fu_strtoull(values[0], &idx, 0x0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
		return FALSE;
	loadopt = fu_efivars_get_boot_entry(efivars, (guint16)idx, error);
	if (loadopt == NULL)
		return FALSE;

	/* get value */
	if (values[2] == NULL) {
		const gchar *value;
		fu_console_print_full(console,
				      FU_CONSOLE_PRINT_FLAG_WARNING,
				      "%s\n",
				      /* TRANSLATORS: try to treat the legacy format as a hive */
				      _("The EFI boot entry was not in hive format, falling back"));
		value = fu_efi_load_option_get_metadata(loadopt, values[1], error);
		if (value == NULL)
			return FALSE;
		fu_console_print_literal(console, value);
		return TRUE;
	}

	/* check this is actually shim */
	if (!fu_engine_cli_efiboot_hive_check_loadopt_is_shim(loadopt, error))
		return FALSE;

	/* change the format if required */
	if (fu_efi_load_option_get_kind(loadopt) != FU_EFI_LOAD_OPTION_KIND_HIVE) {
		fu_console_print_full(console,
				      FU_CONSOLE_PRINT_FLAG_WARNING,
				      "%s\n",
				      /* TRANSLATORS: the boot entry was in a legacy format */
				      _("The EFI boot entry is not in hive format, "
					"and shim may not be new enough to read it."));
		if (!fu_cli_has_arg_flag(cli, FU_CLI_ARG_FLAG_FORCE) &&
		    !fu_console_input_bool(console,
					   FALSE,
					   "%s",
					   /* TRANSLATORS: ask the user if it's okay to convert,
					    * "it" being the data contained in the EFI boot entry */
					   _("Do you want to convert it now?"))) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_PERMISSION_DENIED,
					    "User declined action");
			return FALSE;
		}
		fu_efi_load_option_set_kind(loadopt, FU_EFI_LOAD_OPTION_KIND_HIVE);
	}

	/* set value */
	fu_efi_load_option_set_metadata(loadopt, values[1], values[2]);
	return fu_efivars_set_boot_entry(efivars, (guint16)idx, loadopt, error);
}

static gboolean
fu_engine_cli_efiboot_info(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuEfivars *efivars = fu_context_get_efivars(self->ctx);
	g_autoptr(GPtrArray) entries = NULL;
	g_autoptr(GString) str = g_string_new(NULL);
	guint16 idx = 0;

	entries = fu_efivars_get_boot_entries(efivars, error);
	if (entries == NULL)
		return FALSE;

	/* dump to the screen in the most appropriate format */
	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_engine_cli_efiboot_info_as_json(self, entries);
		return TRUE;
	}

	if (fu_efivars_get_boot_current(efivars, &idx, NULL))
		fwupd_codec_string_append_hex(str, 0, "BootCurrent", idx);
	if (fu_efivars_get_boot_next(efivars, &idx, NULL))
		fwupd_codec_string_append_hex(str, 0, "BootNext", idx);

	for (guint i = 0; i < entries->len; i++) {
		FuEfiLoadOption *entry = g_ptr_array_index(entries, i);
		g_autofree gchar *title =
		    g_strdup_printf("Boot%04X", (guint)fu_firmware_get_idx(FU_FIRMWARE(entry)));
		fwupd_codec_string_append(str, 0, title, "");
		fwupd_codec_add_string(FWUPD_CODEC(entry), 1, str);
	}

	/* success */
	fu_console_print_literal(console, str->str);
	return TRUE;
}

static void
fu_engine_cli_efivar_files_as_json(FuEngineCli *self, GPtrArray *files)
{
	FuConsole *console = fu_cli_get_console(FU_CLI(self));
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(GHashTable) hash = g_hash_table_new_full(g_str_hash,
							   g_str_equal,
							   g_free,
							   (GDestroyNotify)g_ptr_array_unref);
	GHashTableIter iter;
	gpointer key;
	gpointer value;

	/* convert an array of FuPeFirmware to a map with the BootXXXX ID as the hash key and the
	 * filename as an array */
	for (guint i = 0; i < files->len; i++) {
		FuFirmware *firmware = g_ptr_array_index(files, i);
		GPtrArray *array;
		g_autofree gchar *name = NULL;

		name = g_strdup_printf("Boot%04X", (guint)fu_firmware_get_idx(firmware));
		array = g_hash_table_lookup(hash, name);
		if (array == NULL) {
			array = g_ptr_array_new_with_free_func(g_free);
			g_hash_table_insert(hash, g_steal_pointer(&name), array);
		}
		g_ptr_array_add(array, g_strdup(fu_firmware_get_filename(firmware)));
	}

	/* export */
	g_hash_table_iter_init(&iter, hash);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		const gchar *bootvar = (const gchar *)key;
		GPtrArray *array = (GPtrArray *)value;
		g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();

		for (guint i = 0; i < array->len; i++) {
			const gchar *filename = g_ptr_array_index(array, i);
			fwupd_json_array_add_string(json_arr, filename);
		}
		fwupd_json_object_add_array(json_obj, bootvar, json_arr);
	}
	fu_cli_print_json_object(console, json_obj);
}

static gboolean
fu_engine_cli_efivar_files(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(GPtrArray) files = NULL;

	files = fu_context_get_esp_files(self->ctx,
					 FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_FIRST_STAGE |
					     FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_SECOND_STAGE |
					     FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_REVOCATIONS,
					 error);
	if (files == NULL)
		return FALSE;
	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_engine_cli_efivar_files_as_json(self, files);
		return TRUE;
	}
	for (guint i = 0; i < files->len; i++) {
		FuFirmware *firmware = g_ptr_array_index(files, i);
		g_autofree gchar *name =
		    g_strdup_printf("Boot%04X", (guint)fu_firmware_get_idx(firmware));
		fu_console_print(console, "%s → %s", name, fu_firmware_get_filename(firmware));
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_cli_efivar_list(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuEfivars *efivars = fu_context_get_efivars(self->ctx);
	g_autoptr(GPtrArray) names = NULL;

	/* sanity check */
	if (g_strv_length(values) < 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: error message */
				    _("Invalid arguments, expected GUID"));
		return FALSE;
	}
	names = fu_efivars_get_names(efivars, values[0], error);
	if (names == NULL)
		return FALSE;
	for (guint i = 0; i < names->len; i++) {
		const gchar *name = g_ptr_array_index(names, i);
		fu_console_print(console, "name: %s", name);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_cli_build_cabinet(FuCli *cli, gchar **values, GError **error)
{
	g_autoptr(GBytes) cab_blob = NULL;
	g_autoptr(FuCabinet) cab_file = fu_cabinet_new();

	/* sanity check */
	if (g_strv_length(values) < 3) {
		g_set_error_literal(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOTHING_TO_DO,
		    /* TRANSLATORS: error message */
		    _("Invalid arguments, expected at least ARCHIVE FIRMWARE METAINFO"));
		return FALSE;
	}

	/* file already exists */
	if (!fu_cli_has_arg_flag(cli, FU_CLI_ARG_FLAG_FORCE) &&
	    g_file_test(values[0], G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Filename already exists");
		return FALSE;
	}

	/* add each file */
	for (guint i = 1; values[i] != NULL; i++) {
		g_autoptr(GBytes) blob = NULL;
		g_autofree gchar *basename = g_path_get_basename(values[i]);
		blob = fu_bytes_get_contents(values[i], error);
		if (blob == NULL)
			return FALSE;
		if (g_bytes_get_size(blob) == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "%s has zero size",
				    values[i]);
			return FALSE;
		}
		if (!fu_cabinet_add_file(cab_file, basename, blob, error))
			return FALSE;
	}

	/* export */
	cab_blob = fu_firmware_write(FU_FIRMWARE(cab_file), error);
	if (cab_blob == NULL)
		return FALSE;

	/* sanity check JCat and XML MetaInfo files */
	if (!fu_firmware_parse_bytes(FU_FIRMWARE(cab_file),
				     cab_blob,
				     0x0,
				     FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
				     error))
		return FALSE;

	return fu_bytes_set_contents(values[0], cab_blob, error);
}

static gboolean
fu_engine_cli_clear_history(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(FuHistory) history = fu_history_new(self->ctx);
	return fu_history_remove_all(history, error);
}

static FwupdJcatFile *
fu_engine_cli_jcat_load_filename(FuEngineCli *self, const gchar *filename, GError **error)
{
	g_autoptr(FwupdJcatFile) file = fwupd_jcat_file_new();
	g_autoptr(GFile) gfile = g_file_new_for_path(filename);

	if (g_file_query_exists(gfile, fu_cli_get_cancellable(FU_CLI(self)))) {
		g_autoptr(FuFileInputStream) istream = NULL;
		g_autoptr(GInputStream) g_istream = NULL; /* nocheck:blocked */
		istream = fu_file_input_stream_from_file(gfile,
							 fu_cli_get_cancellable(FU_CLI(self)),
							 error);
		if (istream == NULL)
			return NULL;
		g_istream = fu_input_stream_as_g_input_stream(FU_INPUT_STREAM(istream));
		if (!fwupd_jcat_file_import_stream(file, g_istream, error))
			return NULL;
	}

	/* success */
	return g_steal_pointer(&file);
}

static gboolean
fu_engine_cli_jcat_save_filename(FuEngineCli *self,
				 FwupdJcatFile *file,
				 const gchar *filename,
				 GError **error)
{
	g_autoptr(GBytes) blob = NULL;

	blob = fwupd_jcat_file_export_bytes(file, error);
	if (blob == NULL)
		return FALSE;
	return fu_bytes_set_contents(filename, blob, error);
}

static gboolean
fu_engine_cli_jcat_info(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(FwupdJcatFile) file = NULL;
	g_autofree gchar *str = NULL;

	/* check args */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "Invalid arguments, expected FILENAME");
		return FALSE;
	}

	/* import file */
	file = fu_engine_cli_jcat_load_filename(self, values[0], error);
	if (file == NULL)
		return FALSE;

	/* output to console */
	str = fwupd_codec_to_string(FWUPD_CODEC(file));
	fu_console_print_literal(console, str);

	/* success */
	return TRUE;
}

static gboolean
fu_engine_cli_jcat_add_alias(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(FwupdJcatFile) file = NULL;
	g_autoptr(FwupdJcatItem) item = NULL;

	/* check args */
	if (g_strv_length(values) != 3) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "Invalid arguments, expected FILENAME ID ALIAS_ID");
		return FALSE;
	}

	/* import file */
	file = fu_engine_cli_jcat_load_filename(self, values[0], error);
	if (file == NULL)
		return FALSE;

	/* add alias */
	item = fwupd_jcat_file_get_item_by_id(file, values[1], error);
	if (item == NULL)
		return FALSE;
	fwupd_jcat_item_add_alias_id(item, values[2]);

	/* export new file */
	return fu_engine_cli_jcat_save_filename(self, file, values[0], error);
}

static gboolean
fu_engine_cli_jcat_remove_alias(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(FwupdJcatFile) file = NULL;
	g_autoptr(FwupdJcatItem) item = NULL;

	/* check args */
	if (g_strv_length(values) != 3) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "Invalid arguments, expected FILENAME ID ALIAS_ID");
		return FALSE;
	}

	/* import file */
	file = fu_engine_cli_jcat_load_filename(self, values[0], error);
	if (file == NULL)
		return FALSE;

	/* remove alias */
	item = fwupd_jcat_file_get_item_by_id(file, values[1], error);
	if (item == NULL)
		return FALSE;
	fwupd_jcat_item_remove_alias_id(item, values[2]);

	/* export new file */
	return fu_engine_cli_jcat_save_filename(self, file, values[0], error);
}

typedef enum {
	FU_CLI_JCAT_BLOB_VARIANT_NONE = 0,
	FU_CLI_JCAT_BLOB_VARIANT_PQ = 1 << 0,
} FuCliJcatBlobVariant;

static gboolean
fu_engine_cli_jcat_blob_kind_from_string(const gchar *kind_str,
					 FwupdJcatBlobKind *kind,
					 FuCliJcatBlobVariant *variant,
					 GError **error)
{
	g_auto(GStrv) split = g_strsplit(kind_str, ":", -1);

	*kind = fwupd_jcat_blob_kind_from_string(split[0]);
	if (*kind == FWUPD_JCAT_BLOB_KIND_UNKNOWN) {
		g_autoptr(GString) tmp = g_string_new(NULL);
		for (guint i = 1; i < FWUPD_JCAT_BLOB_KIND_LAST; i++)
			g_string_append_printf(tmp, "%s,", fwupd_jcat_blob_kind_to_string(i));
		if (tmp->len > 0)
			g_string_truncate(tmp, tmp->len - 1);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_ARGS,
			    "failed to parse '%s', expected %s",
			    split[0],
			    tmp->str);
		return FALSE;
	}

	/* variants */
	for (guint i = 1; split[i] != NULL; i++) {
		if (g_strcmp0(split[i], "pq") == 0) {
			if (variant != NULL)
				*variant |= FU_CLI_JCAT_BLOB_VARIANT_PQ;
		}
	}

	/* success */
	return TRUE;
}

static const gchar *
fu_engine_cli_jcat_blob_kind_to_ext(FwupdJcatBlobKind kind)
{
	if (kind == FWUPD_JCAT_BLOB_KIND_GPG)
		return "asc";
	if (kind == FWUPD_JCAT_BLOB_KIND_PKCS7)
		return "p7b";
	if (kind == FWUPD_JCAT_BLOB_KIND_SHA256)
		return "sha256";
	if (kind == FWUPD_JCAT_BLOB_KIND_SHA1)
		return "sha1";
	if (kind == FWUPD_JCAT_BLOB_KIND_ED25519)
		return "ed25519";
	if (kind == FWUPD_JCAT_BLOB_KIND_SHA512)
		return "sha512";
	return NULL;
}

static gboolean
fu_engine_cli_jcat_export(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FwupdJcatBlobKind kind = FWUPD_JCAT_BLOB_KIND_UNKNOWN;
	g_autoptr(FwupdJcatFile) file = NULL;
	g_autoptr(GPtrArray) items = NULL;

	/* check args */
	if (g_strv_length(values) < 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "Invalid arguments, expected FILENAME");
		return FALSE;
	}
	if (g_strv_length(values) > 1) {
		if (!fu_engine_cli_jcat_blob_kind_from_string(values[1], &kind, NULL, error))
			return FALSE;
	}

	/* import existing file */
	file = fu_engine_cli_jcat_load_filename(self, values[0], error);
	if (file == NULL)
		return FALSE;

	/* extract each file */
	items = fwupd_jcat_file_get_items(file);
	for (guint i = 0; i < items->len; i++) {
		FwupdJcatItem *item = g_ptr_array_index(items, i);
		const gchar *id;
		g_autoptr(GPtrArray) blobs = fwupd_jcat_item_get_blobs(item);

		/* the ID is used as the basename, so it cannot contain path components */
		id = fwupd_jcat_item_get_id_safe(item, error);
		if (id == NULL)
			return FALSE;
		for (guint j = 0; j < blobs->len; j++) {
			FwupdJcatBlob *blob = g_ptr_array_index(blobs, j);
			g_autofree gchar *fn = NULL;
			g_autoptr(GString) str = NULL;

			/* skip */
			if (kind != FWUPD_JCAT_BLOB_KIND_UNKNOWN &&
			    kind != fwupd_jcat_blob_get_kind(blob))
				continue;

			/* export */
			str = g_string_new(id);
			g_string_append_printf(
			    str,
			    ".%s",
			    fu_engine_cli_jcat_blob_kind_to_ext(fwupd_jcat_blob_get_kind(blob)));
			fn = g_build_filename(self->destdir, str->str, NULL);
			if (!fu_bytes_set_contents_full(fn,
							fwupd_jcat_blob_get_data(blob),
							0644,
							error))
				return FALSE;
			fu_console_print(console, "Wrote %s", fn);
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_cli_jcat_import(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	g_autoptr(GBytes) data_sig = NULL;
	g_autoptr(FwupdJcatBlob) blob = NULL;
	g_autoptr(FwupdJcatFile) file = NULL;
	g_autoptr(FwupdJcatItem) item = NULL;

	/* check args */
	if (g_strv_length(values) < 3) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "Invalid arguments, expected FILENAME DATA DETACHED_KEY");
		return FALSE;
	}

	/* import existing file */
	file = fu_engine_cli_jcat_load_filename(self, values[0], error);
	if (file == NULL)
		return FALSE;

	/* load source */
	data_sig = fu_bytes_get_contents(values[2], error);
	if (data_sig == NULL)
		return FALSE;

	/* guess format */
	if (g_str_has_suffix(values[2], ".asc")) {
		blob = fwupd_jcat_blob_new(FWUPD_JCAT_BLOB_KIND_GPG,
					   data_sig,
					   FWUPD_JCAT_BLOB_FLAG_IS_UTF8);
	} else if (g_str_has_suffix(values[2], ".p7b") || g_str_has_suffix(values[2], ".p7c") ||
		   g_str_has_suffix(values[2], ".pem")) {
		blob = fwupd_jcat_blob_new(FWUPD_JCAT_BLOB_KIND_PKCS7,
					   data_sig,
					   FWUPD_JCAT_BLOB_FLAG_IS_UTF8);
	} else if (g_str_has_suffix(values[2], ".der")) {
		blob = fwupd_jcat_blob_new(FWUPD_JCAT_BLOB_KIND_PKCS7,
					   data_sig,
					   FWUPD_JCAT_BLOB_FLAG_NONE);
	} else if (g_str_has_suffix(values[2], ".ed25519")) {
		blob = fwupd_jcat_blob_new(FWUPD_JCAT_BLOB_KIND_ED25519,
					   data_sig,
					   FWUPD_JCAT_BLOB_FLAG_NONE);
	} else if (g_str_has_suffix(values[2], ".sha256") ||
		   g_str_has_suffix(values[2], ".SHA256")) {
		blob = fwupd_jcat_blob_new(FWUPD_JCAT_BLOB_KIND_SHA256,
					   data_sig,
					   FWUPD_JCAT_BLOB_FLAG_IS_UTF8);
	} else if (g_str_has_suffix(values[2], ".sha512") ||
		   g_str_has_suffix(values[2], ".SHA512")) {
		blob = fwupd_jcat_blob_new(FWUPD_JCAT_BLOB_KIND_SHA512,
					   data_sig,
					   FWUPD_JCAT_BLOB_FLAG_IS_UTF8);
	} else {
		g_autoptr(GString) tmp = g_string_new(NULL);
		for (guint i = 1; i < FWUPD_JCAT_BLOB_KIND_LAST; i++)
			g_string_append_printf(tmp, "%s,", fu_engine_cli_jcat_blob_kind_to_ext(i));
		if (tmp->len > 0)
			g_string_truncate(tmp, tmp->len - 1);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "Cannot detect blob kind from extension, expected %s",
			    tmp->str);
		return FALSE;
	}

	/* get item */
	item = fwupd_jcat_file_get_item_by_id(file, values[1], NULL);
	if (item == NULL) {
		g_autofree gchar *basename = g_path_get_basename(values[1]);
		item = fwupd_jcat_item_new(basename);
		fwupd_jcat_file_add_item(file, item);
	}

	/* just import existing key */
	fwupd_jcat_item_add_blob(item, blob);

	/* export new file */
	return fu_engine_cli_jcat_save_filename(self, file, values[0], error);
}

static gboolean
fu_engine_cli_jcat_self_sign(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuJcatSignFlags flags = FU_JCAT_SIGN_FLAG_NONE;
	FwupdJcatBlobKind kind = FWUPD_JCAT_BLOB_KIND_UNKNOWN;
	FwupdJcatBlobKind target = FWUPD_JCAT_BLOB_KIND_UNKNOWN;
	FuCliJcatBlobVariant variant = FU_CLI_JCAT_BLOB_VARIANT_NONE;
	g_autofree gchar *basename = NULL;
	g_autoptr(GBytes) source = NULL;
	g_autoptr(FwupdJcatBlob) blob = NULL;
	g_autoptr(FuJcatEngine) engine = NULL;
	g_autoptr(FwupdJcatFile) file = NULL;
	g_autoptr(FwupdJcatItem) item = NULL;

	/* check args */
	if (g_strv_length(values) < 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "Invalid arguments, expected FILENAME SOURCE");
		return FALSE;
	}
	if (g_strv_length(values) >= 3) {
		if (!fu_engine_cli_jcat_blob_kind_from_string(values[2], &kind, &variant, error))
			return FALSE;
	}
	if (g_strv_length(values) >= 4) {
		if (!fu_engine_cli_jcat_blob_kind_from_string(values[3], &target, NULL, error))
			return FALSE;
	}

	/* import existing file */
	file = fu_engine_cli_jcat_load_filename(self, values[0], error);
	if (file == NULL)
		return FALSE;

	/* create item if required */
	basename = g_path_get_basename(values[1]);
	item = fwupd_jcat_file_get_item_by_id(file, basename, NULL);
	if (item == NULL) {
		item = fwupd_jcat_item_new(basename);
		fwupd_jcat_file_add_item(file, item);
	}

	/* load source */
	if (target == FWUPD_JCAT_BLOB_KIND_UNKNOWN) {
		source = fu_bytes_get_contents(values[1], error);
		if (source == NULL)
			return FALSE;
	} else {
		g_autoptr(FwupdJcatBlob) blob_target = NULL;
		blob_target = fwupd_jcat_item_get_blob_by_kind(item, target, error);
		if (blob_target == NULL)
			return FALSE;
		source = g_bytes_ref(fwupd_jcat_blob_get_data(blob_target));
	}

	/* sign with this kind */
	if (kind == FWUPD_JCAT_BLOB_KIND_UNKNOWN)
		kind = FWUPD_JCAT_BLOB_KIND_PKCS7;
	if (variant & FU_CLI_JCAT_BLOB_VARIANT_PQ)
		flags |= FU_JCAT_SIGN_FLAG_USE_PQ;
	engine = fu_jcat_context_get_engine(self->jcat_context, kind, error);
	if (engine == NULL)
		return FALSE;
	blob = fu_jcat_engine_self_sign(engine, source, flags, error);
	if (blob == NULL)
		return FALSE;
	if (target != FWUPD_JCAT_BLOB_KIND_UNKNOWN)
		fwupd_jcat_blob_set_target(blob, target);
	fwupd_jcat_item_add_blob(item, blob);

	/* export new file */
	return fu_engine_cli_jcat_save_filename(self, file, values[0], error);
}

static gboolean
fu_engine_cli_jcat_sign(FuCli *cli, gchar **values, GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FwupdJcatBlobKind kind = FWUPD_JCAT_BLOB_KIND_UNKNOWN;
	FwupdJcatBlobKind target = FWUPD_JCAT_BLOB_KIND_UNKNOWN;
	g_autoptr(GBytes) cert = NULL;
	g_autoptr(GBytes) privkey = NULL;
	g_autoptr(GBytes) source = NULL;
	g_autoptr(FwupdJcatBlob) blob = NULL;
	g_autoptr(FwupdJcatFile) file = NULL;
	g_autoptr(FwupdJcatItem) item = NULL;
	g_autoptr(FuJcatEngine) engine = NULL;

	/* check args */
	if (g_strv_length(values) < 4) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "Invalid arguments, expected FILENAME "
				    "SOURCE CERT PRIVKEY");
		return FALSE;
	}
	if (g_strv_length(values) >= 5) {
		if (!fu_engine_cli_jcat_blob_kind_from_string(values[4], &kind, NULL, error))
			return FALSE;
	}
	if (g_strv_length(values) >= 6) {
		if (!fu_engine_cli_jcat_blob_kind_from_string(values[5], &target, NULL, error))
			return FALSE;
	}

	/* import existing file */
	file = fu_engine_cli_jcat_load_filename(self, values[0], error);
	if (file == NULL)
		return FALSE;

	/* create item if required */
	item = fwupd_jcat_file_get_item_by_id(file, values[1], NULL);
	if (item == NULL) {
		g_autofree gchar *basename = g_path_get_basename(values[1]);
		item = fwupd_jcat_item_new(basename);
		fwupd_jcat_file_add_item(file, item);
	}

	/* load source */
	if (target == FWUPD_JCAT_BLOB_KIND_UNKNOWN) {
		source = fu_bytes_get_contents(values[1], error);
		if (source == NULL)
			return FALSE;
	} else {
		g_autoptr(FwupdJcatBlob) blob_target = NULL;
		blob_target = fwupd_jcat_item_get_blob_by_kind(item, target, error);
		if (blob_target == NULL)
			return FALSE;
		source = g_bytes_ref(fwupd_jcat_blob_get_data(blob_target));
	}

	/* certificate and privatekey */
	cert = fu_bytes_get_contents(values[2], error);
	if (cert == NULL)
		return FALSE;
	privkey = fu_bytes_get_contents(values[3], error);
	if (privkey == NULL)
		return FALSE;

	/* sign with this kind */
	if (kind == FWUPD_JCAT_BLOB_KIND_UNKNOWN)
		kind = FWUPD_JCAT_BLOB_KIND_PKCS7;
	engine = fu_jcat_context_get_engine(self->jcat_context, kind, error);
	if (engine == NULL)
		return FALSE;
	blob =
	    fu_jcat_engine_pubkey_sign(engine,
				       source,
				       cert,
				       privkey,
				       FU_JCAT_SIGN_FLAG_ADD_TIMESTAMP | FU_JCAT_SIGN_FLAG_ADD_CERT,
				       error);
	if (blob == NULL)
		return FALSE;
	if (target != FWUPD_JCAT_BLOB_KIND_UNKNOWN)
		fwupd_jcat_blob_set_target(blob, target);
	fwupd_jcat_item_add_blob(item, blob);

	/* export new file */
	return fu_engine_cli_jcat_save_filename(self, file, values[0], error);
}

static gboolean
fu_engine_cli_jcat_verify_item(FuEngineCli *self,
			       FwupdJcatItem *item,
			       FwupdJcatBlobKind kind,
			       FuCliJcatBlobVariant variant,
			       GError **error)
{
	FuConsole *console = fu_cli_get_console(FU_CLI(self));
	gboolean ret = TRUE;
	const gchar *id;
	g_autoptr(GBytes) source = NULL;
	g_autoptr(GPtrArray) alias_ids = fwupd_jcat_item_get_alias_ids(item);
	g_autoptr(GPtrArray) blobs = fwupd_jcat_item_get_blobs(item);
	g_autoptr(GPtrArray) fns_possible = g_ptr_array_new_with_free_func(g_free);
	g_autofree gchar *fn_safe = NULL;

	/* load source */
	fu_console_print(console, "%s:", fwupd_jcat_item_get_id(item));

	/* find the source; the ID and aliases are used as basenames, so they cannot contain
	 * path components */
	id = fwupd_jcat_item_get_id_safe(item, error);
	if (id == NULL)
		return FALSE;
	g_ptr_array_add(fns_possible, g_build_filename(self->destdir, id, NULL));
	for (guint i = 0; i < alias_ids->len; i++) {
		const gchar *alias_id = g_ptr_array_index(alias_ids, i);
		if (!fu_path_verify_safe(alias_id, error))
			return FALSE;
		g_ptr_array_add(fns_possible, g_build_filename(self->destdir, alias_id, NULL));
	}
	for (guint i = 0; i < fns_possible->len; i++) {
		const gchar *fn = g_ptr_array_index(fns_possible, i);
		if (g_file_test(fn, G_FILE_TEST_EXISTS)) {
			fn_safe = g_strdup(fn);
			break;
		}
	}
	if (fn_safe == NULL) {
		g_autofree gchar *str = NULL;
		g_autofree gchar **strv = g_new0(gchar *, fns_possible->len + 1);
		for (guint i = 0; i < fns_possible->len; i++)
			strv[i] = g_ptr_array_index(fns_possible, i);
		str = g_strjoinv(" or ", strv);
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "Could not find %s", str);
		return FALSE;
	}

	/* load source */
	source = fu_bytes_get_contents(fn_safe, error);
	if (source == NULL)
		return FALSE;

	/* verify blob */
	for (guint j = 0; j < blobs->len; j++) {
		FwupdJcatBlob *blob = g_ptr_array_index(blobs, j);
		FwupdJcatBlobKind target = fwupd_jcat_blob_get_target(blob);
		FuJcatVerifyFlags flags = FU_JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS;
		const gchar *authority;
		g_autoptr(GError) error_verify = NULL;
		g_autoptr(FuJcatResult) result = NULL;
		g_autoptr(GBytes) blob_source = NULL;
		g_autoptr(GString) kind_str = g_string_new(NULL);

		/* skip */
		if (kind != FWUPD_JCAT_BLOB_KIND_UNKNOWN && kind != fwupd_jcat_blob_get_kind(blob))
			continue;

		/* get correct source */
		if (target == FWUPD_JCAT_BLOB_KIND_UNKNOWN) {
			blob_source = g_bytes_ref(source);
		} else {
			g_autoptr(FwupdJcatBlob) blob_target = NULL;
			blob_target = fwupd_jcat_item_get_blob_by_kind(item, target, error);
			if (blob_target == NULL)
				return FALSE;
			blob_source = g_bytes_ref(fwupd_jcat_blob_get_data(blob_target));
		}

		g_string_append(kind_str,
				fwupd_jcat_blob_kind_to_string(fwupd_jcat_blob_get_kind(blob)));
		if (fwupd_jcat_blob_get_target(blob) != FWUPD_JCAT_BLOB_KIND_UNKNOWN) {
			g_string_append_printf(
			    kind_str,
			    "-of-%s",
			    fwupd_jcat_blob_kind_to_string(fwupd_jcat_blob_get_target(blob)));
		}
		if (variant & FU_CLI_JCAT_BLOB_VARIANT_PQ)
			flags |= FU_JCAT_VERIFY_FLAG_ONLY_PQ;
		result = fu_jcat_context_verify_blob(self->jcat_context,
						     blob_source,
						     blob,
						     flags,
						     &error_verify);
		if (result == NULL) {
			if (g_error_matches(error_verify, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
				fu_console_print(console,
						 "    SKIPPED %s: %s",
						 kind_str->str,
						 error_verify->message);
				continue;
			}
			fu_console_print(console,
					 "    FAILED %s: %s",
					 kind_str->str,
					 error_verify->message);
			ret = FALSE;
			continue;
		}
		authority = fu_jcat_result_get_authority(result);
		fu_console_print(console,
				 "    PASSED %s: %s",
				 kind_str->str,
				 authority != NULL ? authority : "OK");
	}
	if (!ret) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "validation failed");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_cli_jcat_verify(FuCli *cli, gchar **values, GError **error)
{
	FuConsole *console = fu_cli_get_console(cli);
	FuEngineCli *self = FU_ENGINE_CLI(cli);
	FuCliJcatBlobVariant variant = FU_CLI_JCAT_BLOB_VARIANT_NONE;
	FwupdJcatBlobKind kind = FWUPD_JCAT_BLOB_KIND_UNKNOWN;
	gboolean ret = TRUE;
	g_autoptr(FwupdJcatFile) file = NULL;
	g_autoptr(GPtrArray) items = NULL;

	/* check args */
	if (g_strv_length(values) < 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "Invalid arguments, expected FILENAME [SOURCE]");
		return FALSE;
	}
	if (g_strv_length(values) >= 3) {
		if (!fu_engine_cli_jcat_blob_kind_from_string(values[2], &kind, &variant, error))
			return FALSE;
	}

	/* import existing file */
	file = fu_engine_cli_jcat_load_filename(self, values[0], error);
	if (file == NULL)
		return FALSE;

	/* verify each file */
	items = fwupd_jcat_file_get_items(file);
	for (guint i = 0; i < items->len; i++) {
		FwupdJcatItem *item = g_ptr_array_index(items, i);
		g_autoptr(GError) error_local = NULL;
		if (!fu_engine_cli_jcat_verify_item(self, item, kind, variant, &error_local)) {
			fu_console_print(console, "    FAILED: %s", error_local->message);
			ret = FALSE;
			continue;
		}
	}
	if (!ret) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "Validation failed");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_cli_sync_impl_activate(FwupdClient *client,
				 const gchar *device_id,
				 gpointer user_data,
				 GCancellable *cancellable,
				 GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_activate(self->engine, device_id, self->progress, error);
}

static gboolean
fu_engine_cli_sync_impl_clean_remote(FwupdClient *client,
				     const gchar *remote_id,
				     gpointer user_data,
				     GCancellable *cancellable,
				     GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_clean_remote(self->engine, remote_id, error);
}

static gboolean
fu_engine_cli_sync_impl_clear_results(FwupdClient *client,
				      const gchar *device_id,
				      gpointer user_data,
				      GCancellable *cancellable,
				      GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_clear_results(self->engine, device_id, error);
}

static gboolean
fu_engine_cli_sync_impl_connect(FwupdClient *client,
				gpointer user_data,
				GCancellable *cancellable,
				GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	FuContext *ctx = fu_engine_get_context(self->engine);
	FuHwids *hwids = fu_context_get_hwids(ctx);
	g_autoptr(GPtrArray) chid_keys = NULL;
	g_autoptr(GPtrArray) hwid_keys = NULL;

	/* already done */
	if (fu_engine_get_phase(self->engine) != FU_ENGINE_PHASE_IDLE)
		return TRUE;

	if (!fu_engine_cli_start_engine(
		self,
		FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_DEVICE_HOTPLUG |
		    FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS | FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
		    FU_ENGINE_LOAD_FLAG_PATH_STORE_DEFAULTS | FU_ENGINE_LOAD_FLAG_HWINFO |
		    FU_ENGINE_LOAD_FLAG_REMOTES | FU_ENGINE_LOAD_FLAG_HISTORY,
		self->progress,
		error))
		return FALSE;

	/* recreate pre-cooked HWIDs */
	hwid_keys = fu_hwids_get_keys(hwids);
	for (guint i = 0; i < hwid_keys->len; i++) {
		const gchar *hwid_key = g_ptr_array_index(hwid_keys, i);
		const gchar *value = fu_hwids_get_value(hwids, hwid_key);
		g_autofree gchar *hwid_value = NULL;

		if (value == NULL)
			continue;
		if (g_strcmp0(hwid_key, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE) == 0 ||
		    g_strcmp0(hwid_key, FU_HWIDS_KEY_BIOS_MINOR_RELEASE) == 0) {
			guint64 val = 0;
			if (!fu_strtoull(value, &val, 0, G_MAXUINT64, FU_INTEGER_BASE_16, error))
				return FALSE;
			hwid_value = g_strdup_printf("%" G_GUINT64_FORMAT, val);
		} else {
			hwid_value = g_strdup(value);
		}
		fwupd_client_add_hwid(client, hwid_key, hwid_value);
	}
	chid_keys = fu_hwids_get_chid_keys(hwids);
	for (guint i = 0; i < chid_keys->len; i++) {
		const gchar *key = g_ptr_array_index(chid_keys, i);
		const gchar *keys = NULL;
		g_autofree gchar *guid = NULL;

		keys = fu_hwids_get_replace_keys(hwids, key);
		if (keys == NULL)
			continue;
		guid = fu_hwids_get_guid(hwids, key, NULL);
		if (guid == NULL)
			continue;
		fwupd_client_add_hwid(client, keys, guid);
	}

	/* success */
	return fwupd_client_run_connect_funcs(client, error);
}

static gboolean
fu_engine_cli_sync_impl_emulation_load(FwupdClient *client,
				       const gchar *filename,
				       gpointer user_data,
				       GCancellable *cancellable,
				       GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	g_autoptr(FuInputStream) stream = NULL;

	stream = fu_input_stream_from_path(filename, error);
	if (stream == NULL)
		return FALSE;
	return fu_engine_emulation_load(self->engine, stream, error);
}

static gboolean
fu_engine_cli_sync_impl_emulation_save(FwupdClient *client,
				       const gchar *filename,
				       gpointer user_data,
				       GCancellable *cancellable,
				       GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileOutputStream) stream = NULL;

	/* save every tagged device */
	file = g_file_new_for_path(filename);
	stream = g_file_replace(file, NULL, FALSE, G_FILE_CREATE_REPLACE_DESTINATION, NULL, error);
	if (stream == NULL)
		return FALSE;
	return fu_engine_emulation_save(self->engine, G_OUTPUT_STREAM(stream), error);
}

static GPtrArray *
fu_engine_cli_sync_impl_get_bios_settings(FwupdClient *client,
					  gpointer user_data,
					  GCancellable *cancellable,
					  GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	FuContext *ctx = fu_engine_get_context(self->engine);
	g_autoptr(FuBiosSettings) attrs = fu_context_get_bios_settings(ctx);
	return fu_bios_settings_get_all(attrs);
}

static GPtrArray *
fu_engine_cli_sync_impl_get_details(FwupdClient *client,
				    const gchar *filename,
				    gpointer user_data,
				    GCancellable *cancellable,
				    GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	g_autoptr(FuInputStream) stream = NULL;

	stream = fu_input_stream_from_path(filename, error);
	if (stream == NULL) {
		fu_engine_cli_maybe_prefix_sandbox_error(filename, error);
		return NULL;
	}
	return fu_engine_get_details(self->engine, self->request, stream, error);
}

static GPtrArray *
fu_engine_cli_sync_impl_get_devices(FwupdClient *client,
				    gpointer user_data,
				    GCancellable *cancellable,
				    GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_get_devices(self->engine, error);
}

static FwupdDevice *
fu_engine_cli_sync_impl_get_device_by_id(FwupdClient *client,
					 const gchar *device_id,
					 gpointer user_data,
					 GCancellable *cancellable,
					 GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return FWUPD_DEVICE(fu_engine_get_device(self->engine, device_id, error));
}

static GPtrArray *
fu_engine_cli_sync_impl_get_downgrades(FwupdClient *client,
				       const gchar *device_id,
				       gpointer user_data,
				       GCancellable *cancellable,
				       GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_get_downgrades(self->engine, self->request, device_id, error);
}

static GPtrArray *
fu_engine_cli_sync_impl_get_devices_by_guid(FwupdClient *client,
					    const gchar *guid,
					    gpointer user_data,
					    GCancellable *cancellable,
					    GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_get_devices_by_guid(self->engine, guid, error);
}

static GPtrArray *
fu_engine_cli_sync_impl_get_plugins(FwupdClient *client,
				    gpointer user_data,
				    GCancellable *cancellable,
				    GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return g_ptr_array_ref(fu_engine_get_plugins(self->engine));
}

static GPtrArray *
fu_engine_cli_sync_impl_get_history(FwupdClient *client,
				    gpointer user_data,
				    GCancellable *cancellable,
				    GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_get_history(self->engine, error);
}

static GPtrArray *
fu_engine_cli_sync_impl_get_host_security_attrs(FwupdClient *client,
						gpointer user_data,
						GCancellable *cancellable,
						GError **error)
{
#ifdef HAVE_HSI
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	g_autofree gchar *host_security_id = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_engine_get_host_security_attrs(self->engine);

	host_security_id = fu_engine_get_host_security_id(self->engine, NULL);
	fwupd_client_set_host_security_id(client, host_security_id);
	return fu_security_attrs_get_all(attrs, NULL);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "HSI support not enabled");

	return NULL;
#endif
}

static GPtrArray *
fu_engine_cli_sync_impl_get_host_security_events(FwupdClient *client,
						 guint limit,
						 gpointer user_data,
						 GCancellable *cancellable,
						 GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	g_autoptr(FuSecurityAttrs) events = NULL;

	events = fu_engine_get_host_security_events(self->engine, limit, error);
	if (events == NULL)
		return NULL;
	return fu_security_attrs_get_all(events, NULL);
}

static GPtrArray *
fu_engine_cli_sync_impl_get_releases(FwupdClient *client,
				     const gchar *device_id,
				     gpointer user_data,
				     GCancellable *cancellable,
				     GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_get_releases(self->engine, self->request, device_id, error);
}

static FwupdDevice *
fu_engine_cli_sync_impl_get_results(FwupdClient *client,
				    const gchar *device_id,
				    gpointer user_data,
				    GCancellable *cancellable,
				    GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_get_results(self->engine, device_id, error);
}

static GPtrArray *
fu_engine_cli_sync_impl_get_upgrades(FwupdClient *client,
				     const gchar *device_id,
				     gpointer user_data,
				     GCancellable *cancellable,
				     GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_get_upgrades(self->engine, self->request, device_id, error);
}

static GPtrArray *
fu_engine_cli_sync_impl_get_remotes(FwupdClient *client,
				    gpointer user_data,
				    GCancellable *cancellable,
				    GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_get_remotes(self->engine, error);
}

static FwupdRemote *
fu_engine_cli_sync_impl_get_remote_by_id(FwupdClient *client,
					 const gchar *remote_id,
					 gpointer user_data,
					 GCancellable *cancellable,
					 GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_get_remote_by_id(self->engine, remote_id, error);
}

static GHashTable *
fu_engine_cli_sync_impl_get_report_metadata(FwupdClient *client,
					    gpointer user_data,
					    GCancellable *cancellable,
					    GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_get_report_metadata(self->engine, error);
}

static gboolean
fu_engine_cli_sync_impl_install(FwupdClient *client,
				const gchar *device_id,
				const gchar *filename,
				FwupdInstallFlags install_flags,
				gpointer user_data,
				GCancellable *cancellable,
				GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FwupdDevice) device = NULL;
	g_autoptr(GPtrArray) devices_possible = NULL;

	/* handle both forms */
	if (g_strcmp0(device_id, FWUPD_DEVICE_ID_ANY) == 0) {
		devices_possible = fu_engine_get_devices(self->engine, error);
		if (devices_possible == NULL)
			return FALSE;
	} else {
		device = fu_engine_cli_get_device(self, device_id, error);
		if (device == NULL)
			return FALSE;
		devices_possible =
		    fu_engine_get_devices_by_composite_id(self->engine,
							  fu_device_get_composite_id(device),
							  error);
		if (devices_possible == NULL)
			return FALSE;
		g_ptr_array_add(devices_possible, device);
	}

	/* download if required */
	stream = fu_input_stream_from_path(filename, error);
	if (stream == NULL) {
		fu_engine_cli_maybe_prefix_sandbox_error(filename, error);
		return FALSE;
	}
	if (!fu_engine_cli_install_stream(self, stream, devices_possible, self->progress, error))
		return FALSE;
	fu_cli_display_current_message(FU_CLI(self));

	/* we don't want to ask anything */
	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_NO_REBOOT_CHECK)) {
		g_debug("skipping reboot check");
		return TRUE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_cli_sync_impl_install_release(FwupdClient *client,
					FwupdDevice *device,
					FwupdRelease *release,
					FwupdInstallFlags install_flags,
					FwupdClientDownloadFlags download_flags,
					gpointer user_data,
					GCancellable *cancellable,
					GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(GPtrArray) devices =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	/* download */
	blob = fwupd_client_download_bytes2(client,
					    fwupd_release_get_locations(release),
					    download_flags,
					    cancellable,
					    error);
	if (blob == NULL)
		return FALSE;
	stream = fu_memory_input_stream_new_from_bytes(blob);
	fu_release_set_stream(FU_RELEASE(release), stream);

	/* install */
	g_ptr_array_add(devices, g_object_ref(device));
	fu_progress_reset(self->progress);
	return fu_engine_cli_install_stream(self, stream, devices, self->progress, error);
}

static gboolean
fu_engine_cli_sync_impl_modify_bios_setting(FwupdClient *client,
					    GHashTable *settings,
					    gpointer user_data,
					    GCancellable *cancellable,
					    GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_modify_bios_settings(self->engine, settings, FALSE, error);
}

static gboolean
fu_engine_cli_sync_impl_modify_config(FwupdClient *client,
				      const gchar *section,
				      const gchar *key,
				      const gchar *value,
				      gpointer user_data,
				      GCancellable *cancellable,
				      GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_modify_config(self->engine, section, key, value, error);
}

static gboolean
fu_engine_cli_sync_impl_modify_device(FwupdClient *client,
				      const gchar *device_id,
				      const gchar *key,
				      const gchar *value,
				      gpointer user_data,
				      GCancellable *cancellable,
				      GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_modify_device(self->engine, device_id, key, value, error);
}

static gboolean
fu_engine_cli_sync_impl_modify_remote(FwupdClient *client,
				      const gchar *remote_id,
				      const gchar *key,
				      const gchar *value,
				      gpointer user_data,
				      GCancellable *cancellable,
				      GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_modify_remote(self->engine, remote_id, key, value, error);
}

static gboolean
fu_engine_cli_sync_impl_refresh_remote(FwupdClient *client,
				       FwupdRemote *remote,
				       FwupdClientDownloadFlags download_flags,
				       gpointer user_data,
				       GCancellable *cancellable,
				       GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	g_autofree gchar *uri_raw = NULL;
	g_autofree gchar *uri_sig = NULL;
	g_autoptr(GBytes) bytes_raw = NULL;
	g_autoptr(GBytes) bytes_sig = NULL;

	/* signature */
	if (fwupd_remote_get_metadata_uri_sig(remote) == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOTHING_TO_DO,
			    "no metadata signature URI available for %s",
			    fwupd_remote_get_id(remote));
		return FALSE;
	}
	uri_sig = fwupd_remote_build_metadata_sig_uri(remote, error);
	if (uri_sig == NULL)
		return FALSE;
	bytes_sig =
	    fwupd_client_download_bytes(client, uri_sig, download_flags, cancellable, error);
	if (bytes_sig == NULL)
		return FALSE;
	if (!fwupd_remote_load_signature_bytes(remote, bytes_sig, error))
		return FALSE;

	/* payload */
	if (fwupd_remote_get_metadata_uri(remote) == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOTHING_TO_DO,
			    "no metadata URI available for %s",
			    fwupd_remote_get_id(remote));
		return FALSE;
	}
	uri_raw = fwupd_remote_build_metadata_uri(remote, error);
	if (uri_raw == NULL)
		return FALSE;
	bytes_raw =
	    fwupd_client_download_bytes(client, uri_raw, download_flags, cancellable, error);
	if (bytes_raw == NULL)
		return FALSE;

	/* send to daemon */
	g_info("updating %s", fwupd_remote_get_id(remote));
	return fu_engine_update_metadata_bytes(self->engine,
					       fwupd_remote_get_id(remote),
					       bytes_raw,
					       bytes_sig,
					       error);
}

static gboolean
fu_engine_cli_sync_impl_reset_config(FwupdClient *client,
				     const gchar *section,
				     gpointer user_data,
				     GCancellable *cancellable,
				     GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_reset_config(self->engine, section, error);
}

static GPtrArray *
fu_engine_cli_sync_impl_search(FwupdClient *client,
			       const gchar *token,
			       gpointer user_data,
			       GCancellable *cancellable,
			       GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_search(self->engine, token, error);
}

static gboolean
fu_engine_cli_sync_impl_set_feature_flags(FwupdClient *client,
					  FwupdFeatureFlags feature_flags,
					  gpointer user_data,
					  GCancellable *cancellable,
					  GError **error)
{
	FuConsole *console = fu_cli_get_console(FU_CLI(user_data));
	FuEngineCli *self = FU_ENGINE_CLI(user_data);

	/* non-TTY consoles cannot answer questions */
	if (!fu_console_get_interactive(console)) {
		fu_engine_request_set_feature_flags(
		    self->request,
		    FWUPD_FEATURE_FLAG_SWITCH_BRANCH | FWUPD_FEATURE_FLAG_FDE_WARNING |
			FWUPD_FEATURE_FLAG_COMMUNITY_TEXT | FWUPD_FEATURE_FLAG_SHOW_PROBLEMS);
		return TRUE;
	}

	/* set our implemented feature set */
	fu_engine_request_set_feature_flags(
	    self->request,
	    FWUPD_FEATURE_FLAG_DETACH_ACTION | FWUPD_FEATURE_FLAG_SWITCH_BRANCH |
		FWUPD_FEATURE_FLAG_FDE_WARNING | FWUPD_FEATURE_FLAG_UPDATE_ACTION |
		FWUPD_FEATURE_FLAG_COMMUNITY_TEXT | FWUPD_FEATURE_FLAG_SHOW_PROBLEMS |
		FWUPD_FEATURE_FLAG_REQUESTS | FWUPD_FEATURE_FLAG_REQUESTS_NON_GENERIC);
	return TRUE;
}

static gboolean
fu_engine_cli_sync_impl_unlock(FwupdClient *client,
			       const gchar *device_id,
			       gpointer user_data,
			       GCancellable *cancellable,
			       GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_unlock(self->engine, device_id, error);
}

static gboolean
fu_engine_cli_sync_impl_update_metadata(FwupdClient *client,
					const gchar *remote_id,
					const gchar *metadata_fn,
					const gchar *signature_fn,
					gpointer user_data,
					GCancellable *cancellable,
					GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	g_autoptr(GBytes) metadata_blob = NULL;
	g_autoptr(GBytes) signature_blob = NULL;

	metadata_blob = fu_bytes_get_contents(metadata_fn, error);
	if (metadata_blob == NULL)
		return FALSE;
	signature_blob = fu_bytes_get_contents(signature_fn, error);
	if (signature_blob == NULL)
		return FALSE;
	return fu_engine_update_metadata_bytes(self->engine,
					       remote_id,
					       metadata_blob,
					       signature_blob,
					       error);
}

static gboolean
fu_engine_cli_sync_impl_verify(FwupdClient *client,
			       const gchar *device_id,
			       gpointer user_data,
			       GCancellable *cancellable,
			       GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_verify(self->engine, device_id, self->progress, error);
}

static gboolean
fu_engine_cli_sync_impl_verify_update(FwupdClient *client,
				      const gchar *device_id,
				      gpointer user_data,
				      GCancellable *cancellable,
				      GError **error)
{
	FuEngineCli *self = FU_ENGINE_CLI(user_data);
	return fu_engine_verify_update(self->engine, device_id, self->progress, error);
}

static void
fu_engine_cli_init(FuEngineCli *self)
{
	static FwupdClientSyncImpl impl = {
	    .activate = fu_engine_cli_sync_impl_activate,
	    .clean_remote = fu_engine_cli_sync_impl_clean_remote,
	    .clear_results = fu_engine_cli_sync_impl_clear_results,
	    .connect = fu_engine_cli_sync_impl_connect,
	    .emulation_load = fu_engine_cli_sync_impl_emulation_load,
	    .emulation_save = fu_engine_cli_sync_impl_emulation_save,
	    .get_bios_settings = fu_engine_cli_sync_impl_get_bios_settings,
	    .get_details = fu_engine_cli_sync_impl_get_details,
	    .get_devices = fu_engine_cli_sync_impl_get_devices,
	    .get_devices_by_guid = fu_engine_cli_sync_impl_get_devices_by_guid,
	    .get_device_by_id = fu_engine_cli_sync_impl_get_device_by_id,
	    .get_downgrades = fu_engine_cli_sync_impl_get_downgrades,
	    .get_history = fu_engine_cli_sync_impl_get_history,
	    .get_host_security_attrs = fu_engine_cli_sync_impl_get_host_security_attrs,
	    .get_host_security_events = fu_engine_cli_sync_impl_get_host_security_events,
	    .get_releases = fu_engine_cli_sync_impl_get_releases,
	    .get_results = fu_engine_cli_sync_impl_get_results,
	    .get_upgrades = fu_engine_cli_sync_impl_get_upgrades,
	    .get_plugins = fu_engine_cli_sync_impl_get_plugins,
	    .get_remotes = fu_engine_cli_sync_impl_get_remotes,
	    .get_remote_by_id = fu_engine_cli_sync_impl_get_remote_by_id,
	    .get_report_metadata = fu_engine_cli_sync_impl_get_report_metadata,
	    .install = fu_engine_cli_sync_impl_install,
	    .install_release = fu_engine_cli_sync_impl_install_release,
	    .modify_bios_setting = fu_engine_cli_sync_impl_modify_bios_setting,
	    .modify_config = fu_engine_cli_sync_impl_modify_config,
	    .modify_device = fu_engine_cli_sync_impl_modify_device,
	    .modify_remote = fu_engine_cli_sync_impl_modify_remote,
	    .refresh_remote = fu_engine_cli_sync_impl_refresh_remote,
	    .reset_config = fu_engine_cli_sync_impl_reset_config,
	    .search = fu_engine_cli_sync_impl_search,
	    .set_feature_flags = fu_engine_cli_sync_impl_set_feature_flags,
	    .unlock = fu_engine_cli_sync_impl_unlock,
	    .update_metadata = fu_engine_cli_sync_impl_update_metadata,
	    .verify = fu_engine_cli_sync_impl_verify,
	    .verify_update = fu_engine_cli_sync_impl_verify_update,
	};
	self->lock_fd = -1;
	self->request = fu_engine_request_new(NULL);

	/* used for monitoring and downloading */
	fwupd_client_set_sync_impl(fu_cli_get_client(FU_CLI(self)), &impl, self, NULL);
	fwupd_client_set_daemon_version(fu_cli_get_client(FU_CLI(self)), PACKAGE_VERSION);
	fwupd_client_set_user_agent_for_package(fu_cli_get_client(FU_CLI(self)),
						"fwupdtool",
						PACKAGE_VERSION);

	/* when not using the engine */
	self->progress = fu_progress_new(G_STRLOC);
	g_signal_connect(self->progress,
			 "percentage-changed",
			 G_CALLBACK(fu_engine_cli_progress_percentage_changed_cb),
			 self);
	g_signal_connect(self->progress,
			 "status-changed",
			 G_CALLBACK(fu_engine_cli_progress_status_changed_cb),
			 self);
}

static void
fu_engine_cli_class_init(FuEngineCliClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_engine_cli_finalize;
}

int
main(int argc, char *argv[])
{ /* nocheck:lines */
	gboolean ignore_checksum = FALSE;
	gboolean ignore_requirements = FALSE;
	gboolean ignore_vid_pid = FALSE;
	gboolean no_search = FALSE;
	gint rc;
	g_auto(GStrv) plugin_glob = NULL;
	g_auto(GStrv) public_keys = NULL;
	g_autoptr(FuEngineCli) self = g_object_new(FU_TYPE_ENGINE_CLI, NULL);
	g_autoptr(GError) error_console = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) option_context = g_option_context_new(NULL);
	g_autofree gchar *cmd_descriptions = NULL;
	g_autofree gchar *destdir = NULL;
	const GOptionEntry options[] = {
	    {"ignore-checksum",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &ignore_checksum,
	     /* TRANSLATORS: command line option */
	     N_("Ignore firmware checksum failures"),
	     NULL},
	    {"ignore-vid-pid",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &ignore_vid_pid,
	     /* TRANSLATORS: command line option */
	     N_("Ignore firmware hardware mismatch failures"),
	     NULL},
	    {"ignore-requirements",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &ignore_requirements,
	     /* TRANSLATORS: command line option */
	     N_("Ignore non-critical firmware requirements"),
	     NULL},
	    {"no-search",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &no_search,
	     /* TRANSLATORS: command line option */
	     N_("Do not search the firmware when parsing"),
	     NULL},
	    {"plugins",
	     '\0',
	     0,
	     G_OPTION_ARG_STRING_ARRAY,
	     &plugin_glob,
	     /* TRANSLATORS: command line option */
	     N_("Manually enable specific plugins"),
	     NULL},
	    {"plugin-whitelist",
	     '\0',
	     G_OPTION_FLAG_HIDDEN,
	     G_OPTION_ARG_STRING_ARRAY,
	     &plugin_glob,
	     /* TRANSLATORS: command line option */
	     N_("Manually enable specific plugins"),
	     NULL},
	    {"public-keys",
	     '\0',
	     0,
	     G_OPTION_ARG_STRING_ARRAY,
	     &public_keys,
	     _("Location of public key directories used for JCat verification"),
	     NULL},
	    {"prepare",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &self->prepare_blob,
	     /* TRANSLATORS: command line option */
	     N_("Run the plugin composite prepare routine when using install-blob"),
	     NULL},
	    {"cleanup",
	     '\0',
	     0,
	     G_OPTION_ARG_NONE,
	     &self->cleanup_blob,
	     /* TRANSLATORS: command line option */
	     N_("Run the plugin composite cleanup routine when using install-blob"),
	     NULL},
	    {"destdir",
	     '\0',
	     0,
	     G_OPTION_ARG_STRING,
	     &destdir,
	     _("Prefix for import and output files"),
	     NULL},
	    {NULL}};

#ifdef _WIN32
	/* workaround Windows setting the codepage to 1252 */
	(void)g_setenv("LANG", "C.UTF-8", FALSE);
#endif

	setlocale(LC_ALL, "");

	bindtextdomain(GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);
	g_set_prgname(fu_cli_get_prgname(argv[0]));

	/* add commands */
	fu_cli_cmd_array_add_common(FU_CLI(self));
	fu_cli_cmd_array_add(FU_CLI(self),
			     "smbios-dump",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILE"),
			     /* TRANSLATORS: command description */
			     _("Dump SMBIOS data from a file"),
			     fu_engine_cli_smbios_dump);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "get-device-flags",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Get all device flags supported by fwupd"),
			     fu_engine_cli_get_device_flags);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "install-blob",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME DEVICE-ID [VERSION]"),
			     /* TRANSLATORS: command description */
			     _("Install a raw firmware blob on a device"),
			     fu_engine_cli_install_blob);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "attach",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("DEVICE-ID|GUID"),
			     /* TRANSLATORS: command description */
			     _("Attach to firmware mode"),
			     fu_engine_cli_attach);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "get-report-metadata",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Get device report metadata"),
			     fu_engine_cli_get_report_metadata);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "detach",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("DEVICE-ID|GUID"),
			     /* TRANSLATORS: command description */
			     _("Detach to bootloader mode"),
			     fu_engine_cli_detach);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "unbind-driver",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Unbind current driver"),
			     fu_engine_cli_unbind_driver);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "bind-driver",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("SUBSYSTEM DRIVER [DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Bind new kernel driver"),
			     fu_engine_cli_bind_driver);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "export-hwids",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("HWIDS-FILE"),
			     /* TRANSLATORS: command description */
			     _("Save a file that allows generation of hardware IDs"),
			     fu_engine_cli_export_hwids);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "self-sign",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("TEXT"),
			     /* TRANSLATORS: command description */
			     C_("command-description", "Sign data using the client certificate"),
			     fu_engine_cli_self_sign);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "firmware-sign",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME CERTIFICATE PRIVATE-KEY"),
			     /* TRANSLATORS: command description */
			     _("Sign a firmware with a new key"),
			     fu_engine_cli_firmware_sign);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "firmware-dump",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME [DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Read a firmware blob from a device"),
			     fu_engine_cli_firmware_dump);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "firmware-read",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME [DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Read a firmware from a device"),
			     fu_engine_cli_firmware_read);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "firmware-patch",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME OFFSET DATA [FIRMWARE-TYPE]"),
			     /* TRANSLATORS: command description */
			     _("Patch a firmware blob at a known offset"),
			     fu_engine_cli_firmware_patch);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "firmware-convert",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME-SRC FILENAME-DST [FIRMWARE-TYPE-SRC] [FIRMWARE-TYPE-DST]"),
			     /* TRANSLATORS: command description */
			     _("Convert a firmware file"),
			     fu_engine_cli_firmware_convert);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "firmware-build",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("BUILDER-XML FILENAME-DST"),
			     /* TRANSLATORS: command description */
			     _("Build a firmware file"),
			     fu_engine_cli_firmware_build);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "firmware-parse",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME [FIRMWARE-TYPE]"),
			     /* TRANSLATORS: command description */
			     _("Parse and show details about a firmware file"),
			     fu_engine_cli_firmware_parse);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "firmware-export",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME [FIRMWARE-TYPE]"),
			     /* TRANSLATORS: command description */
			     _("Export a firmware file structure to XML"),
			     fu_engine_cli_firmware_export);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "firmware-extract",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME [FIRMWARE-TYPE]"),
			     /* TRANSLATORS: command description */
			     _("Extract a firmware blob to images"),
			     fu_engine_cli_firmware_extract);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "get-firmware-types",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("List the available firmware types"),
			     fu_engine_cli_get_firmware_types);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "get-firmware-gtypes",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("List the available firmware GTypes"),
			     fu_engine_cli_get_firmware_gtypes);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "esp-mount",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Mounts the ESP"),
			     fu_engine_cli_esp_mount);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "esp-unmount",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Unmounts the ESP"),
			     fu_engine_cli_esp_unmount);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "esp-list",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Lists files on the ESP"),
			     fu_engine_cli_esp_list);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "clear-history",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Erase all firmware update history"),
			     fu_engine_cli_clear_history);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "build-cabinet",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("ARCHIVE FIRMWARE METAINFO [FIRMWARE] [METAINFO] [JCATFILE]"),
			     /* TRANSLATORS: command description */
			     _("Build a cabinet archive from a firmware blob and XML metadata"),
			     fu_engine_cli_build_cabinet);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "efivar-list",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     C_("command-argument", "GUID"),
			     /* TRANSLATORS: command description */
			     _("List EFI variables with a specific GUID"),
			     fu_engine_cli_efivar_list);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "efiboot-info,efivar-boot",
			     /* TRANSLATORS: lowercase sub-command (do not translate): then
			      * uppercase, spaces->dashes */
			     NULL,
			     /* TRANSLATORS: command description */
			     _("List EFI boot parameters"),
			     fu_engine_cli_efiboot_info);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "efiboot-next",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("INDEX"),
			     /* TRANSLATORS: command description */
			     _("Set the EFI boot next"),
			     fu_engine_cli_efiboot_next);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "efiboot-order",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("INDEX1,INDEX2"),
			     /* TRANSLATORS: command description */
			     _("Set the EFI boot order"),
			     fu_engine_cli_efiboot_order);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "efiboot-delete",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("INDEX"),
			     /* TRANSLATORS: command description */
			     _("Delete an EFI boot entry"),
			     fu_engine_cli_efiboot_delete);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "efiboot-create",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("INDEX NAME TARGET [MOUNTPOINT]"),
			     /* TRANSLATORS: command description */
			     _("Create an EFI boot entry"),
			     fu_engine_cli_efiboot_create);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "efiboot-hive",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("INDEX KEY [VALUE]"),
			     /* TRANSLATORS: command description */
			     _("Set or remove an EFI boot hive entry"),
			     fu_engine_cli_efiboot_hive);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "efiboot-files,efivar-files",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     NULL,
			     /* TRANSLATORS: command description */
			     _("List EFI boot files"),
			     fu_engine_cli_efivar_files);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "reboot-cleanup",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[DEVICE]"),
			     /* TRANSLATORS: command description */
			     _("Run the post-reboot cleanup action"),
			     fu_engine_cli_reboot_cleanup);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "enable-test-devices",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Enables virtual testing devices"),
			     fu_engine_cli_enable_test_devices);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "disable-test-devices",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Disables virtual testing devices"),
			     fu_engine_cli_disable_test_devices);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "get-version-formats",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Get all known version formats"),
			     fu_engine_cli_get_verfmts);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "vercmp",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("VERSION1 VERSION2 [FORMAT]"),
			     /* TRANSLATORS: command description */
			     _("Compares two versions for equality"),
			     fu_engine_cli_vercmp);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "crc",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("KIND FILENAME"),
			     /* TRANSLATORS: command description */
			     _("Calculates a CRC of a file"),
			     fu_engine_cli_crc);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "crc-find",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("CRC FILENAME"),
			     /* TRANSLATORS: command description */
			     _("Finds an algorithm that matches the file CRC"),
			     fu_engine_cli_crc_find);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "tpm-eventlog",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[PCR]"),
			     /* TRANSLATORS: command description */
			     _("Parse the system PCR eventlog"),
			     fu_engine_cli_tpm_eventlog);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "jcat-info",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME"),
			     /* TRANSLATORS: command description */
			     _("Show information about a JCat file"),
			     fu_engine_cli_jcat_info);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "jcat-add-alias",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME ID ALIAS_ID"),
			     /* TRANSLATORS: command description */
			     _("Add an alias for a specific JCat item"),
			     fu_engine_cli_jcat_add_alias);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "jcat-remove-alias",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME ID ALIAS_ID"),
			     /* TRANSLATORS: command description */
			     _("Remove an alias for a specific JCat item"),
			     fu_engine_cli_jcat_remove_alias);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "jcat-export",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME [KIND]"),
			     /* TRANSLATORS: command description */
			     _("Exports all embedded signatures to files"),
			     fu_engine_cli_jcat_export);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "jcat-import",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME DATA DETACHED_KEY"),
			     /* TRANSLATORS: command description */
			     _("Import an existing signature to a JCat file"),
			     fu_engine_cli_jcat_import);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "jcat-self-sign",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME SOURCE [KIND[:VARIANT]] [TARGET]"),
			     /* TRANSLATORS: command description */
			     _("Add a self-signed signature to a JCat file"),
			     fu_engine_cli_jcat_self_sign);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "jcat-sign",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME SOURCE CERT PRIVKEY [KIND[:VARIANT]] [TARGET]"),
			     /* TRANSLATORS: command description */
			     _("Add a signature to a JCat file"),
			     fu_engine_cli_jcat_sign);
	fu_cli_cmd_array_add(FU_CLI(self),
			     "jcat-verify",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME [SOURCE]"),
			     /* TRANSLATORS: command description */
			     _("Verify a signature from a JCat file"),
			     fu_engine_cli_jcat_verify);

	/* sort by command name */
	fu_cli_cmd_array_sort(FU_CLI(self));

	/* get a list of the commands */
	cmd_descriptions = fu_cli_cmd_array_to_string(FU_CLI(self));
	g_option_context_set_summary(option_context, cmd_descriptions);
	g_option_context_set_description(
	    option_context,
	    /* TRANSLATORS: CLI description */
	    _("This tool allows an administrator to use the fwupd plugins "
	      "without being installed on the host system."));

	/* TRANSLATORS: program name */
	g_set_application_name(_("Firmware Utility"));
	g_option_context_add_main_entries(option_context, options, NULL);
	g_option_context_add_group(option_context, fu_cli_get_option_group(FU_CLI(self)));
	g_option_context_add_group(option_context, fu_debug_get_option_group());
	if (!g_option_context_parse(option_context, &argc, &argv, &error)) {
		fu_console_print(fu_cli_get_console(FU_CLI(self)),
				 "%s: %s",
				 /* TRANSLATORS: the user didn't read the man page */
				 _("Failed to parse arguments"),
				 error->message);
		return EXIT_FAILURE;
	}
	fu_progress_set_profile(self->progress, g_log_get_debug_enabled());

	/* allow disabling SSL strict mode for broken corporate proxies */
	if (fu_cli_has_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_DISABLE_SSL_STRICT)) {
		fu_console_print_full(fu_cli_get_console(FU_CLI(self)),
				      FU_CONSOLE_PRINT_FLAG_WARNING,
				      "%s\n",
				      /* TRANSLATORS: try to help */
				      _("Ignoring SSL strict checks, "
					"to do this automatically in the future "
					"export DISABLE_SSL_STRICT in your environment"));
		(void)g_setenv("DISABLE_SSL_STRICT", "1", TRUE);
	}

	/* set flags */
	if (no_search)
		self->parse_flags |= FU_FIRMWARE_PARSE_FLAG_NO_SEARCH;
	if (ignore_checksum)
		self->parse_flags |= FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM;
	if (ignore_vid_pid)
		self->parse_flags |= FU_FIRMWARE_PARSE_FLAG_IGNORE_VID_PID;
	if (ignore_requirements)
		fu_cli_add_arg_flag(FU_CLI(self), FU_CLI_ARG_FLAG_IGNORE_REQUIREMENTS);
	self->destdir = g_strdup(destdir != NULL ? destdir : ".");

	/* load engine */
	self->ctx = fu_context_new();
	fu_context_set_main_context(self->ctx, fu_cli_get_main_ctx(FU_CLI(self)));
	g_signal_connect(FU_CONTEXT(self->ctx),
			 "notify::flags",
			 G_CALLBACK(fu_engine_cli_context_flags_notify_cb),
			 self);
	fu_context_add_flag(self->ctx, FU_CONTEXT_FLAG_NO_IDLE_SOURCES);
	self->engine = fu_engine_new(self->ctx);
	g_signal_connect(FU_ENGINE(self->engine),
			 "device-request",
			 G_CALLBACK(fu_engine_cli_update_device_request_cb),
			 self);
	g_signal_connect(FU_ENGINE(self->engine),
			 "device-added",
			 G_CALLBACK(fu_engine_cli_engine_device_added_cb),
			 self);
	g_signal_connect(FU_ENGINE(self->engine),
			 "device-removed",
			 G_CALLBACK(fu_engine_cli_engine_device_removed_cb),
			 self);
	g_signal_connect(FU_ENGINE(self->engine),
			 "status-changed",
			 G_CALLBACK(fu_engine_cli_engine_status_changed_cb),
			 self);

	/* jcat */
	self->jcat_context = fu_jcat_context_new();
	fu_jcat_context_allow_blob_kind(self->jcat_context, FWUPD_JCAT_BLOB_KIND_PKCS7);
	fu_jcat_context_allow_blob_kind(self->jcat_context, FWUPD_JCAT_BLOB_KIND_SHA256);
	fu_jcat_context_allow_blob_kind(self->jcat_context, FWUPD_JCAT_BLOB_KIND_SHA512);
	if (public_keys != NULL) {
		for (guint i = 0; public_keys[i] != NULL; i++)
			fu_jcat_context_add_public_keys(self->jcat_context, public_keys[i]);
	}

	/* any plugin allowlist specified */
	for (guint i = 0; plugin_glob != NULL && plugin_glob[i] != NULL; i++)
		fu_engine_add_plugin_filter(self->engine, plugin_glob[i]);

	/* process command line arguments */
	rc = fu_cli_main(FU_CLI(self), argc, argv);

	/* a good place to do the traceback */
	if (fu_progress_get_profile(self->progress)) {
		g_autofree gchar *str = fu_progress_traceback(self->progress);
		if (str != NULL)
			fu_console_print_literal(fu_cli_get_console(FU_CLI(self)), str);
	}

	/* success */
	return rc;
}
