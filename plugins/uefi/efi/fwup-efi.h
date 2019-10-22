/*
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#define FWUPDATE_ATTEMPT_UPDATE		0x00000001
#define FWUPDATE_ATTEMPTED		0x00000002

#define UPDATE_INFO_VERSION	7

static __attribute__((__unused__)) EFI_GUID empty_guid =
	{0x0,0x0,0x0,{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
static __attribute__((__unused__))EFI_GUID fwupdate_guid =
	{0x0abba7dc,0xe516,0x4167,{0xbb,0xf5,0x4d,0x9d,0x1c,0x73,0x94,0x16}};
static __attribute__((__unused__))EFI_GUID global_variable_guid = EFI_GLOBAL_VARIABLE;

typedef struct {
	UINT32		 update_info_version;

	/* stuff we need to apply an update */
	EFI_GUID	 guid;
	UINT32		 capsule_flags;
	UINT64		 hw_inst;

	EFI_TIME	 time_attempted;

	/* our metadata */
	UINT32		 status;

	/* variadic device path */
	union {
		EFI_DEVICE_PATH	 dp;
		UINT8		 dp_buf[0];
	};
} __attribute__((__packed__)) FWUP_UPDATE_INFO;

typedef struct {
	UINT32		 attributes;
	UINT16		 file_path_list_length;
	CHAR16		*description;
}  __attribute__((__packed__)) EFI_LOAD_OPTION;

EFI_STATUS	 fwup_delete_variable	(CHAR16		*name,
					 EFI_GUID	*guid);
EFI_STATUS	 fwup_set_variable	(CHAR16		*name,
					 EFI_GUID	*guid,
					 VOID		*data,
					 UINTN		 size,
					 UINT32		 attrs);
EFI_STATUS	 fwup_get_variable	(CHAR16		*name,
					 EFI_GUID	*guid,
					 VOID		**buf_out,
					 UINTN		*buf_size_out,
					 UINT32		*attrs_out);
