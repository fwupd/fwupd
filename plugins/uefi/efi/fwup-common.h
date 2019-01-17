/*
 * Copyright (C) 2015-2016 Peter Jones <pjones@redhat.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef _FWUP_COMMON_H
#define _FWUP_COMMON_H

#include "fwup-efi.h"

VOID		 fwup_msleep		(unsigned long		 msecs);
EFI_STATUS	 fwup_time		(EFI_TIME		*ts);
EFI_STATUS	 fwup_read_file		(EFI_FILE_HANDLE	 fh,
					 UINT8			**buf_out,
					 UINTN			*buf_size_out);
VOID		*fwup_malloc_raw	(UINTN			 size);

VOID		*fwup_malloc		(UINTN			 size);
VOID		*fwup_malloc0		(UINTN			 size);

#endif /* _FWUP_COMMON_H */
