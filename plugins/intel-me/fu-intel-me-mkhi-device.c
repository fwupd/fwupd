/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-intel-me-common.h"
#include "fu-intel-me-mkhi-device.h"

struct _FuIntelMeMkhiDevice {
	FuIntelMeHeciDevice parent_instance;
};

G_DEFINE_TYPE(FuIntelMeMkhiDevice, fu_intel_me_mkhi_device, FU_TYPE_INTEL_ME_HECI_DEVICE)

static gboolean
fu_intel_me_mkhi_device_add_checksum_for_filename(FuIntelMeMkhiDevice *self,
						  const gchar *filename,
						  GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GString) checksum = NULL;

	/* read from the MFS */
	buf = fu_intel_me_heci_device_read_file(FU_INTEL_ME_HECI_DEVICE(self), filename, error);
	if (buf == NULL)
		return FALSE;

	/* convert into checksum, but only if non-zero and set */
	checksum = fu_intel_me_convert_checksum(buf, error);
	if (checksum == NULL)
		return FALSE;
	fu_device_add_checksum(FU_DEVICE(self), checksum->str);

	/* success */
	return TRUE;
}

static gboolean
fu_intel_me_mkhi_device_setup(FuDevice *device, GError **error)
{
	FuIntelMeMkhiDevice *self = FU_INTEL_ME_MKHI_DEVICE(device);
	const gchar *fns[] = {"/fpf/OemCred", NULL};

	/* this is the legacy way to get the hash, which is removed in newer ME versions due to
	 * possible path traversal attacks */
	for (guint i = 0; fns[i] != NULL; i++) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_intel_me_mkhi_device_add_checksum_for_filename(self,
								       fns[i],
								       &error_local)) {
			if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)) {
				continue;
			}
			g_warning("failed to get public key using %s: %s",
				  fns[i],
				  error_local->message);
		}
	}

	/* no point even adding */
	if (fu_device_get_checksums(device)->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no OEM public keys found");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_intel_me_mkhi_device_init(FuIntelMeMkhiDevice *self)
{
	fu_device_set_logical_id(FU_DEVICE(self), "MKHI");
	fu_device_set_name(FU_DEVICE(self), "BootGuard Configuration");
	fu_device_add_parent_guid(FU_DEVICE(self), "main-system-firmware");
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_ONLY_CHECKSUM);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_SET_FLAGS);
}

static void
fu_intel_me_mkhi_device_class_init(FuIntelMeMkhiDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_intel_me_mkhi_device_setup;
}
