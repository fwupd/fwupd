/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-tpm-v1-device.h"

struct _FuTpmV1Device {
	FuTpmDevice parent_instance;
};

G_DEFINE_TYPE(FuTpmV1Device, fu_tpm_v1_device, FU_TYPE_TPM_DEVICE)

static gboolean
_g_string_isxdigit(GString *str)
{
	for (gsize i = 0; i < str->len; i++) {
		if (!g_ascii_isxdigit(str->str[i]))
			return FALSE;
	}
	return TRUE;
}

static void
fu_tpm_device_parse_line(const gchar *line, gpointer user_data)
{
	FuTpmDevice *self = FU_TPM_DEVICE(user_data);
	guint64 idx;
	g_autofree gchar *idxstr = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GString) str = NULL;
	g_autoptr(GError) error_local = NULL;

	/* split into index:hash */
	if (line == NULL || line[0] == '\0')
		return;
	split = g_strsplit(line, ":", -1);
	if (g_strv_length(split) != 2) {
		g_debug("unexpected format, skipping: %s", line);
		return;
	}

	/* get index */
	idxstr = fu_strstrip(split[0]);
	if (!fu_strtoull(idxstr, &idx, 0, 64, &error_local)) {
		g_debug("unexpected index %s, skipping: %s", idxstr, error_local->message);
		return;
	}

	/* parse hash */
	str = g_string_new(split[1]);
	fu_string_replace(str, " ", "");
	if ((str->len != 40 && str->len != 64) || !_g_string_isxdigit(str)) {
		g_debug("not SHA-1 or SHA-256, skipping: %s", split[1]);
		return;
	}
	g_string_ascii_down(str);
	fu_tpm_device_add_checksum(self, idx, str->str);
}

static gboolean
fu_tpm_v1_device_probe(FuDevice *device, GError **error)
{
	FuTpmV1Device *self = FU_TPM_V1_DEVICE(device);
	g_auto(GStrv) lines = NULL;
	g_autofree gchar *buf_pcrs = NULL;

	/* get entire contents */
	if (!g_file_get_contents(fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)),
				 &buf_pcrs,
				 NULL,
				 error))
		return FALSE;

	/* find PCR lines */
	lines = g_strsplit(buf_pcrs, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		if (g_str_has_prefix(lines[i], "PCR-"))
			fu_tpm_device_parse_line(lines[i] + 4, self);
	}
	return TRUE;
}

static void
fu_tpm_v1_device_init(FuTpmV1Device *self)
{
}

static void
fu_tpm_v1_device_class_init(FuTpmV1DeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->probe = fu_tpm_v1_device_probe;
}

FuTpmDevice *
fu_tpm_v1_device_new(FuContext *ctx)
{
	return FU_TPM_DEVICE(g_object_new(FU_TYPE_TPM_V1_DEVICE, "context", ctx, NULL));
}
