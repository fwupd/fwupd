/*
 * Copyright 2022 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-pci-psp-device.h"

#define FU_CPU_AGESA_SMBIOS_TYPE   40
#define FU_CPU_AGESA_SMBIOS_LENGTH 0x0E
#define FU_CPU_AGESA_SMBIOS_OFFSET 4

struct _FuPciPspDevice {
	FuUdevDevice parent_instance;
	gboolean supported;
};

G_DEFINE_TYPE(FuPciPspDevice, fu_pci_psp_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_pci_psp_device_ensure_agesa_version(FuPciPspDevice *self, GError **error)
{
	const gchar *agesa_stream;
	g_auto(GStrv) split = NULL;
	g_autofree gchar *summary = NULL;

	/* get the AGESA stream e.g. `AGESA!V9 StrixKrackanPI-FP8 1.1.0.0a` */
	agesa_stream = fu_device_get_smbios_string(FU_DEVICE(self),
						   FU_CPU_AGESA_SMBIOS_TYPE,
						   FU_CPU_AGESA_SMBIOS_LENGTH,
						   FU_CPU_AGESA_SMBIOS_OFFSET,
						   error);
	if (agesa_stream == NULL) {
		g_prefix_error(error, "no SMBIOS data: ");
		return FALSE;
	}
	split = g_strsplit(agesa_stream, " ", 3);
	if (g_strv_length(split) != 3) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid format: %s",
			    agesa_stream);
		return FALSE;
	}
	summary = g_strdup_printf("AGESA %s %s", split[1], split[2]);

	fu_device_set_summary(FU_DEVICE(self), summary);
	return TRUE;
}

static gboolean
fu_pci_psp_device_probe(FuDevice *device, GError **error)
{
	FuPciPspDevice *self = FU_PCI_PSP_DEVICE(device);
	g_autofree gchar *attr_bootloader_version = NULL;
	g_autofree gchar *attr_tee_version = NULL;
	g_autoptr(GError) error_agesa = NULL;
	g_autoptr(GError) error_boot = NULL;
	g_autoptr(GError) error_tee = NULL;

	attr_bootloader_version =
	    fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
				      "bootloader_version",
				      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
				      &error_boot);
	if (attr_bootloader_version == NULL)
		g_info("failed to read bootloader version: %s", error_boot->message);
	else
		fu_device_set_version_bootloader(device, attr_bootloader_version);

	attr_tee_version = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
						     "tee_version",
						     FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						     &error_tee);
	if (attr_tee_version == NULL)
		g_info("failed to read bootloader version: %s", error_tee->message);
	else
		fu_device_set_version(device, attr_tee_version);

	if (!fu_pci_psp_device_ensure_agesa_version(self, &error_agesa))
		g_info("failed to read AGESA stream: %s", error_agesa->message);

	return TRUE;
}

static gboolean
fu_pci_psp_device_get_attr(FwupdSecurityAttr *attr,
			   const gchar *path,
			   const gchar *file,
			   gboolean *out,
			   GError **error)
{
	guint64 val = 0;
	g_autofree gchar *fn = g_build_filename(path, file, NULL);
	g_autofree gchar *buf = NULL;
	gsize bufsz = 0;

	if (!g_file_get_contents(fn, &buf, &bufsz, error)) {
		g_prefix_error(error, "could not open %s: ", fn);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		return FALSE;
	}
	if (!fu_strtoull(buf, &val, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
		return FALSE;
	*out = val ? TRUE : FALSE;
	return TRUE;
}

static void
fu_pci_psp_device_set_valid_data(FuDevice *device, FuSecurityAttrs *attrs)
{
	FuPciPspDevice *self = FU_PCI_PSP_DEVICE(device);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	if (self->supported)
		return;

	/* CPU supported */
	self->supported = TRUE;
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU,
						     NULL);
	if (attr != NULL)
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static FwupdSecurityAttr *
fu_pci_psp_device_get_security_attr(FuDevice *device,
				    FuSecurityAttrs *attrs,
				    const gchar *appstream_id)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	attr = fu_security_attrs_get_by_appstream_id(attrs, appstream_id, NULL);
	if (attr == NULL) {
		attr = fu_device_security_attr_new(device, appstream_id);
		fu_security_attrs_append(attrs, attr);
	} else if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA)) {
		g_debug("found missing data on old attribute, repopulating");
		fwupd_security_attr_remove_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
	}
	return g_steal_pointer(&attr);
}

static void
fu_pci_psp_device_add_security_attrs_tsme(FuDevice *device,
					  const gchar *path,
					  FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean val;

	attr = fu_pci_psp_device_get_security_attr(device,
						   attrs,
						   FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM);
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		g_debug("ignoring already populated attribute");
		return;
	}
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED);

	if (!fu_pci_psp_device_get_attr(attr, path, "tsme_status", &val, &error_local)) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		g_debug("%s", error_local->message);
		return;
	}

	fu_pci_psp_device_set_valid_data(device, attrs);

	/* BIOS knob used on Lenovo systems */
	fu_security_attr_add_bios_target_value(attr, "com.thinklmi.TSME", "enable");

	if (!val) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENCRYPTED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		return;
	}

	fwupd_security_attr_add_obsolete(attr, "msr");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_psp_device_add_security_attrs_fused_part(FuDevice *device,
						const gchar *path,
						FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean val;

	attr = fu_pci_psp_device_get_security_attr(device,
						   attrs,
						   FWUPD_SECURITY_ATTR_ID_PLATFORM_FUSED);
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		g_debug("ignoring already populated attribute");
		return;
	}
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);

	if (!fu_pci_psp_device_get_attr(attr, path, "fused_part", &val, &error_local)) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		g_debug("%s", error_local->message);
		return;
	}

	fu_pci_psp_device_set_valid_data(device, attrs);

	if (!val) {
		g_debug("part is not fused");
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_psp_device_add_security_attrs_debug_locked_part(FuDevice *device,
						       const gchar *path,
						       FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean val;

	attr = fu_pci_psp_device_get_security_attr(device,
						   attrs,
						   FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED);
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		g_debug("ignoring already populated attribute");
		return;
	}
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);

	if (!fu_pci_psp_device_get_attr(attr, path, "debug_lock_on", &val, &error_local)) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		g_debug("%s", error_local->message);
		return;
	}

	fu_pci_psp_device_set_valid_data(device, attrs);

	if (!val) {
		g_debug("debug lock disabled");
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_psp_device_add_security_attrs_rollback_protection(FuDevice *device,
							 const gchar *path,
							 FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean val;

	attr = fu_pci_psp_device_get_security_attr(device,
						   attrs,
						   FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION);
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		g_debug("ignoring already populated attribute");
		return;
	}
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);

	if (!fu_pci_psp_device_get_attr(attr, path, "anti_rollback_status", &val, &error_local)) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		g_debug("%s", error_local->message);
		return;
	}

	fu_pci_psp_device_set_valid_data(device, attrs);

	if (!val) {
		g_debug("rollback protection not enforced");
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		return;
	}

	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_psp_device_add_security_attrs_rom_armor(FuDevice *device,
					       const gchar *path,
					       FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean val;

	/* create attr */
	attr = fu_pci_psp_device_get_security_attr(device,
						   attrs,
						   FWUPD_SECURITY_ATTR_ID_AMD_SPI_WRITE_PROTECTION);
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		g_debug("ignoring already populated attribute");
		return;
	}
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);

	if (!fu_pci_psp_device_get_attr(attr, path, "rom_armor_enforced", &val, &error_local)) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		g_debug("%s", error_local->message);
		return;
	}

	fu_pci_psp_device_set_valid_data(device, attrs);

	if (!val) {
		g_debug("ROM armor not enforced");
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_psp_device_add_security_attrs_rpmc(FuDevice *device,
					  const gchar *path,
					  FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean val;

	/* create attr */
	attr =
	    fu_pci_psp_device_get_security_attr(device,
						attrs,
						FWUPD_SECURITY_ATTR_ID_AMD_SPI_REPLAY_PROTECTION);
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		g_debug("ignoring already populated attribute");
		return;
	}

	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);

	if (!fu_pci_psp_device_get_attr(attr, path, "rpmc_spirom_available", &val, &error_local)) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		g_debug("%s", error_local->message);
		return;
	}

	fu_pci_psp_device_set_valid_data(device, attrs);

	if (!val) {
		g_debug("no RPMC compatible SPI rom present");
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	if (!fu_pci_psp_device_get_attr(attr,
					path,
					"rpmc_production_enabled",
					&val,
					&error_local)) {
		g_debug("%s", error_local->message);
		return;
	}

	if (!val) {
		g_debug("no RPMC compatible SPI rom present");
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_psp_device_add_security_attrs(FuDevice *device, FuSecurityAttrs *attrs)
{
	FuPciPspDevice *self = FU_PCI_PSP_DEVICE(device);
	const gchar *sysfs_path = NULL;

	if (device != NULL)
		sysfs_path = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	/* ccp not loaded */
	if (sysfs_path == NULL)
		return;

	self->supported = FALSE;

	fu_pci_psp_device_add_security_attrs_tsme(device, sysfs_path, attrs);
	fu_pci_psp_device_add_security_attrs_fused_part(device, sysfs_path, attrs);
	fu_pci_psp_device_add_security_attrs_debug_locked_part(device, sysfs_path, attrs);
	fu_pci_psp_device_add_security_attrs_rollback_protection(device, sysfs_path, attrs);
	fu_pci_psp_device_add_security_attrs_rpmc(device, sysfs_path, attrs);
	fu_pci_psp_device_add_security_attrs_rom_armor(device, sysfs_path, attrs);
}

static void
fu_pci_psp_device_init(FuPciPspDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "Secure Processor");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_COMPUTER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_HOST_CPU_CHILD);
	fu_device_set_vendor(FU_DEVICE(self), "Advanced Micro Devices, Inc.");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_physical_id(FU_DEVICE(self), "pci-psp");
}

static void
fu_pci_psp_device_class_init(FuPciPspDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_pci_psp_device_probe;
	device_class->add_security_attrs = fu_pci_psp_device_add_security_attrs;
}
