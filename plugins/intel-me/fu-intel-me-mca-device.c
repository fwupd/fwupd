/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-intel-me-common.h"
#include "fu-intel-me-mca-device.h"
#include "fu-intel-me-struct.h"

struct _FuIntelMeMcaDevice {
	FuIntelMeHeciDevice parent_instance;
};

G_DEFINE_TYPE(FuIntelMeMcaDevice, fu_intel_me_mca_device, FU_TYPE_INTEL_ME_HECI_DEVICE)

static gboolean
fu_intel_me_mca_device_add_checksum_for_id(FuIntelMeMcaDevice *self,
					   guint32 file_id,
					   guint32 section,
					   GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GString) checksum = NULL;

	/*
	 * Call READ_FILE_EX with a larger-than-required data size -- which hopefully works when
	 * SHA512 results start being returned too.
	 *
	 * CometLake: 0x20 (SHA256)
	 * TigerLake: 0x30 (SHA384)
	 */
	buf = fu_intel_me_heci_device_read_file_ex(FU_INTEL_ME_HECI_DEVICE(self),
						   file_id,
						   section,
						   0x40,
						   error);
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
fu_intel_me_mca_device_setup(FuDevice *device, GError **error)
{
	FuIntelMeMcaDevice *self = FU_INTEL_ME_MCA_DEVICE(device);
	const guint32 sections[] = {FU_INTEL_ME_MCA_SECTION_FPF,
				    FU_INTEL_ME_MCA_SECTION_UEP,
				    FU_INTEL_ME_MCA_SECTION_ME,
				    G_MAXUINT32};
	const guint32 file_ids[] = {0x40002300, /* CometLake: OEM Public Key Hash */
				    0x40005B00, /* TigerLake: 1st OEM Public Key Hash */
				    0x40005C00 /* TigerLake: 2nd OEM Public Key Hash */,
				    G_MAXUINT32};

	/* look for all the possible OEM Public Key hashes using the CML+ method */
	for (guint i = 0; file_ids[i] != G_MAXUINT32; i++) {
		for (guint j = 0; sections[j] != G_MAXUINT32; j++) {
			g_autoptr(GError) error_local = NULL;
			if (!fu_intel_me_mca_device_add_checksum_for_id(self,
									file_ids[i],
									sections[j],
									&error_local)) {
				if (g_error_matches(error_local,
						    G_IO_ERROR,
						    G_IO_ERROR_NOT_SUPPORTED) ||
				    g_error_matches(error_local,
						    G_IO_ERROR,
						    G_IO_ERROR_NOT_INITIALIZED)) {
					continue;
				}
				g_warning("failed to get public key using file-id 0x%x, "
					  "section %s [0x%x]: %s",
					  file_ids[i],
					  fu_intel_me_mca_section_to_string(sections[j]),
					  sections[j],
					  error_local->message);
			}
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
fu_intel_me_mca_device_add_security_attrs(FuDevice *device, FuSecurityAttrs *attrs)
{
	FuIntelMeMcaDevice *self = FU_INTEL_ME_MCA_DEVICE(device);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr =
	    fu_device_security_attr_new(FU_DEVICE(self), FWUPD_SECURITY_ATTR_ID_MEI_KEY_MANIFEST);
	fu_security_attrs_append(attrs, attr);

	/* verify keys */
	if (fu_device_get_checksums(device)->len == 0) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		return;
	}
	if (fu_device_has_private_flag(device, FU_INTEL_ME_HECI_DEVICE_FLAG_LEAKED_KM)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}

static void
fu_intel_me_mca_device_init(FuIntelMeMcaDevice *self)
{
	fu_device_set_logical_id(FU_DEVICE(self), "MCA");
	fu_device_set_name(FU_DEVICE(self), "BootGuard Configuration");
	fu_device_add_parent_guid(FU_DEVICE(self), "main-system-firmware");
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_ONLY_CHECKSUM);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_SET_FLAGS);
}

static void
fu_intel_me_mca_device_class_init(FuIntelMeMcaDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_intel_me_mca_device_setup;
	klass_device->add_security_attrs = fu_intel_me_mca_device_add_security_attrs;
}
