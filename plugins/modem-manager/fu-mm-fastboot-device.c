/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"
#include "fu-mm-mbim-device.h"
#include "fu-mm-fastboot-device.h"

struct _FuMmFastbootDevice {
	FuMmDevice parent_instance;
	gchar *detach_at;
};

G_DEFINE_TYPE(FuMmFastbootDevice, fu_mm_fastboot_device, FU_TYPE_MM_DEVICE)

#define FU_MM_FASTBOOT_DEVICE_FLAG_DETACH_AT_NO_RESPONSE "detach-at-fastboot-has-no-response"

#define ROLLING_WIRELESS_VENDOR_ID 0x33F8

static void
fu_mm_fastboot_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuMmFastbootDevice *self = FU_MM_FASTBOOT_DEVICE(device);
	fwupd_codec_string_append(str, idt, "DetachAt", self->detach_at);
}

void
fu_mm_fastboot_device_set_detach_at(FuMmFastbootDevice *self, const gchar *detach_at)
{
	g_return_if_fail(FU_IS_MM_FASTBOOT_DEVICE(self));
	g_return_if_fail(detach_at != NULL);
	g_free(self->detach_at);
	self->detach_at = g_strdup(detach_at);
}

static gboolean
fu_mm_rolling_deatch_at_cmd_transaction_sync(const gchar *device_file,
                                          const gchar *detach_at,
                                          GError **error)
{
    g_autoptr(MbimMessage) request = NULL;
    g_autoptr(MbimMessage) response = NULL;
    g_autofree gchar *cmd_crlf = NULL;
    const guint8 *resp_data = NULL;
    guint32 resp_size = 0;

    g_return_val_if_fail(device_file != NULL, FALSE);
    g_return_val_if_fail(detach_at != NULL, FALSE);

    /* AT channel pre-check with "AT" */
    cmd_crlf = g_strdup_printf("AT\r\n");
    request = mbim_message_fibocom_at_command_set_new(strlen(cmd_crlf),
                                                      (const guint8 *)cmd_crlf,
                                                      error);
    if (request == NULL)
        return FALSE;

    response = fu_mm_mbim_device_transaction_sync(device_file,
                                                  request,
                                                  10 * 1000,
                                                  error);
    if (response == NULL) {
        g_prefix_error(error, "AT channel pre-check transaction failed: ");
        return FALSE;
    }

    if (!mbim_message_fibocom_at_command_response_parse(response,
                                                        &resp_size,
                                                        &resp_data,
                                                        error)) {
        g_prefix_error(error, "AT pre-check response parse failed: ");
        return FALSE;
    }

    /* Check for "\r\nOK\r\n" in the response */
    if (g_strrstr_len((const gchar *)resp_data, resp_size, "\r\nOK\r\n") == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "AT pre-check did not contain '\\r\\nOK\\r\\n'");
        return FALSE;
    }

    /* Manual free cmd_crlf */
    g_free(cmd_crlf);
    cmd_crlf = NULL;

    /* Send detach at command (modem need reboot, ignore response) */
    cmd_crlf = g_strdup_printf("%s\r\n", detach_at);
    request = mbim_message_fibocom_at_command_set_new(strlen(cmd_crlf),
                                                      (const guint8 *)cmd_crlf,
                                                      error);
    if (request == NULL)
        return FALSE;

    fu_mm_mbim_device_transaction_sync(device_file,
                                       request,
                                       10 * 1000,
                                       NULL);
    return TRUE;
}

static gboolean
fu_mm_fastboot_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMmFastbootDevice *self = FU_MM_FASTBOOT_DEVICE(device);
	gboolean has_response = TRUE;
	guint16 vid = fu_device_get_vid(device);

	/* expect response for fastboot AT command */
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_MM_FASTBOOT_DEVICE_FLAG_DETACH_AT_NO_RESPONSE)) {
		has_response = FALSE;
	}

	if (vid != ROLLING_WIRELESS_VENDOR_ID) {
                if (!fu_mm_device_at_cmd(FU_MM_DEVICE(self), "AT", TRUE, error))
                        return FALSE;
                if (!fu_mm_device_at_cmd(FU_MM_DEVICE(self), self->detach_at, has_response, error)) {
                        g_prefix_error_literal(error, "rebooting into fastboot not supported: ");
                        return FALSE;
                }
        } else {
	        g_autofree gchar *device_file = NULL;
                if (!fu_mm_device_get_device_file(FU_MM_DEVICE(self), MM_MODEM_PORT_TYPE_MBIM, &device_file, error)) {
                    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            "no mbim port type found for modem: ");
                    return FALSE;
                }

                if (!fu_mm_rolling_deatch_at_cmd_transaction_sync(device_file, self->detach_at, error)) {
                    g_prefix_error_literal(error, "failed to execute mbimcli command: ");
                    return FALSE;
                }
        }

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_mm_fastboot_device_probe(FuDevice *device, GError **error)
{
	FuMmFastbootDevice *self = FU_MM_FASTBOOT_DEVICE(device);
	guint16 vid = fu_device_get_vid(device);
	if (vid != ROLLING_WIRELESS_VENDOR_ID) {
		return fu_mm_device_set_device_file(FU_MM_DEVICE(self), MM_MODEM_PORT_TYPE_AT, error);
	}
	return TRUE;
}


static gboolean
fu_mm_fastboot_device_from_json(FuDevice *device, FwupdJsonObject *json_obj, GError **error)
{
	FuMmFastbootDevice *self = FU_MM_FASTBOOT_DEVICE(device);
	const gchar *tmp;

	/* FuMmDevice->from_json */
	if (!FU_DEVICE_CLASS(fu_mm_fastboot_device_parent_class)
		 ->from_json(device, json_obj, error))
		return FALSE;

	/* optional properties */
	tmp = fwupd_json_object_get_string(json_obj, "DetachAt", NULL);
	if (tmp != NULL)
		fu_mm_fastboot_device_set_detach_at(self, tmp);

	/* success */
	return TRUE;
}

static void
fu_mm_fastboot_device_add_json(FuDevice *device, FwupdJsonObject *json_obj, FwupdCodecFlags flags)
{
	FuMmFastbootDevice *self = FU_MM_FASTBOOT_DEVICE(device);

	/* FuMmDevice->add_json */
	FU_DEVICE_CLASS(fu_mm_fastboot_device_parent_class)->add_json(device, json_obj, flags);

	/* optional properties */
	if (self->detach_at != NULL)
		fwupd_json_object_add_string(json_obj, "DetachAt", self->detach_at);
}

static void
fu_mm_fastboot_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_mm_fastboot_device_init(FuMmFastbootDevice *self)
{
	fu_device_set_remove_delay(FU_DEVICE(self), 20000);
	fu_device_add_protocol(FU_DEVICE(self), "com.google.fastboot");
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_instance_id_full(FU_DEVICE(self),
				       "USB\\VID_18D1&PID_D00D",
				       FU_DEVICE_INSTANCE_FLAG_COUNTERPART);
	fu_device_add_instance_id_full(FU_DEVICE(self),
				       "USB\\VID_2CB7&PID_D00D",
				       FU_DEVICE_INSTANCE_FLAG_COUNTERPART);
}

static void
fu_mm_fastboot_device_finalize(GObject *object)
{
	FuMmFastbootDevice *self = FU_MM_FASTBOOT_DEVICE(object);
	g_free(self->detach_at);
	G_OBJECT_CLASS(fu_mm_fastboot_device_parent_class)->finalize(object);
}

static void
fu_mm_fastboot_device_class_init(FuMmFastbootDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_mm_fastboot_device_finalize;
	device_class->set_progress = fu_mm_fastboot_device_set_progress;
	device_class->detach = fu_mm_fastboot_device_detach;
	device_class->probe = fu_mm_fastboot_device_probe;
	device_class->to_string = fu_mm_fastboot_device_to_string;
	device_class->from_json = fu_mm_fastboot_device_from_json;
	device_class->add_json = fu_mm_fastboot_device_add_json;
	fu_device_register_private_flag(device_class,
					FU_MM_FASTBOOT_DEVICE_FLAG_DETACH_AT_NO_RESPONSE);
}
