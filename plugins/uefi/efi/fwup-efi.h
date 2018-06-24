/*
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef _FWUP_EFI_H
#define _FWUP_EFI_H

#define FWUPDATE_ATTEMPT_UPDATE		0x00000001
#define FWUPDATE_ATTEMPTED		0x00000002

#define UPDATE_INFO_VERSION	7

#ifdef _EFI_INCLUDE_
#define efidp_header EFI_DEVICE_PATH
#define efi_guid_t EFI_GUID
#endif /* _EFI_INCLUDE_ */

typedef struct {
	uint8_t version;
	uint8_t checksum;
	uint8_t image_type;
	uint8_t reserved;
	uint32_t mode;
	uint32_t x_offset;
	uint32_t y_offset;
} ux_capsule_header_t;

typedef struct update_info_s {
	uint32_t update_info_version;

	/* stuff we need to apply an update */
	efi_guid_t guid;
	uint32_t capsule_flags;
	uint64_t hw_inst;

	EFI_TIME time_attempted;

	/* our metadata */
	uint32_t status;

	/* variadic device path */
	union {
		efidp_header *dp_ptr;
		efidp_header dp;
		uint8_t dp_buf[0];
	};
} __attribute__((__packed__)) update_info;

#endif /* _FWUP_EFI_H */
