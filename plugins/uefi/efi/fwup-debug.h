/*
 * Copyright (C) 2015-2016 Peter Jones <pjones@redhat.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

typedef enum {
	FWUP_DEBUG_LEVEL_DEBUG,
	FWUP_DEBUG_LEVEL_INFO,
	FWUP_DEBUG_LEVEL_WARNING,
	FWUP_DEBUG_LEVEL_LAST
} FwupLogLevel;

VOID		 fwup_log		(FwupLogLevel	 level,
					 const char	*func,
					 const char	*file,
					 const int	 line,
					 CHAR16		*fmt,
					 ...);

BOOLEAN		 fwup_debug_get_enabled	(VOID);
VOID		 fwup_debug_set_enabled	(BOOLEAN	 val);

#define fwup_debug(fmt, args...) fwup_log(FWUP_DEBUG_LEVEL_DEBUG, __func__, __FILE__, __LINE__, fmt, ## args )
#define fwup_info(fmt, args...) fwup_log(FWUP_DEBUG_LEVEL_INFO, __func__, __FILE__, __LINE__, fmt, ## args )
#define fwup_warning(fmt, args...) fwup_log(FWUP_DEBUG_LEVEL_WARNING, __func__, __FILE__, __LINE__, fmt, ## args )
