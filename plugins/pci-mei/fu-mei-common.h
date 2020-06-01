/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

typedef enum {
	FU_MEI_FAMILY_UNKNOWN,
	FU_MEI_FAMILY_SPS,
	FU_MEI_FAMILY_TXE,
	FU_MEI_FAMILY_ME,
	FU_MEI_FAMILY_CSME,
} FuMeiFamily;

typedef enum {
	FU_MEI_ISSUE_UNKNOWN,
	FU_MEI_ISSUE_NOT_VULNERABLE,
	FU_MEI_ISSUE_VULNERABLE,
	FU_MEI_ISSUE_PATCHED,
} FuMeiIssue;

typedef struct {
	guint8	platform;
	guint8	major;
	guint8	minor;
	guint8	hotfix;
	guint16	buildno;
} FuMeiVersion;

const gchar	*fu_mei_common_family_to_string		(FuMeiFamily	 family);
FuMeiIssue	 fu_mei_common_is_csme_vulnerable	(FuMeiVersion	*vers);
FuMeiIssue	 fu_mei_common_is_txe_vulnerable	(FuMeiVersion	*vers);
FuMeiIssue	 fu_mei_common_is_sps_vulnerable	(FuMeiVersion	*vers);
