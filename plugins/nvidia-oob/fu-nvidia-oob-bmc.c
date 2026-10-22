/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-nvidia-oob-bmc.h"

#define FU_NVIDIA_OOB_UPDATE_SERVICE_URI "/redfish/v1/UpdateService"

struct _FuNvidiaOobBmc {
	GObject parent_instance;
	gchar *base_uri;
	gboolean cacheck;
	gchar *username;
	gchar *password;
	gchar *session_key_file;
	gchar *session_key;
	GFileMonitor *session_key_monitor;
	gchar *push_uri;
	guint64 max_image_size;
	gchar *uploaded_checksum;
	gboolean composite;
	FuDevice *written_device;
	GPtrArray *devices; /* of FuDevice */
};

G_DEFINE_TYPE(FuNvidiaOobBmc, fu_nvidia_oob_bmc, G_TYPE_OBJECT)

gboolean
fu_nvidia_oob_bmc_set_uri(FuNvidiaOobBmc *self, const gchar *uri, GError **error)
{
	const gchar *scheme;
	const gchar *host;
	g_autoptr(GUri) guri = NULL;

	g_return_val_if_fail(FU_IS_NVIDIA_OOB_BMC(self), FALSE);
	g_return_val_if_fail(uri != NULL, FALSE);

	guri = g_uri_parse(uri, G_URI_FLAGS_NONE, error);
	if (guri == NULL)
		return FALSE;
	scheme = g_uri_get_scheme(guri);
	host = g_uri_get_host(guri);
	if ((g_strcmp0(scheme, "https") != 0 && g_strcmp0(scheme, "http") != 0) || host == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "Uri %s is not an http or https address",
			    uri);
		return FALSE;
	}
	g_free(self->base_uri);
	if (g_uri_get_port(guri) > 0)
		self->base_uri = g_strdup_printf("%s://%s:%d", scheme, host, g_uri_get_port(guri));
	else
		self->base_uri = g_strdup_printf("%s://%s", scheme, host);
	return TRUE;
}

void
fu_nvidia_oob_bmc_set_cacheck(FuNvidiaOobBmc *self, gboolean cacheck)
{
	g_return_if_fail(FU_IS_NVIDIA_OOB_BMC(self));
	self->cacheck = cacheck;
}

void
fu_nvidia_oob_bmc_set_credentials(FuNvidiaOobBmc *self,
				  const gchar *username,
				  const gchar *password)
{
	g_return_if_fail(FU_IS_NVIDIA_OOB_BMC(self));
	g_set_str(&self->username, username);
	g_set_str(&self->password, password);
}

static gboolean
fu_nvidia_oob_bmc_read_session_key(FuNvidiaOobBmc *self, GError **error)
{
	g_autofree gchar *session_key = NULL;

	if (!g_file_get_contents(self->session_key_file, &session_key, NULL, error)) {
		fwupd_error_convert(error);
		return FALSE;
	}
	g_strstrip(session_key);
	if (session_key[0] == '\0') {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "%s is empty",
			    self->session_key_file);
		return FALSE;
	}
	g_set_str(&self->session_key, session_key);
	return TRUE;
}

static void
fu_nvidia_oob_bmc_session_key_changed_cb(GFileMonitor *monitor,
					 GFile *file,
					 GFile *other_file,
					 GFileMonitorEvent event_type,
					 gpointer user_data)
{
	FuNvidiaOobBmc *self = FU_NVIDIA_OOB_BMC(user_data);
	g_autoptr(GError) error_local = NULL;

	/* also sent after a file is renamed into place */
	if (event_type != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
		return;

	/* a failed or empty read leaves the previous key in place */
	if (!fu_nvidia_oob_bmc_read_session_key(self, &error_local)) {
		g_debug("keeping the previous session key: %s", error_local->message);
		return;
	}
	g_debug("reloaded the session key from %s", self->session_key_file);
}

void
fu_nvidia_oob_bmc_set_session_key_file(FuNvidiaOobBmc *self, const gchar *session_key_file)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file = NULL;

	g_return_if_fail(FU_IS_NVIDIA_OOB_BMC(self));

	g_set_str(&self->session_key_file, session_key_file);
	g_clear_pointer(&self->session_key, g_free);
	g_clear_object(&self->session_key_monitor);
	if (session_key_file == NULL)
		return;

	file = g_file_new_for_path(session_key_file);
	self->session_key_monitor =
	    g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, &error_local);
	if (self->session_key_monitor == NULL) {
		g_warning("cannot watch %s, so it is read on every request: %s",
			  session_key_file,
			  error_local->message);
		return;
	}
	g_signal_connect_object(self->session_key_monitor,
				"changed",
				G_CALLBACK(fu_nvidia_oob_bmc_session_key_changed_cb),
				self,
				0);
}

FuNvidiaOobRequest *
fu_nvidia_oob_bmc_request_new(FuNvidiaOobBmc *self)
{
	FuNvidiaOobRequest *request;

	g_return_val_if_fail(FU_IS_NVIDIA_OOB_BMC(self), NULL);

	request = fu_nvidia_oob_request_new(self->base_uri, self->cacheck);
	if (self->session_key_file != NULL) {
		g_autoptr(GError) error_local = NULL;

		/* the monitor keeps the value current once read; without one, re-read */
		if ((self->session_key == NULL || self->session_key_monitor == NULL) &&
		    !fu_nvidia_oob_bmc_read_session_key(self, &error_local))
			g_debug("no session key: %s", error_local->message);
		if (self->session_key != NULL)
			fu_nvidia_oob_request_set_session_key(request, self->session_key);
	} else if (self->username != NULL && self->password != NULL) {
		fu_nvidia_oob_request_set_credentials(request, self->username, self->password);
	}
	return request;
}

gboolean
fu_nvidia_oob_bmc_setup(FuNvidiaOobBmc *self, gchar **inventory_uri, GError **error)
{
	const gchar *tmp;
	gboolean service_enabled = TRUE;
	gint64 max_image_size = -1;
	g_autoptr(FuNvidiaOobRequest) request = NULL;
	g_autoptr(FwupdJsonObject) json_inventory = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;

	g_return_val_if_fail(FU_IS_NVIDIA_OOB_BMC(self), FALSE);
	g_return_val_if_fail(inventory_uri != NULL, FALSE);

	if (self->base_uri == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no Uri set");
		return FALSE;
	}
	request = fu_nvidia_oob_bmc_request_new(self);
	if (!fu_nvidia_oob_request_perform(request, FU_NVIDIA_OOB_UPDATE_SERVICE_URI, error))
		return FALSE;
	json_obj = fu_nvidia_oob_request_get_json_object(request, error);
	if (json_obj == NULL)
		return FALSE;
	if (!fwupd_json_object_get_boolean_with_default(json_obj,
							"ServiceEnabled",
							&service_enabled,
							TRUE,
							error))
		return FALSE;
	if (!service_enabled) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "the BMC update service is not enabled");
		return FALSE;
	}
	tmp = fwupd_json_object_get_string(json_obj, "MultipartHttpPushUri", NULL);
	if (tmp == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "the BMC reports no MultipartHttpPushUri");
		return FALSE;
	}
	g_set_str(&self->push_uri, tmp);
	if (!fwupd_json_object_get_integer_with_default(json_obj,
							"MaxImageSizeBytes",
							&max_image_size,
							-1,
							error))
		return FALSE;
	if (max_image_size > 0)
		self->max_image_size = (guint64)max_image_size;

	json_inventory = fwupd_json_object_get_object(json_obj, "FirmwareInventory", error);
	if (json_inventory == NULL)
		return FALSE;
	tmp = fwupd_json_object_get_string(json_inventory, "@odata.id", error);
	if (tmp == NULL)
		return FALSE;
	*inventory_uri = g_strdup(tmp);
	return TRUE;
}

const gchar *
fu_nvidia_oob_bmc_get_push_uri(FuNvidiaOobBmc *self)
{
	g_return_val_if_fail(FU_IS_NVIDIA_OOB_BMC(self), NULL);
	return self->push_uri;
}

guint64
fu_nvidia_oob_bmc_get_max_image_size(FuNvidiaOobBmc *self)
{
	g_return_val_if_fail(FU_IS_NVIDIA_OOB_BMC(self), 0);
	return self->max_image_size;
}

const gchar *
fu_nvidia_oob_bmc_get_uploaded_checksum(FuNvidiaOobBmc *self)
{
	g_return_val_if_fail(FU_IS_NVIDIA_OOB_BMC(self), NULL);
	return self->uploaded_checksum;
}

void
fu_nvidia_oob_bmc_set_uploaded_checksum(FuNvidiaOobBmc *self, const gchar *checksum)
{
	g_return_if_fail(FU_IS_NVIDIA_OOB_BMC(self));
	g_set_str(&self->uploaded_checksum, checksum);
}

gboolean
fu_nvidia_oob_bmc_get_composite(FuNvidiaOobBmc *self)
{
	g_return_val_if_fail(FU_IS_NVIDIA_OOB_BMC(self), FALSE);
	return self->composite;
}

void
fu_nvidia_oob_bmc_set_composite(FuNvidiaOobBmc *self, gboolean composite)
{
	g_return_if_fail(FU_IS_NVIDIA_OOB_BMC(self));
	self->composite = composite;
}

FuDevice *
fu_nvidia_oob_bmc_get_written_device(FuNvidiaOobBmc *self)
{
	g_return_val_if_fail(FU_IS_NVIDIA_OOB_BMC(self), NULL);
	return self->written_device;
}

void
fu_nvidia_oob_bmc_set_written_device(FuNvidiaOobBmc *self, FuDevice *device)
{
	g_return_if_fail(FU_IS_NVIDIA_OOB_BMC(self));
	g_set_object(&self->written_device, device);
}

void
fu_nvidia_oob_bmc_add_device(FuNvidiaOobBmc *self, FuDevice *device)
{
	g_return_if_fail(FU_IS_NVIDIA_OOB_BMC(self));
	g_return_if_fail(FU_IS_DEVICE(device));
	g_ptr_array_add(self->devices, g_object_ref(device));
}

GPtrArray *
fu_nvidia_oob_bmc_get_devices(FuNvidiaOobBmc *self)
{
	g_return_val_if_fail(FU_IS_NVIDIA_OOB_BMC(self), NULL);
	return self->devices;
}

void
fu_nvidia_oob_bmc_remove_devices(FuNvidiaOobBmc *self)
{
	g_return_if_fail(FU_IS_NVIDIA_OOB_BMC(self));
	g_ptr_array_set_size(self->devices, 0);
	g_clear_object(&self->written_device);
}

FuNvidiaOobBmc *
fu_nvidia_oob_bmc_new(void)
{
	return g_object_new(FU_TYPE_NVIDIA_OOB_BMC, NULL);
}

static void
fu_nvidia_oob_bmc_init(FuNvidiaOobBmc *self)
{
	self->devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

static void
fu_nvidia_oob_bmc_finalize(GObject *object)
{
	FuNvidiaOobBmc *self = FU_NVIDIA_OOB_BMC(object);
	if (self->session_key_monitor != NULL)
		g_object_unref(self->session_key_monitor);
	if (self->written_device != NULL)
		g_object_unref(self->written_device);
	g_ptr_array_unref(self->devices);
	g_free(self->base_uri);
	g_free(self->username);
	g_free(self->password);
	g_free(self->session_key_file);
	g_free(self->session_key);
	g_free(self->push_uri);
	g_free(self->uploaded_checksum);
	G_OBJECT_CLASS(fu_nvidia_oob_bmc_parent_class)->finalize(object);
}

static void
fu_nvidia_oob_bmc_class_init(FuNvidiaOobBmcClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_nvidia_oob_bmc_finalize;
}
