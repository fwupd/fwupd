/*
 * Copyright (C) 2015-2016 Peter Jones <pjones@redhat.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fwup-efi.h"

VOID		 fwup_msleep		(unsigned long		 msecs);
EFI_STATUS	 fwup_time		(EFI_TIME		*ts);
EFI_STATUS	 fwup_read_file		(EFI_FILE_HANDLE	 fh,
					 UINT8			**buf_out,
					 UINTN			*buf_size_out);
VOID		*fwup_malloc_raw	(UINTN			 size);

VOID		*fwup_malloc		(UINTN			 size);
VOID		*fwup_malloc0		(UINTN			 size);

#define fwup_new(struct_type, n)	((struct_type*)fwup_malloc((n)*sizeof(struct_type)))
#define fwup_new0(struct_type, n)	((struct_type*)fwup_malloc0((n)*sizeof(struct_type)))
