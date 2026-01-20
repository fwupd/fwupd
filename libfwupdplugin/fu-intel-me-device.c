/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-security-attr-private.h"

#include "fu-intel-me-device.h"
#include "fu-string.h"

struct _FuIntelMeDevice {
	FuDevice parent_instance;
	FuIntelMeFamily family;
	FuIntelMeIssue issue;
	FuMeHfsCws working_state;
	FuStructIntelMeHfsts *hfsts[7]; /* 1-6, 0 unused */
};

G_DEFINE_TYPE(FuIntelMeDevice, fu_intel_me_device, FU_TYPE_DEVICE)

/**
 * fu_intel_me_device_get_family:
 * @self: a #FuIntelMeDevice
 *
 * Gets the ME device family.
 *
 * Returns: a #FuIntelMeFamily, e.g. #FU_INTEL_ME_FAMILY_CSME11
 *
 * Since: 2.1.1
 **/
FuIntelMeFamily
fu_intel_me_device_get_family(FuIntelMeDevice *self)
{
	g_return_val_if_fail(FU_IS_INTEL_ME_DEVICE(self), FU_INTEL_ME_FAMILY_UNKNOWN);
	return self->family;
}

/**
 * fu_intel_me_device_get_issue:
 * @self: a #FuIntelMeDevice
 *
 * Gets the ME device issue.
 *
 * Returns: a #FuIntelMeIssue, e.g. #FU_INTEL_ME_ISSUE_NOT_VULNERABLE
 *
 * Since: 2.1.1
 **/
FuIntelMeIssue
fu_intel_me_device_get_issue(FuIntelMeDevice *self)
{
	g_return_val_if_fail(FU_IS_INTEL_ME_DEVICE(self), FU_INTEL_ME_ISSUE_UNKNOWN);
	return self->issue;
}

static void
fu_intel_me_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuIntelMeDevice *self = FU_INTEL_ME_DEVICE(device);
	fwupd_codec_string_append(str, idt, "Family", fu_intel_me_family_to_string(self->family));
	fwupd_codec_string_append(str, idt, "Issue", fu_intel_me_issue_to_string(self->issue));
	fwupd_codec_string_append(str,
				  idt,
				  "WorkingState",
				  fu_me_hfs_cws_to_string(self->working_state));
	for (guint i = 1; i < G_N_ELEMENTS(self->hfsts); i++) {
		g_autofree gchar *title = g_strdup_printf("Hfsts%x", i);
		if (self->hfsts[i] != NULL)
			fwupd_codec_string_append_hex(
			    str,
			    idt,
			    title,
			    fu_struct_intel_me_hfsts_get_value(self->hfsts[i]));
	}
}

typedef struct {
	guint8 major;
	guint8 minor;
	guint8 patch;
	guint16 buildno;
} FuIntelMeVersions;

static FuIntelMeVersions *
fu_intel_me_device_parse_versions(FuIntelMeDevice *self, GError **error)
{
	const gchar *version = fu_device_get_version(FU_DEVICE(self));
	guint64 tmp64 = 0;
	g_autofree FuIntelMeVersions *vers = g_new0(FuIntelMeVersions, 1);
	g_auto(GStrv) split = NULL;

	/* parse vers */
	split = g_strsplit(version, ".", -1);
	if (g_strv_length(split) != 4) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "expected major.minor.micro.build, got %s",
			    version);
		return NULL;
	}
	if (!fu_strtoull(split[0], &tmp64, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error)) {
		g_prefix_error(error, "failed to process major version %s: ", split[0]);
		return NULL;
	}
	vers->major = tmp64;
	if (!fu_strtoull(split[1], &tmp64, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error)) {
		g_prefix_error(error, "failed to process minor version %s: ", split[1]);
		return NULL;
	}
	vers->minor = tmp64;
	if (!fu_strtoull(split[2], &tmp64, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error)) {
		g_prefix_error(error, "failed to process patch version %s: ", split[2]);
		return NULL;
	}
	vers->patch = tmp64;
	if (!fu_strtoull(split[3], &tmp64, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error)) {
		g_prefix_error(error, "failed to process buildno version %s: ", split[3]);
		return NULL;
	}
	vers->buildno = tmp64;

	/* success */
	return g_steal_pointer(&vers);
}

static void
fu_intel_me_device_version_notify_cb(FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	FuIntelMeDevice *self = FU_INTEL_ME_DEVICE(device);
	g_autofree FuIntelMeVersions *vers = NULL;
	g_autoptr(GError) error = NULL;
	struct {
		FuIntelMeFamily family;
		guint8 major_eq;
		guint8 minor_eq;
		guint8 patch_ge;
	} verdata[] = {
	    {FU_INTEL_ME_FAMILY_TXE, 3, 1, 92},
	    {FU_INTEL_ME_FAMILY_TXE, 4, 0, 45},
	    {FU_INTEL_ME_FAMILY_CSME11, 11, 8, 92},
	    {FU_INTEL_ME_FAMILY_CSME11, 11, 12, 92},
	    {FU_INTEL_ME_FAMILY_CSME11, 11, 22, 92},
	    {FU_INTEL_ME_FAMILY_CSME11, 12, 0, 90},
	    {FU_INTEL_ME_FAMILY_CSME11, 13, 0, 60},
	    {FU_INTEL_ME_FAMILY_CSME11, 13, 30, 30},
	    {FU_INTEL_ME_FAMILY_CSME11, 13, 50, 20},
	    {FU_INTEL_ME_FAMILY_CSME11, 14, 1, 65},
	    {FU_INTEL_ME_FAMILY_CSME11, 14, 5, 45},
	    {FU_INTEL_ME_FAMILY_CSME11, 15, 0, 40},
	    {FU_INTEL_ME_FAMILY_CSME11, 15, 40, 20},
	};

	/* parse into 4 sections */
	if (fu_device_get_version(device) == NULL)
		return;
	vers = fu_intel_me_device_parse_versions(self, &error);
	if (vers == NULL) {
		g_warning("failed to parse ME version: %s", error->message);
		return;
	}

	/* set the family */
	if (vers->major == 0) {
		self->family = FU_INTEL_ME_FAMILY_UNKNOWN;
	} else if (vers->major == 1 || vers->major == 2 || vers->major == 3 || vers->major == 4 ||
		   vers->major == 5) {
		/* not completely true, but good enough for 2025... */
		self->family = FU_INTEL_ME_FAMILY_TXE;
	} else if (vers->major == 6 || vers->major == 7 || vers->major == 8 || vers->major == 9 ||
		   vers->major == 10) {
		self->family = FU_INTEL_ME_FAMILY_ME;
	} else if (vers->major == 11 || vers->major == 12 || vers->major == 13 ||
		   vers->major == 14 || vers->major == 15) {
		self->family = FU_INTEL_ME_FAMILY_CSME11;
	} else if (vers->major == 16 || vers->major == 17) {
		self->family = FU_INTEL_ME_FAMILY_CSME16;
	} else {
		self->family = FU_INTEL_ME_FAMILY_CSME18;
	}

	/* check the AMT version for issues using the data from:
	 * https://downloadcenter.intel.com/download/28632 */
	self->issue = FU_INTEL_ME_ISSUE_NOT_VULNERABLE;
	for (guint i = 0; i < G_N_ELEMENTS(verdata); i++) {
		if (self->family == verdata[i].family && vers->major == verdata[i].major_eq &&
		    vers->minor == verdata[i].minor_eq) {
			self->issue = vers->patch >= verdata[i].patch_ge
					  ? FU_INTEL_ME_ISSUE_PATCHED
					  : FU_INTEL_ME_ISSUE_VULNERABLE;
			break;
		}
	}
}

/**
 * fu_intel_me_device_get_hfsts: (skip):
 * @self: a #FuIntelMeDevice
 * @idx: index, 1-6
 *
 * Gets a HFSTSx register.
 *
 * Returns: (transfer none): a #FuStructIntelMeHfsts, or %NULL if unset
 *
 * Since: 2.1.1
 **/
FuStructIntelMeHfsts *
fu_intel_me_device_get_hfsts(FuIntelMeDevice *self, guint idx)
{
	g_return_val_if_fail(FU_IS_INTEL_ME_DEVICE(self), NULL);
	g_return_val_if_fail(idx != 0, NULL);
	g_return_val_if_fail(idx < G_N_ELEMENTS(self->hfsts), NULL);
	return self->hfsts[idx];
}

/**
 * fu_intel_me_device_set_hfsts: (skip):
 * @self: a #FuIntelMeDevice
 * @idx: index, 1-6
 * @hfsts: a #FuStructIntelMeHfsts
 *
 * Sets a HFSTSx register.
 *
 * Since: 2.1.1
 **/
void
fu_intel_me_device_set_hfsts(FuIntelMeDevice *self, guint idx, FuStructIntelMeHfsts *hfsts)
{
	g_return_if_fail(FU_IS_INTEL_ME_DEVICE(self));
	g_return_if_fail(idx != 0);
	g_return_if_fail(idx < G_N_ELEMENTS(self->hfsts));

	/* not 100% true, but the CWS section is the same for CSME11 and CSME18 */
	if (idx == 1) {
		g_autoptr(FuMeiCsme11Hfsts1) st = NULL;
		st = fu_mei_csme11_hfsts1_parse(hfsts->buf->data, hfsts->buf->len, 0x0, NULL);
		if (st != NULL)
			self->working_state = fu_mei_csme11_hfsts1_get_working_state(st);
	}

	/* save buffer for later */
	if (self->hfsts[idx] != NULL)
		fu_struct_intel_me_hfsts_unref(self->hfsts[idx]);
	self->hfsts[idx] = fu_struct_intel_me_hfsts_ref(hfsts);
}

static void
fu_intel_me_device_add_attrs_csme11_manufacturing_mode(FuIntelMeDevice *self,
						       FuMeiCsme11Hfsts1 *hfsts1,
						       FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/*
	 * For CSMEv11->CSMEv15 `mfg_mode` is used to indicate the ME being in manufacturing mode,
	 * but for CSMEv16+ this bit has been repurposed to indicate whether BIOS has write access
	 * to the flash descriptor.
	 */
	if (self->family == FU_INTEL_ME_FAMILY_CSME16)
		return;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self),
					   FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fu_security_attrs_append(attrs, attr);

	/* manufacturing mode */
	fwupd_security_attr_add_metadata(attr, "kind", fu_intel_me_family_to_string(self->family));
	if (fu_mei_csme11_hfsts1_get_mfg_mode(hfsts1)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_intel_me_device_add_attrs_csme18_manufacturing_mode(FuIntelMeDevice *self,
						       FuMeiCsme18Hfsts1 *hfsts1,
						       FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self),
					   FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fu_security_attrs_append(attrs, attr);

	/* manufacturing mode, BIOS has access to the SPI descriptor */
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
fu_intel_me_device_add_attrs_csme11_override_strap(FuIntelMeDevice *self,
						   FuMeiCsme11Hfsts1 *hfsts1,
						   FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr =
	    fu_device_security_attr_new(FU_DEVICE(self), FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fu_security_attrs_append(attrs, attr);

	/* flash descriptor security override strap */
	fwupd_security_attr_add_metadata(attr, "kind", fu_intel_me_family_to_string(self->family));
	if (fu_mei_csme11_hfsts1_get_operation_mode(hfsts1) == FU_ME_HFS_MODE_OVERRIDE_JUMPER) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_intel_me_device_add_attrs_csme18_override_strap(FuIntelMeDevice *self,
						   FuMeiCsme18Hfsts1 *hfsts1,
						   FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr =
	    fu_device_security_attr_new(FU_DEVICE(self), FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fu_security_attrs_append(attrs, attr);

	/* flash descriptor security override strap */
	fwupd_security_attr_add_metadata(attr, "kind", fu_intel_me_family_to_string(self->family));
	if (fu_mei_csme18_hfsts1_get_operation_mode(hfsts1) == FU_ME_HFS_MODE_OVERRIDE_JUMPER) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_intel_me_device_add_attrs_csme11_bootguard_enabled(FuIntelMeDevice *self,
						      FuMeiCsme11Hfsts6 *hfsts6,
						      FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self),
					   FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

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
fu_intel_me_device_add_attrs_csme18_bootguard_enabled(FuIntelMeDevice *self,
						      FuMeiCsme18Hfsts5 *hfsts5,
						      FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self),
					   FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

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
fu_intel_me_device_add_attrs_csme11_bootguard_verified(FuIntelMeDevice *self,
						       FuMeiCsme11Hfsts6 *hfsts6,
						       FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self),
					   FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

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
fu_intel_me_device_add_attrs_csme11_bootguard_acm(FuIntelMeDevice *self,
						  FuMeiCsme11Hfsts6 *hfsts6,
						  FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self),
					   FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

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
fu_intel_me_device_add_attrs_csme18_bootguard_acm(FuIntelMeDevice *self,
						  FuMeiCsme18Hfsts5 *hfsts5,
						  FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self),
					   FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

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
fu_intel_me_device_add_attrs_csme11_bootguard_policy(FuIntelMeDevice *self,
						     FuMeiCsme11Hfsts6 *hfsts6,
						     FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self),
					   FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

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
fu_intel_me_device_add_attrs_csme11_bootguard_otp(FuIntelMeDevice *self,
						  FuMeiCsme11Hfsts6 *hfsts6,
						  FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self),
					   FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

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
fu_intel_me_device_add_attrs_csme18_bootguard_otp(FuIntelMeDevice *self,
						  FuMeiCsme18Hfsts6 *hfsts6,
						  FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self),
					   FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

	/* ensure vendor set the FPF configuration fuse */
	if (!fu_mei_csme18_hfsts6_get_fpf_soc_configuration_lock(hfsts6)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_intel_me_device_add_attrs_mei_version(FuIntelMeDevice *self, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self), FWUPD_SECURITY_ATTR_ID_MEI_VERSION);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

	/* format version as string */
	fwupd_security_attr_add_metadata(attr, "version", fu_device_get_version(FU_DEVICE(self)));
	fwupd_security_attr_add_metadata(attr, "kind", fu_intel_me_family_to_string(self->family));

	/* disabled, perhaps HAP? */
	if (self->working_state == FU_ME_HFS_CWS_DISABLED) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
		return;
	}

	/* flash descriptor security override strap */
	if (self->issue == FU_INTEL_ME_ISSUE_VULNERABLE) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_intel_me_device_add_security_attrs(FuDevice *device, FuSecurityAttrs *attrs)
{
	FuIntelMeDevice *self = FU_INTEL_ME_DEVICE(device);

	/* run CSME-specific tests depending on version */
	if ((self->family == FU_INTEL_ME_FAMILY_CSME11 ||
	     self->family == FU_INTEL_ME_FAMILY_CSME16) &&
	    self->hfsts[1] != NULL && self->hfsts[6] != NULL) {
		g_autoptr(FuMeiCsme11Hfsts1) hfsts1 = NULL;
		g_autoptr(FuMeiCsme11Hfsts6) hfsts6 = NULL;

		/* CSME 11 to 17 */
		hfsts1 = fu_mei_csme11_hfsts1_parse(self->hfsts[1]->buf->data,
						    self->hfsts[1]->buf->len,
						    0x0,
						    NULL);
		if (hfsts1 == NULL)
			return;
		hfsts6 = fu_mei_csme11_hfsts6_parse(self->hfsts[6]->buf->data,
						    self->hfsts[6]->buf->len,
						    0x0,
						    NULL);
		if (hfsts6 == NULL)
			return;

		fu_intel_me_device_add_attrs_csme11_manufacturing_mode(self, hfsts1, attrs);
		fu_intel_me_device_add_attrs_csme11_override_strap(self, hfsts1, attrs);
		fu_intel_me_device_add_attrs_csme11_bootguard_enabled(self, hfsts6, attrs);
		fu_intel_me_device_add_attrs_csme11_bootguard_verified(self, hfsts6, attrs);
		fu_intel_me_device_add_attrs_csme11_bootguard_acm(self, hfsts6, attrs);
		fu_intel_me_device_add_attrs_csme11_bootguard_policy(self, hfsts6, attrs);
		fu_intel_me_device_add_attrs_csme11_bootguard_otp(self, hfsts6, attrs);

	} else if (self->family == FU_INTEL_ME_FAMILY_CSME18 && self->hfsts[1] != NULL &&
		   self->hfsts[5] != NULL && self->hfsts[6] != NULL) {
		g_autoptr(FuMeiCsme18Hfsts1) hfsts1 = NULL;
		g_autoptr(FuMeiCsme18Hfsts5) hfsts5 = NULL;
		g_autoptr(FuMeiCsme18Hfsts6) hfsts6 = NULL;

		/* CSME 18+ */
		hfsts1 = fu_mei_csme18_hfsts1_parse(self->hfsts[1]->buf->data,
						    self->hfsts[1]->buf->len,
						    0x0,
						    NULL);
		if (hfsts1 == NULL)
			return;
		hfsts5 = fu_mei_csme18_hfsts5_parse(self->hfsts[5]->buf->data,
						    self->hfsts[5]->buf->len,
						    0x0,
						    NULL);
		if (hfsts5 == NULL)
			return;
		hfsts6 = fu_mei_csme18_hfsts6_parse(self->hfsts[6]->buf->data,
						    self->hfsts[6]->buf->len,
						    0x0,
						    NULL);
		if (hfsts6 == NULL)
			return;
		fu_intel_me_device_add_attrs_csme18_manufacturing_mode(self, hfsts1, attrs);
		fu_intel_me_device_add_attrs_csme18_override_strap(self, hfsts1, attrs);
		fu_intel_me_device_add_attrs_csme18_bootguard_enabled(self, hfsts5, attrs);
		fu_intel_me_device_add_attrs_csme18_bootguard_acm(self, hfsts5, attrs);
		fu_intel_me_device_add_attrs_csme18_bootguard_otp(self, hfsts6, attrs);
	} else {
		g_autoptr(FwupdSecurityAttr) attr = NULL;

		/* not supported */
		attr = fu_device_security_attr_new(FU_DEVICE(self),
						   FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		fu_security_attrs_append(attrs, attr);
		return;
	}

	/* all */
	fu_intel_me_device_add_attrs_mei_version(self, attrs);
}

static gboolean
fu_intel_me_device_from_json(FuDevice *device, FwupdJsonObject *json_obj, GError **error)
{
	FuIntelMeDevice *self = FU_INTEL_ME_DEVICE(device);
	const gchar *version;

	/* optional properties */
	version = fwupd_json_object_get_string(json_obj, "Version", NULL);
	if (version != NULL)
		fu_device_set_version(device, version);
	for (guint i = 1; i < G_N_ELEMENTS(self->hfsts); i++) {
		gint64 tmp64 = 0;
		g_autofree gchar *title = g_strdup_printf("Hfsts%x", i);
		g_autoptr(FuStructIntelMeHfsts) st = fu_struct_intel_me_hfsts_new();

		if (!fwupd_json_object_get_integer_with_default(json_obj, title, &tmp64, 0, error))
			return FALSE;
		fu_struct_intel_me_hfsts_set_value(st, (guint32)tmp64);
		fu_intel_me_device_set_hfsts(self, i, st);
	}

	/* success */
	return TRUE;
}

static void
fu_intel_me_device_add_json(FuDevice *device, FwupdJsonObject *json_obj, FwupdCodecFlags flags)
{
	FuIntelMeDevice *self = FU_INTEL_ME_DEVICE(device);
	fwupd_json_object_add_string(json_obj, "GType", "FuIntelMeDevice");
	fwupd_json_object_add_string(json_obj, "BackendName", "udev");
	if (fu_device_get_version(device) != NULL)
		fwupd_json_object_add_string(json_obj, "Version", fu_device_get_version(device));
	for (guint i = 1; i < G_N_ELEMENTS(self->hfsts); i++) {
		g_autofree gchar *title = g_strdup_printf("Hfsts%x", i);
		if (self->hfsts[i] == NULL)
			continue;
		fwupd_json_object_add_integer(json_obj,
					      title,
					      fu_struct_intel_me_hfsts_get_value(self->hfsts[i]));
	}
}

static void
fu_intel_me_device_init(FuIntelMeDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "ME");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_physical_id(FU_DEVICE(self), "PCI_SLOT_NAME=0000:00:16.0");
	fu_device_add_icon(FU_DEVICE(self), "cpu");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	g_signal_connect(FWUPD_DEVICE(self),
			 "notify::version",
			 G_CALLBACK(fu_intel_me_device_version_notify_cb),
			 NULL);
}

static void
fu_intel_me_device_constructed(GObject *obj)
{
	FuIntelMeDevice *self = FU_INTEL_ME_DEVICE(obj);
	fu_device_add_instance_id_full(FU_DEVICE(self),
				       "PCI\\VEN_8086",
				       FU_DEVICE_INSTANCE_FLAG_QUIRKS);
}

static void
fu_intel_me_device_finalize(GObject *object)
{
	FuIntelMeDevice *self = FU_INTEL_ME_DEVICE(object);

	for (guint i = 1; i < G_N_ELEMENTS(self->hfsts); i++) {
		if (self->hfsts[i] != NULL)
			fu_struct_intel_me_hfsts_unref(self->hfsts[i]);
	}

	G_OBJECT_CLASS(fu_intel_me_device_parent_class)->finalize(object);
}

static void
fu_intel_me_device_class_init(FuIntelMeDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->constructed = fu_intel_me_device_constructed;
	object_class->finalize = fu_intel_me_device_finalize;
	device_class->to_string = fu_intel_me_device_to_string;
	device_class->from_json = fu_intel_me_device_from_json;
	device_class->add_json = fu_intel_me_device_add_json;
	device_class->add_security_attrs = fu_intel_me_device_add_security_attrs;
}

/**
 * fu_intel_me_device_new:
 * @ctx: a #FuContext
 *
 * Creates a new Intel ME device.
 *
 * Returns: (transfer full): a #FuIntelMeDevice
 *
 * Since: 2.1.1
 **/
FuIntelMeDevice *
fu_intel_me_device_new(FuContext *ctx)
{
	return g_object_new(FU_TYPE_INTEL_ME_DEVICE, "context", ctx, NULL);
}
