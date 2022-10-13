/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-intel-me-common.h"
#include "fu-intel-me-mca-device.h"

struct _FuIntelMeMcaDevice {
	FuIntelMeHeciDevice parent_instance;
	gboolean using_leaked_km;
};

G_DEFINE_TYPE(FuIntelMeMcaDevice, fu_intel_me_mca_device, FU_TYPE_INTEL_ME_HECI_DEVICE)

#define MCA_SECTION_ME	0x00 /* OEM Public Key Hash ME FW */
#define MCA_SECTION_UEP 0x04 /* OEM Public Key Hash UEP */
#define MCA_SECTION_FPF 0x08 /* OEM Public Key Hash FPF */

static const gchar *
fu_intel_me_mca_device_section_to_string(guint8 section)
{
	if (section == MCA_SECTION_ME)
		return "ME";
	if (section == MCA_SECTION_UEP)
		return "UEP";
	if (section == MCA_SECTION_FPF)
		return "FPF";
	return NULL;
}

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
	const guint32 sections[] = {MCA_SECTION_FPF, MCA_SECTION_UEP, MCA_SECTION_ME, G_MAXUINT32};
	const guint32 file_ids[] = {0x40002300, /* CometLake: OEM Public Key Hash */
				    0x40005B00, /* TigerLake: 1st OEM Public Key Hash */
				    0x40005C00 /* TigerLake: 2nd OEM Public Key Hash */,
				    G_MAXUINT32};
	const gchar *leaked_kms[] = {"05a92e16da51d10882bfa7e3ba449184ce48e94fa9903e07983d2112ab"
				     "54ecf20fbb07512cea2c13b167c0e252c6a704",
				     "2e357bca116cf3da637bb5803be3550873eddb5a4431a49df1770aca83"
				     "5d94853b458239d207653dce277910d9e5aa0b",
				     "b52a825cf0be60027f12a226226b055ed68efaa9273695d45d859c0ed3"
				     "3d063143974f4b4c59fabfc5afeadab0b00f09",
				     NULL};

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
					  fu_intel_me_mca_device_section_to_string(sections[j]),
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

	/* check for any of the leaked keys */
	for (guint i = 0; leaked_kms[i] != NULL; i++) {
		if (fu_device_has_checksum(self, leaked_kms[i])) {
			self->using_leaked_km = TRUE;
			break;
		}
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
	if (self->using_leaked_km) {
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
}

static void
fu_intel_me_mca_device_class_init(FuIntelMeMcaDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_intel_me_mca_device_setup;
	klass_device->add_security_attrs = fu_intel_me_mca_device_add_security_attrs;
}
