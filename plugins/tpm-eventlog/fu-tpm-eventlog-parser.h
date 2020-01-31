/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#include "fu-tpm-eventlog-common.h"

typedef enum {
	FU_TPM_EVENTLOG_PARSER_FLAG_NONE		= 0,
	FU_TPM_EVENTLOG_PARSER_FLAG_ALL_PCRS		= 1 << 0,
	FU_TPM_EVENTLOG_PARSER_FLAG_ALL_ALGS		= 1 << 1,
	FU_TPM_EVENTLOG_PARSER_FLAG_LAST
} FuTpmEventlogParserFlags;

GPtrArray	*fu_tpm_eventlog_parser_new	(const guint8	*buf,
						 gsize		 bufsz,
						 FuTpmEventlogParserFlags flags,
						 GError		**error);
void		 fu_tpm_eventlog_item_to_string	(FuTpmEventlogItem *item,
						 guint		 idt,
						 GString	*str);
