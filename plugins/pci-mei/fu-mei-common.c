/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-mei-common.h"

const gchar *
fu_mei_common_family_to_string (FuMeiFamily family)
{
	if (family == FU_MEI_FAMILY_SPS)
		return "SPS";
	if (family == FU_MEI_FAMILY_TXE)
		return "TXE";
	if (family == FU_MEI_FAMILY_ME)
		return "ME";
	if (family == FU_MEI_FAMILY_CSME)
		return "CSME";
	return "AMT";
}

static gint
fu_mei_common_cmp_version (FuMeiVersion *vers1, FuMeiVersion *vers2)
{
	guint16 vers1buf[] = {
		vers1->major,
		vers1->minor,
		vers1->hotfix,
		vers1->buildno,
	};
	guint16 vers2buf[] = {
		vers2->major,
		vers2->minor,
		vers2->hotfix,
		vers2->buildno,
	};
	for (guint i = 0; i < 4; i++) {
		if (vers1buf[i] < vers2buf[i])
			return -1;
		if (vers1buf[i] > vers2buf[i])
			return 1;
	}
	return 0;
}

FuMeiIssue
fu_mei_common_is_csme_vulnerable (FuMeiVersion *vers)
{
	if (vers->major == 11 && vers->minor == 8 && vers->hotfix >= 70)
		return FU_MEI_ISSUE_PATCHED;
	if (vers->major == 11 && vers->minor == 11 && vers->hotfix >= 70)
		return FU_MEI_ISSUE_PATCHED;
	if (vers->major == 11 && vers->minor == 22 && vers->hotfix >= 70)
		return FU_MEI_ISSUE_PATCHED;
	if (vers->major == 12 && vers->minor == 0 && (vers->hotfix == 49 || vers->hotfix >= 56))
		return FU_MEI_ISSUE_PATCHED;
	if (vers->major == 13 && vers->minor == 0 && vers->hotfix >= 21)
		return FU_MEI_ISSUE_PATCHED;
	if (vers->major == 14 && vers->minor == 0 && vers->hotfix >= 11)
		return FU_MEI_ISSUE_PATCHED;
	if (vers->major == 15)
		return FU_MEI_ISSUE_NOT_VULNERABLE;
	return FU_MEI_ISSUE_VULNERABLE;
}

FuMeiIssue
fu_mei_common_is_txe_vulnerable (FuMeiVersion *vers)
{
	if (vers->major == 3 && vers->minor == 1 && vers->hotfix >= 70)
		return FU_MEI_ISSUE_PATCHED;
	if (vers->major == 4 && vers->minor == 0 && vers->hotfix >= 20)
		return FU_MEI_ISSUE_PATCHED;
	if (vers->major == 5)
		return FU_MEI_ISSUE_NOT_VULNERABLE;
	return FU_MEI_ISSUE_VULNERABLE;
}

FuMeiIssue
fu_mei_common_is_sps_vulnerable (FuMeiVersion *vers)
{
	if (vers->major == 3 || vers->major > 5)
		return FU_MEI_ISSUE_NOT_VULNERABLE;
	if (vers->major == 4) {
		if (vers->hotfix < 44)
			return FU_MEI_ISSUE_VULNERABLE;
		if (vers->platform == 0xA) { /* Purley */
			FuMeiVersion ver2 = {
				.major = 4,
				.minor = 1,
				.hotfix = 4,
				.buildno = 339,
			};
			if (fu_mei_common_cmp_version (vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		} else if (vers->platform == 0xE) { /* Bakerville */
			FuMeiVersion ver2 = {
				.major = 4,
				.minor = 0,
				.hotfix = 4,
				.buildno = 112,
			};
			if (fu_mei_common_cmp_version (vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		} else if (vers->platform == 0xB) { /* Harrisonville */
			FuMeiVersion ver2 = {
				.major = 4,
				.minor = 0,
				.hotfix = 4,
				.buildno = 193,
			};
			if (fu_mei_common_cmp_version (vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		} else if (vers->platform == 0x9) { /* Greenlow */
			FuMeiVersion ver2 = {
				.major = 4,
				.minor = 1,
				.hotfix = 4,
				.buildno = 88,
			};
			if (vers->minor < 1)
				return FU_MEI_ISSUE_NOT_VULNERABLE;
			if (fu_mei_common_cmp_version (vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		} else if (vers->platform == 0xD) { /* MonteVista */
			FuMeiVersion ver2 = {
				.major = 4,
				.minor = 8,
				.hotfix = 4,
				.buildno = 51,
			};
			if (fu_mei_common_cmp_version (vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		}
		return FU_MEI_ISSUE_NOT_VULNERABLE;
	}
	if (vers->major == 5) {
		if (vers->platform == 0x10) { /* Mehlow */
			FuMeiVersion ver2 = { 5, 1, 3, 89 };
			if (fu_mei_common_cmp_version (vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		}
		return FU_MEI_ISSUE_NOT_VULNERABLE;
	}
	return FU_MEI_ISSUE_PATCHED;
}
