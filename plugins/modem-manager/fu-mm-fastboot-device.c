/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mm-fastboot-device.h"

struct _FuMmFastbootDevice {
	FuMmDevice parent_instance;
	gchar *detach_at;
};

G_DEFINE_TYPE(FuMmFastbootDevice, fu_mm_fastboot_device, FU_TYPE_MM_DEVICE)

#define FU_MM_FASTBOOT_DEVICE_FLAG_DETACH_AT_NO_RESPONSE "detach-at-fastboot-has-no-response"

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
fu_mm_fastboot_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMmFastbootDevice *self = FU_MM_FASTBOOT_DEVICE(device);
	gboolean has_response = TRUE;

	/* expect response for fastboot AT command */
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_MM_FASTBOOT_DEVICE_FLAG_DETACH_AT_NO_RESPONSE)) {
		has_response = FALSE;
	}
	if (!fu_mm_device_at_cmd(FU_MM_DEVICE(self), "AT", TRUE, error))
		return FALSE;
	if (!fu_mm_device_at_cmd(FU_MM_DEVICE(self), self->detach_at, has_response, error)) {
		g_prefix_error(error, "rebooting into fastboot not supported: ");
		return FALSE;
	}

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_mm_fastboot_device_probe(FuDevice *device, GError **error)
{
	FuMmFastbootDevice *self = FU_MM_FASTBOOT_DEVICE(device);
	return fu_mm_device_set_device_file(FU_MM_DEVICE(self), MM_MODEM_PORT_TYPE_AT, error);
}

static gboolean
fu_mm_fastboot_device_from_json(FuDevice *device, JsonObject *json_object, GError **error)
{
	FuMmFastbootDevice *self = FU_MM_FASTBOOT_DEVICE(device);
	const gchar *tmp;

	/* FuMmDevice->from_json */
	if (!FU_DEVICE_CLASS(fu_mm_fastboot_device_parent_class)
		 ->from_json(device, json_object, error))
		return FALSE;

	/* optional properties */
	tmp = json_object_get_string_member_with_default(json_object, "DetachAt", NULL);
	if (tmp != NULL)
		fu_mm_fastboot_device_set_detach_at(self, tmp);

	/* success */
	return TRUE;
}

static void
fu_mm_fastboot_device_add_json(FuDevice *device, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuMmFastbootDevice *self = FU_MM_FASTBOOT_DEVICE(device);

	/* FuMmDevice->add_json */
	FU_DEVICE_CLASS(fu_mm_fastboot_device_parent_class)->add_json(device, builder, flags);

	/* optional properties */
	if (self->detach_at != NULL)
		fwupd_codec_json_append(builder, "DetachAt", self->detach_at);
}

static void
fu_mm_fastboot_device_set_progress(FuDevice *self, FuProgress *progress)
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
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_MM_FASTBOOT_DEVICE_FLAG_DETACH_AT_NO_RESPONSE);
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
}
