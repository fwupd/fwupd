/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "bulk_controller.h"
#include "proto_manager.h"

/* Firmware upgrade time */
#define TIMEOUT_20M 1200

/* Callback prototypes */
void bulk_error_cb (gint error_code, BulkInterface bulk_int, const gchar *data, guint32 size, void *user_data);
void read_sync_data_cb (const gchar *data, guint32 size, void *user_data);
void read_upd_data_cb (const gchar *data, guint32 size, void *user_data);
void send_data_sync_cb (gint error_code, gint status, gint id, void *data);
void bulk_file_transfer_cb (FileTransferState state, gint progress, BulkInterface bulk_intf, void *data);

void
bulk_error_cb (gint error_code, BulkInterface bulk_int, const gchar *data, guint32 size, void *user_data)
{
        ApiUserData *api_data = (ApiUserData*) user_data;
        if (ERRORCODE_NO_ERROR != error_code) {
                g_debug("\n%s with reason : %s %d", api_data->prog_name, data, error_code);
        }
}

void
read_sync_data_cb (const gchar *data, guint32 size, void *user_data)
{
        g_debug("Length of data received %u", size);
}

void
read_upd_data_cb (const gchar *data, guint32 size, void *user_data)
{
}

void
send_data_sync_cb (gint error_code, gint status, gint id, void *data)
{
        ApiUserData *user_data = (ApiUserData*) data;

	(status == TRANSFER_SUCCESS)
	    ? g_debug("Send data sync success ID: %d ErrorCode: %d", id, error_code)
	    : g_warning("Send data sync failed ID: %d ErrorCode: %d", id, error_code);
	g_cond_signal(&user_data->test_upd_cond);
}

void
bulk_file_transfer_cb (FileTransferState state, gint progress, BulkInterface bulk_intf, void *data)
{
        ApiUserData *user_data = (ApiUserData*) data;

        switch (state) {
                case TRANSFER_HASH_STARTED:
                        g_debug("[%s] : File transfer hash in progress %u", user_data->prog_name, bulk_intf);
                        break;
                case TRANSFER_INIT_STARTED:
                        g_debug("[%s] : File transfer init in progress %u", user_data->prog_name, bulk_intf);
                        break;
                case TRANSFER_STARTED:
                        g_debug("[%s] : File transfer started for interface %u", user_data->prog_name, bulk_intf);
                        break;
                case TRANSFER_FAILED:
                        g_warning("[%s] : File transfer failed for interface %u", user_data->prog_name, bulk_intf);
                        g_cond_signal(&user_data->test_upd_cond);
                        break;
                case TRANSFER_INPROGRESS:
                        if (BULK_INTERFACE_UPD == bulk_intf) {
				fflush(stdout);
				FuDevice *device = (FuDevice *)user_data->device_ptr;
				if (device)
					fu_device_set_progress(device, progress);
			} else if (BULK_INTERFACE_SYNC == bulk_intf) {
				fflush(stdout);
			}
			if (progress == 100) {
			}
			break;
		case TRANSFER_COMPLETED:
			g_debug("[%s] :  File transfer completed for interface %u",
				user_data->prog_name,
				bulk_intf);
			g_cond_signal(&user_data->test_upd_cond);
			break;
		default:
			break;
		}
}

void
get_device_version(LogiBulkController *obj, void *data)
{
        Message message;
        gint ret;
        ReturnValue *ret_val;
        ApiUserData *user_data = (ApiUserData*) data;
        g_cond_init (&user_data->test_upd_cond);
        g_mutex_init (&user_data->test_upd_mutex);
        g_mutex_lock (&user_data->test_upd_mutex);

	memset(&message, '\0', sizeof(message));

	ret = proto_manager_generate_get_device_info_request(&message);
	if (!ret && message.data) {
		ret_val = logibulkcontroller_send_data_sync(obj, message.data, message.len);
		if ((ERRORCODE_NO_ERROR != ret_val->error_code) ||
		    (ERRORCODE_SEND_DATA_REQUEST_PUSHED_TO_QUEUE != ret_val->error_code)) {
			g_warning("Error in send data %u", ret_val->error_code);
		}
		g_free(message.data);
	}

	if (g_cond_wait_until(&user_data->test_upd_cond,
			      &user_data->test_upd_mutex,
			      g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND)) {
		g_mutex_unlock(&user_data->test_upd_mutex);
		g_mutex_lock(&user_data->test_upd_mutex);
	}
	return;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
                  FuDevice *device,
                  GBytes *blob_fw,
                  FwupdInstallFlags flags,
                  GError **error)
{
        ApiUserData user_data;
        LogiBulkController *obj;
        BulkControllerCallbacks bulkcb = {bulk_error_cb, bulk_file_transfer_cb, read_upd_data_cb,
                                           read_sync_data_cb, send_data_sync_cb };
        g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
        user_data.prog_name = g_strdup ("Logitech Rally Bar Mini");
        user_data.device_ptr = (FuDevice *)device;
	obj = logibulkcontroller_create_bulk_controller(0x46d, 0x8d3, bulkcb, &user_data);
	fu_device_set_status(device, FWUPD_STATUS_DEVICE_WRITE);
	logibulkcontroller_send_file_upd(obj, blob_fw, g_bytes_get_size(blob_fw), FALSE);
	if (g_cond_wait_until(&user_data.test_upd_cond,
			      &user_data.test_upd_mutex,
			      g_get_monotonic_time() + TIMEOUT_20M * G_TIME_SPAN_SECOND)) {
		g_mutex_unlock(&user_data.test_upd_mutex);
		g_mutex_lock(&user_data.test_upd_mutex);
	}
	fu_device_set_status(device, FWUPD_STATUS_DEVICE_VERIFY);
	logibulkcontroller_close_device(obj);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	return TRUE;
}

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	g_debug("Terminating Logitech bulk controller plugin");
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	g_autoptr(FuDevice) device = NULL;
	device = fu_device_new_with_context (ctx);
	fu_device_set_id (device, "FakeDevice");
	fu_device_add_guid (device, "b585990a-003e-5270-89d5-3705a17f9a43");
	fu_device_set_name (device, "Rally Bar Mini");
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_protocol (device, "com.acme.test");
	fu_device_set_vendor (device, "Logitech");
	fu_device_add_vendor_id (device, "USB:0x046D");
	fu_device_set_version_format (device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version (device, "1.2.3");
	if (g_strcmp0 (g_getenv ("FWUPD_PLUGIN_LOGITECH_BULKCONTROLLER"), "registration") == 0) {
		fu_plugin_device_register (plugin, device);
		if (fu_device_get_metadata (device, "BestDevice") == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "Device not set by another plugin");
			return FALSE;
		}
	}
	fu_plugin_device_add (plugin, device);
	return TRUE;
}

void
fu_plugin_device_registered (FuPlugin *plugin, FuDevice *device)
{
	fu_device_set_metadata (device, "BestDevice", "/dev/urandom");
}

gboolean
fu_plugin_verify (FuPlugin *plugin,
		  FuDevice *device,
		  FuPluginVerifyFlags flags,
		  GError **error)
{
	if (g_strcmp0 (fu_device_get_version (device), "1.2.2") == 0) {
		fu_device_add_checksum (device, "90d0ad436d21e0687998cd2127b2411135e1f730");
		fu_device_add_checksum (device, "921631916a60b295605dbae6a0309f9b64e2401b3de8e8506e109fc82c586e3a");
		return TRUE;
	}
	if (g_strcmp0 (fu_device_get_version (device), "1.2.3") == 0) {
		fu_device_add_checksum (device, "7998cd212721e068b2411135e1f90d0ad436d730");
		fu_device_add_checksum (device, "dbae6a0309b3de8e850921631916a60b2956056e109fc82c586e3f9b64e2401a");
		return TRUE;
	}
	if (g_strcmp0 (fu_device_get_version (device), "1.2.4") == 0) {
		fu_device_add_checksum (device, "2b8546ba805ad10bf8a2e5ad539d53f303812ba5");
		fu_device_add_checksum (device, "b546c241029ce4e16c99eb6bfd77b86e4490aa3826ba71b8a4114e96a2d69bcd");
		return TRUE;
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "no checksum for %s", fu_device_get_version (device));
	return FALSE;
}
