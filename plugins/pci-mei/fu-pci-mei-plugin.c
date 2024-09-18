/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mei-common.h"
#include "fu-mei-struct.h"
#include "fu-pci-mei-plugin.h"

struct _FuPciMeiPlugin {
	FuPlugin parent_instance;
	FuDevice *pci_device;
	guint8 hfsts_buf[7][4]; /* 1-6, 0 unused */
	FuMeiFamily family;
	FuMeiVersion vers;
	FuMeiIssue issue;
};

G_DEFINE_TYPE(FuPciMeiPlugin, fu_pci_mei_plugin, FU_TYPE_PLUGIN)

static FuMeiFamily
fu_pci_mei_plugin_detect_family(FuPlugin *plugin)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	guint8 ver = self->vers.major;

	if (ver == 0)
		return FU_MEI_FAMILY_UNKNOWN;
	if (ver == 1 || ver == 2) {
		g_autoptr(FuMeiCsme11Hfsts1) hfsts1 = NULL;
		hfsts1 = fu_mei_csme11_hfsts1_parse(self->hfsts_buf[1],
						    sizeof(self->hfsts_buf[1]),
						    0x0,
						    NULL);
		if (hfsts1 == NULL)
			return FU_MEI_FAMILY_UNKNOWN;
		if (fu_mei_csme11_hfsts1_get_operation_mode(hfsts1) == 0xf)
			return FU_MEI_FAMILY_SPS;
		return FU_MEI_FAMILY_TXE;
	}
	if (ver == 3 || ver == 4 || ver == 5)
		return FU_MEI_FAMILY_TXE;
	if (ver == 6 || ver == 7 || ver == 8 || ver == 9 || ver == 10)
		return FU_MEI_FAMILY_ME;
	if (ver >= 11 && ver <= 17)
		return FU_MEI_FAMILY_CSME;
	return FU_MEI_FAMILY_CSME18;
}

static gboolean
fu_pci_mei_plugin_parse_fwvers(FuPlugin *plugin, const gchar *fwvers, GError **error)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	guint64 tmp64 = 0;
	g_auto(GStrv) lines = NULL;
	g_auto(GStrv) sections = NULL;
	g_auto(GStrv) split = NULL;

	/* we only care about the first version */
	lines = g_strsplit(fwvers, "\n", -1);
	if (g_strv_length(lines) < 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "expected data, got %s",
			    fwvers);
		return FALSE;
	}

	/* split platform : version */
	sections = g_strsplit(lines[0], ":", -1);
	if (g_strv_length(sections) != 2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "expected platform:major.minor.micro.build, got %s",
			    lines[0]);
		return FALSE;
	}

	/* parse platform and versions */
	if (!fu_strtoull(sections[0], &tmp64, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error)) {
		g_prefix_error(error, "failed to process platform version %s: ", sections[0]);
		return FALSE;
	}
	self->vers.platform = tmp64;
	split = g_strsplit(sections[1], ".", -1);
	if (g_strv_length(split) != 4) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "expected major.minor.micro.build, got %s",
			    sections[1]);
		return FALSE;
	}

	if (!fu_strtoull(split[0], &tmp64, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error)) {
		g_prefix_error(error, "failed to process major version %s: ", split[0]);
		return FALSE;
	}
	self->vers.major = tmp64;
	if (!fu_strtoull(split[1], &tmp64, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error)) {
		g_prefix_error(error, "failed to process minor version %s: ", split[1]);
		return FALSE;
	}
	self->vers.minor = tmp64;
	if (!fu_strtoull(split[2], &tmp64, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error)) {
		g_prefix_error(error, "failed to process hotfix version %s: ", split[2]);
		return FALSE;
	}
	self->vers.hotfix = tmp64;
	if (!fu_strtoull(split[3], &tmp64, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error)) {
		g_prefix_error(error, "failed to process buildno version %s: ", split[3]);
		return FALSE;
	}
	self->vers.buildno = tmp64;

	/* check the AMT version for issues using the data from:
	 * https://downloadcenter.intel.com/download/28632 */
	self->family = fu_pci_mei_plugin_detect_family(plugin);
	if (self->family == FU_MEI_FAMILY_CSME || self->family == FU_MEI_FAMILY_CSME18)
		self->issue = fu_mei_common_is_csme_vulnerable(&self->vers);
	else if (self->family == FU_MEI_FAMILY_TXE)
		self->issue = fu_mei_common_is_txe_vulnerable(&self->vers);
	else if (self->family == FU_MEI_FAMILY_SPS)
		self->issue = fu_mei_common_is_sps_vulnerable(&self->vers);
	g_debug("%s version parsed as %u.%u.%u",
		fu_mei_family_to_string(self->family),
		self->vers.major,
		self->vers.minor,
		self->vers.hotfix);
	return TRUE;
}

static gboolean
fu_pci_mei_plugin_backend_device_added(FuPlugin *plugin,
				       FuDevice *device,
				       FuProgress *progress,
				       GError **error)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	const guint hfs_cfg_addrs[] = {0x0, 0x40, 0x48, 0x60, 0x64, 0x68, 0x6c};
	g_autofree gchar *device_file = NULL;
	g_autofree gchar *fwvers = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* interesting device? */
	if (!FU_IS_PCI_DEVICE(device))
		return TRUE;

	/* open the config */
	device_file =
	    g_build_filename(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)), "config", NULL);
	fu_udev_device_set_device_file(FU_UDEV_DEVICE(device), device_file);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(device), FU_IO_CHANNEL_OPEN_FLAG_READ);
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	/* grab MEI config registers */
	g_set_object(&self->pci_device, device);
	for (guint i = 1; i < G_N_ELEMENTS(hfs_cfg_addrs); i++) {
		if (!fu_udev_device_pread(FU_UDEV_DEVICE(self->pci_device),
					  hfs_cfg_addrs[i],
					  self->hfsts_buf[i],
					  sizeof(self->hfsts_buf[i]),
					  error)) {
			g_prefix_error(error, "could not read HFS%u: ", i);
			return FALSE;
		}
	}

	/* check firmware version */
	fwvers = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
					   "mei/mei0/fw_ver",
					   FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					   NULL);
	if (fwvers != NULL) {
		if (!fu_pci_mei_plugin_parse_fwvers(plugin, fwvers, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_pci_mei_plugin_add_attrs_csme11_manufacturing_mode(FuPlugin *plugin,
						      FuMeiCsme11Hfsts1 *hfsts1,
						      FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* Manufacturing Mode */
	fwupd_security_attr_add_metadata(attr, "kind", fu_mei_family_to_string(self->family));
	if (fu_mei_csme11_hfsts1_get_mfg_mode(hfsts1)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_mei_plugin_add_attrs_csme18_manufacturing_mode(FuPlugin *plugin,
						      FuMeiCsme18Hfsts1 *hfsts1,
						      FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* Manufacturing Mode, BIOS has access to the SPI descriptor */
	if (fu_mei_csme18_hfsts1_get_spi_protection_mode(hfsts1)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* running in debug mode */
	if (fu_mei_csme18_hfsts1_get_operation_mode(hfsts1) == FU_ME_HFS_MODE_DEBUG ||
	    fu_mei_csme18_hfsts1_get_operation_mode(hfsts1) == FU_ME_HFS_MODE_ENHANCED_DEBUG) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_mei_plugin_add_attrs_csme11_override_strap(FuPlugin *plugin,
						  FuMeiCsme11Hfsts1 *hfsts1,
						  FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* Flash Descriptor Security Override Strap */
	fwupd_security_attr_add_metadata(attr, "kind", fu_mei_family_to_string(self->family));
	if (fu_mei_csme11_hfsts1_get_operation_mode(hfsts1) == FU_ME_HFS_MODE_OVERRIDE_JUMPER) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_mei_plugin_add_attrs_csme18_override_strap(FuPlugin *plugin,
						  FuMeiCsme18Hfsts1 *hfsts1,
						  FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* Flash Descriptor Security Override Strap */
	fwupd_security_attr_add_metadata(attr, "kind", fu_mei_family_to_string(self->family));
	if (fu_mei_csme18_hfsts1_get_operation_mode(hfsts1) == FU_ME_HFS_MODE_OVERRIDE_JUMPER) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_mei_plugin_add_attrs_csme11_bootguard_enabled(FuPlugin *plugin,
						     FuMeiCsme11Hfsts6 *hfsts6,
						     FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* disabled at runtime? */
	if (fu_mei_csme11_hfsts6_get_boot_guard_disable(hfsts6)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_mei_plugin_add_attrs_csme18_bootguard_enabled(FuPlugin *plugin,
						     FuMeiCsme18Hfsts5 *hfsts5,
						     FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* disabled at runtime? */
	if (!fu_mei_csme18_hfsts5_get_valid(hfsts5)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_mei_plugin_add_attrs_csme11_bootguard_verified(FuPlugin *plugin,
						      FuMeiCsme11Hfsts6 *hfsts6,
						      FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* actively disabled */
	if (fu_mei_csme11_hfsts6_get_boot_guard_disable(hfsts6)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* measured boot is not sufficient, verified is required */
	if (!fu_mei_csme11_hfsts6_get_verified_boot(hfsts6)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_mei_plugin_add_attrs_csme11_bootguard_acm(FuPlugin *plugin,
						 FuMeiCsme11Hfsts6 *hfsts6,
						 FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* actively disabled */
	if (fu_mei_csme11_hfsts6_get_boot_guard_disable(hfsts6)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* ACM protection required */
	if (!fu_mei_csme11_hfsts6_get_force_boot_guard_acm(hfsts6)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_mei_plugin_add_attrs_csme18_bootguard_acm(FuPlugin *plugin,
						 FuMeiCsme18Hfsts5 *hfsts5,
						 FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* ACM protection required */
	if (!fu_mei_csme18_hfsts5_get_btg_acm_active(hfsts5)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}
	if (!fu_mei_csme18_hfsts5_get_acm_done_sts(hfsts5)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_mei_plugin_add_attrs_csme11_bootguard_policy(FuPlugin *plugin,
						    FuMeiCsme11Hfsts6 *hfsts6,
						    FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* actively disabled */
	if (fu_mei_csme11_hfsts6_get_boot_guard_disable(hfsts6)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* policy must be to immediately shutdown or after 30 mins -- the latter isn't ideal but
	 * we've been testing for this accidentally for a long time now */
	if (fu_mei_csme11_hfsts6_get_error_enforce_policy(hfsts6) !=
		FU_ME_HFS_ENFORCEMENT_POLICY_SHUTDOWN_NOW &&
	    fu_mei_csme11_hfsts6_get_error_enforce_policy(hfsts6) !=
		FU_ME_HFS_ENFORCEMENT_POLICY_SHUTDOWN_30MINS) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_mei_plugin_add_attrs_csme11_bootguard_otp(FuPlugin *plugin,
						 FuMeiCsme11Hfsts6 *hfsts6,
						 FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* actively disabled */
	if (fu_mei_csme11_hfsts6_get_boot_guard_disable(hfsts6)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* ensure vendor set the FPF OTP fuse */
	if (!fu_mei_csme11_hfsts6_get_fpf_soc_lock(hfsts6)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_mei_plugin_add_attrs_csme18_bootguard_otp(FuPlugin *plugin,
						 FuMeiCsme18Hfsts6 *hfsts6,
						 FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* actively disabled */
	if (fu_mei_csme11_hfsts6_get_boot_guard_disable(hfsts6)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* ensure vendor set the FPF configuration fuse */
	if (!fu_mei_csme18_hfsts6_get_fpf_soc_configuration_lock(hfsts6)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}
	if (!fu_mei_csme18_hfsts6_get_manufacturing_lock(hfsts6)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_mei_plugin_add_attrs_mei_version(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autofree gchar *version = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_MEI_VERSION);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

	/* not enabled */
	if (self->pci_device == NULL) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		return;
	}

	/* format version as string */
	version = g_strdup_printf("%u:%u.%u.%u.%u",
				  self->vers.platform,
				  self->vers.major,
				  self->vers.minor,
				  self->vers.hotfix,
				  self->vers.buildno);
	if (self->issue == FU_MEI_ISSUE_UNKNOWN) {
		g_warning("ME family not supported for %s", version);
		return;
	}
	fwupd_security_attr_add_metadata(attr, "version", version);
	fwupd_security_attr_add_metadata(attr, "kind", fu_mei_family_to_string(self->family));

	/* Flash Descriptor Security Override Strap */
	if (self->issue == FU_MEI_ISSUE_VULNERABLE) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_mei_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr_cpu = NULL;

	/* only Intel */
	if (fu_cpu_get_vendor() != FU_CPU_VENDOR_INTEL)
		return;

	/* CPU supported */
	attr_cpu = fu_security_attrs_get_by_appstream_id(attrs,
							 FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU,
							 NULL);
	if (attr_cpu != NULL)
		fwupd_security_attr_add_flag(attr_cpu, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);

	/* run CSME-specific tests depending on version */
	if (self->family == FU_MEI_FAMILY_CSME) {
		g_autoptr(FuMeiCsme11Hfsts1) hfsts1 = NULL;
		g_autoptr(FuMeiCsme11Hfsts6) hfsts6 = NULL;

		/* CSME 11 to 17 */
		hfsts1 = fu_mei_csme11_hfsts1_parse(self->hfsts_buf[1],
						    sizeof(self->hfsts_buf[1]),
						    0x0,
						    NULL);
		if (hfsts1 == NULL)
			return;
		hfsts6 = fu_mei_csme11_hfsts6_parse(self->hfsts_buf[6],
						    sizeof(self->hfsts_buf[6]),
						    0x0,
						    NULL);
		if (hfsts6 == NULL)
			return;
		fu_pci_mei_plugin_add_attrs_csme11_manufacturing_mode(plugin, hfsts1, attrs);
		fu_pci_mei_plugin_add_attrs_csme11_override_strap(plugin, hfsts1, attrs);
		fu_pci_mei_plugin_add_attrs_csme11_bootguard_enabled(plugin, hfsts6, attrs);
		fu_pci_mei_plugin_add_attrs_csme11_bootguard_verified(plugin, hfsts6, attrs);
		fu_pci_mei_plugin_add_attrs_csme11_bootguard_acm(plugin, hfsts6, attrs);
		fu_pci_mei_plugin_add_attrs_csme11_bootguard_policy(plugin, hfsts6, attrs);
		fu_pci_mei_plugin_add_attrs_csme11_bootguard_otp(plugin, hfsts6, attrs);

	} else if (self->family == FU_MEI_FAMILY_CSME18) {
		g_autoptr(FuMeiCsme18Hfsts1) hfsts1 = NULL;
		g_autoptr(FuMeiCsme18Hfsts5) hfsts5 = NULL;
		g_autoptr(FuMeiCsme18Hfsts6) hfsts6 = NULL;

		/* CSME 18+ */
		hfsts1 = fu_mei_csme18_hfsts1_parse(self->hfsts_buf[1],
						    sizeof(self->hfsts_buf[1]),
						    0x0,
						    NULL);
		if (hfsts1 == NULL)
			return;
		hfsts5 = fu_mei_csme18_hfsts5_parse(self->hfsts_buf[5],
						    sizeof(self->hfsts_buf[5]),
						    0x0,
						    NULL);
		if (hfsts5 == NULL)
			return;
		hfsts6 = fu_mei_csme18_hfsts6_parse(self->hfsts_buf[6],
						    sizeof(self->hfsts_buf[6]),
						    0x0,
						    NULL);
		if (hfsts6 == NULL)
			return;
		fu_pci_mei_plugin_add_attrs_csme18_manufacturing_mode(plugin, hfsts1, attrs);
		fu_pci_mei_plugin_add_attrs_csme18_override_strap(plugin, hfsts1, attrs);
		fu_pci_mei_plugin_add_attrs_csme18_bootguard_enabled(plugin, hfsts5, attrs);
		fu_pci_mei_plugin_add_attrs_csme18_bootguard_acm(plugin, hfsts5, attrs);
		fu_pci_mei_plugin_add_attrs_csme18_bootguard_otp(plugin, hfsts6, attrs);
	} else {
		g_autoptr(FwupdSecurityAttr) attr = NULL;

		/* not supported */
		attr = fu_plugin_security_attr_new(plugin,
						   FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		fu_security_attrs_append(attrs, attr);
		return;
	}

	/* all */
	fu_pci_mei_plugin_add_attrs_mei_version(plugin, attrs);
}

static void
fu_pci_mei_plugin_init(FuPciMeiPlugin *self)
{
}

static void
fu_pci_mei_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "pci");
}

static void
fu_pci_mei_plugin_finalize(GObject *obj)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(obj);
	if (self->pci_device != NULL)
		g_object_unref(self->pci_device);
	G_OBJECT_CLASS(fu_pci_mei_plugin_parent_class)->finalize(obj);
}

static void
fu_pci_mei_plugin_class_init(FuPciMeiPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_pci_mei_plugin_finalize;
	plugin_class->constructed = fu_pci_mei_plugin_constructed;
	plugin_class->add_security_attrs = fu_pci_mei_plugin_add_security_attrs;
	plugin_class->backend_device_added = fu_pci_mei_plugin_backend_device_added;
}
