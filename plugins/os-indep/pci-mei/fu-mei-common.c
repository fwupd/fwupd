/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mei-common.h"
#include "fu-mei-struct.h"

static gint
fu_mei_common_cmp_version(FuMeiVersion *vers1, FuMeiVersion *vers2)
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
fu_mei_common_is_csme_vulnerable(FuMeiVersion *vers)
{
	struct {
		guint8 major_eq;
		guint8 minor_eq;
		guint8 hotfix_ge;
	} verdata[] = {{11, 8, 92},
		       {11, 12, 92},
		       {11, 22, 92},
		       {12, 0, 90},
		       {13, 0, 60},
		       {13, 30, 30},
		       {13, 50, 20},
		       {14, 1, 65},
		       {14, 5, 45},
		       {15, 0, 40},
		       {15, 40, 20},
		       {0, 0, 0}};
	for (guint i = 0; verdata[i].major_eq != 0; i++) {
		if (vers->major == verdata[i].major_eq && vers->minor == verdata[i].minor_eq) {
			return vers->hotfix >= verdata[i].hotfix_ge ? FU_MEI_ISSUE_PATCHED
								    : FU_MEI_ISSUE_VULNERABLE;
		}
	}
	return FU_MEI_ISSUE_NOT_VULNERABLE;
}

FuMeiIssue
fu_mei_common_is_txe_vulnerable(FuMeiVersion *vers)
{
	struct {
		guint8 major_eq;
		guint8 minor_eq;
		guint8 hotfix_ge;
	} verdata[] = {{3, 1, 92}, {4, 0, 45}, {0, 0, 0}};
	for (guint i = 0; verdata[i].major_eq != 0; i++) {
		if (vers->major == verdata[i].major_eq && vers->minor == verdata[i].minor_eq) {
			return vers->hotfix >= verdata[i].hotfix_ge ? FU_MEI_ISSUE_PATCHED
								    : FU_MEI_ISSUE_VULNERABLE;
		}
	}
	return FU_MEI_ISSUE_NOT_VULNERABLE;
}

FuMeiIssue
fu_mei_common_is_sps_vulnerable(FuMeiVersion *vers)
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
			if (fu_mei_common_cmp_version(vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		} else if (vers->platform == 0xE) { /* Bakerville */
			FuMeiVersion ver2 = {
			    .major = 4,
			    .minor = 0,
			    .hotfix = 4,
			    .buildno = 112,
			};
			if (fu_mei_common_cmp_version(vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		} else if (vers->platform == 0xB) { /* Harrisonville */
			FuMeiVersion ver2 = {
			    .major = 4,
			    .minor = 0,
			    .hotfix = 4,
			    .buildno = 193,
			};
			if (fu_mei_common_cmp_version(vers, &ver2) < 0)
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
			if (fu_mei_common_cmp_version(vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		} else if (vers->platform == 0xD) { /* MonteVista */
			FuMeiVersion ver2 = {
			    .major = 4,
			    .minor = 8,
			    .hotfix = 4,
			    .buildno = 51,
			};
			if (fu_mei_common_cmp_version(vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		}
		return FU_MEI_ISSUE_NOT_VULNERABLE;
	}
	if (vers->major == 5) {
		if (vers->platform == 0x10) { /* Mehlow */
			FuMeiVersion ver2 = {5, 1, 3, 89};
			if (fu_mei_common_cmp_version(vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		}
		return FU_MEI_ISSUE_NOT_VULNERABLE;
	}
	return FU_MEI_ISSUE_PATCHED;
}
