/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-intel-mchi-device.h"

struct _FuIntelMchiDevice {
	FuHeciDevice parent_instance;
};

G_DEFINE_TYPE(FuIntelMchiDevice, fu_intel_mchi_device, FU_TYPE_HECI_DEVICE)

#define FU_INTEL_MCHI_DEVICE_FLAG_LEAKED_KM "leaked-km"

static gboolean
fu_intel_mchi_device_add_checksum_for_id(FuIntelMchiDevice *self,
					 guint32 file_id,
					 guint32 section,
					 GError **error)
{
	g_autofree gchar *checksum = NULL;
	g_autoptr(GByteArray) buf = NULL;

	/*
	 * Call READ_FILE_EX with a larger-than-required data size -- which hopefully works when
	 * SHA512 results start being returned too.
	 *
	 * Icelake/Jasperlake/Cometlake: 0x20 (SHA256)
	 * Elkhartlake/Tigerlake/Alderlake/Raptorlake: 0x30 (SHA384)
	 */
	buf = fu_heci_device_read_file_ex(FU_HECI_DEVICE(self), file_id, section, 0x40, error);
	if (buf == NULL)
		return FALSE;

	/* convert into checksum, but only if non-zero and set */
	checksum = fu_byte_array_to_string(buf);
	if (g_str_has_prefix(checksum, "0000000000000000") ||
	    g_str_has_prefix(checksum, "ffffffffffffffff")) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "checksum %s was invalid",
			    checksum);
		return FALSE;
	}
	fu_device_add_checksum(FU_DEVICE(self), checksum);

	/* success */
	return TRUE;
}

static gboolean
fu_intel_mchi_device_setup(FuDevice *device, GError **error)
{
	FuIntelMchiDevice *self = FU_INTEL_MCHI_DEVICE(device);
	const guint32 file_ids[] = {
	    0x40002300, /* CometLake: OEM Public Key Hash */
	    0x40005B00, /* TigerLake: 1st OEM Public Key Hash */
	    0x40005C00, /* TigerLake: 2nd OEM Public Key Hash */
	};

	/* connect */
	if (!fu_mei_device_connect(FU_MEI_DEVICE(device), FU_HECI_DEVICE_UUID_MCHI, 0, error)) {
		g_prefix_error(error, "failed to connect: ");
		return FALSE;
	}

	/* look for all the possible OEM Public Key hashes using the CML+ method */
	for (guint i = 0; i < G_N_ELEMENTS(file_ids); i++) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_intel_mchi_device_add_checksum_for_id(self,
							      file_ids[i],
							      0x0,
							      &error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED) ||
			    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA)) {
				g_debug("ignoring: %s", error_local->message);
				continue;
			}
			g_warning("failed to get public key using file-id 0x%x: %s",
				  file_ids[i],
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
fu_intel_mchi_device_add_security_attrs(FuDevice *device, FuSecurityAttrs *attrs)
{
	FuIntelMchiDevice *self = FU_INTEL_MCHI_DEVICE(device);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr =
	    fu_device_security_attr_new(FU_DEVICE(self), FWUPD_SECURITY_ATTR_ID_MEI_KEY_MANIFEST);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

	/* verify keys */
	if (fu_device_get_checksums(device)->len == 0) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		return;
	}
	if (fu_device_has_private_flag(device, FU_INTEL_MCHI_DEVICE_FLAG_LEAKED_KM)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_intel_mchi_device_version_notify_cb(FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	if (fu_device_has_private_flag(device, FU_INTEL_MCHI_DEVICE_FLAG_LEAKED_KM))
		fu_device_inhibit(device, "leaked-km", "Provisioned with a leaked private key");
}

static void
fu_intel_mchi_device_init(FuIntelMchiDevice *self)
{
	fu_device_set_logical_id(FU_DEVICE(self), "MCHI");
	fu_device_set_name(FU_DEVICE(self), "BootGuard Configuration");
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE_CHILD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_ONLY_CHECKSUM);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon(FU_DEVICE(self), "computer");
	fu_device_register_private_flag(FU_DEVICE(self), FU_INTEL_MCHI_DEVICE_FLAG_LEAKED_KM);
	g_signal_connect(FWUPD_DEVICE(self),
			 "notify::private-flags",
			 G_CALLBACK(fu_intel_mchi_device_version_notify_cb),
			 NULL);
}

static void
fu_intel_mchi_device_class_init(FuIntelMchiDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_intel_mchi_device_setup;
	device_class->add_security_attrs = fu_intel_mchi_device_add_security_attrs;
}
