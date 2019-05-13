/*
 * Copyright (C) 2019 9elements Agency GmbH <patrick.rudolph@9elements.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "fu-plugin-vfuncs.h"
#include "fu-device-metadata.h"
#include "fu-plugin-coreboot.h"
#include "cbfs_serialized.h"

/* Returns a pointer to the CBFS master header in "mem" if found,
 * NULL otherwise. There's only one master header in the firmware ROM,
 * but the firmware ROM can contain multiple CBFS.
 */
const struct cbfs_header *
fu_plugin_coreboot_find_cbfs_master (const void *mem, const guint region_size)
{

	// search from bottom to top as the header is at the start of the region
	for (guint i = 0; i < region_size; i += 16) {
		const struct cbfs_header *header = (const struct cbfs_header *)((guint8 *)mem + i);
		guint offset;
		guint size;

		// Match signature
		if (g_ntohl (header->magic) != CBFS_HEADER_MAGIC)
			continue;
		// Only accept supported major versions
		if (g_ntohl (header->version) != CBFS_HEADER_VERSION)
			continue;

		offset = g_ntohl (header->offset);
		size = g_ntohl (header->romsize);
		// Sanity check
		if (size > region_size)
			return FALSE;
		size -= offset;
		g_debug ("Found CBFS with size 0x%x @ 0x%x", size, offset);

		return header;
	}

	g_debug ("CBFS not found");
	return NULL;
}

/* Returns a pointer to the first valid CBFS file in "mem" if found,
 * NULL otherwise.
 */
const struct cbfs_file *
fu_plugin_coreboot_find_cbfs (const void *mem, const guint region_size)
{
	for (guint i = 0; i < region_size; i += CBFS_ALIGNMENT) {
		const struct cbfs_file *file = (const struct cbfs_file *)((guint8 *)mem + i);
		if (memcmp (file->magic, CBFS_FILE_MAGIC, sizeof (file->magic)) != 0)
			continue;
		// Sanity checks
		if (g_ntohl (file->len) > (region_size - i))
			continue;
		if (g_ntohl (file->offset) > (region_size - i))
			continue;
		if (g_ntohl (file->offset) < sizeof (struct cbfs_file))
			continue;
		return file;
	}
	return NULL;
}

/* Returns a pointer to the file in "mem" matching "name" if found, NUL otherwise. */
struct cbfs_file *
fu_plugin_coreboot_find_cbfs_file (const void *mem, const guint region_size, const gchar *name)
{
	const struct cbfs_file *first;
	guint offset;

	first = fu_plugin_coreboot_find_cbfs(mem, region_size);
	if (!first)
		return NULL;

	offset = 0;
	while (offset < (region_size - sizeof (struct cbfs_file))) {
		const struct cbfs_file *file = (const struct cbfs_file *)(((guint8 *)first) + offset);

		// filename starts right after struct cbfs_file and is NULL terminated
		const gchar *filename = (const gchar *)(file + 1);

		if (memcmp (file->magic, CBFS_FILE_MAGIC, sizeof (file->magic)) != 0) {
			offset += CBFS_ALIGNMENT;
			continue;
		}
		// Sanity checks
		if (g_ntohl (file->len) > (region_size - offset)) {
			offset += CBFS_ALIGNMENT;
			continue;
		}
		if (g_ntohl (file->offset) > (region_size - offset)) {
			offset += CBFS_ALIGNMENT;
			continue;
		}
		if (g_ntohl (file->offset) < sizeof(struct cbfs_file)) {
			offset += CBFS_ALIGNMENT;
			continue;
		}

		// Have a valid file
		g_debug ("Found file '%s' @ 0x%x", filename, offset);

		if (g_ntohl (file->type) == CBFS_TYPE_DELETED ||
		    g_ntohl (file->type) == CBFS_TYPE_DELETED2) {
			offset += g_ntohl (file->offset) + g_ntohl (file->len);
			offset = (offset + (CBFS_ALIGNMENT - 1)) & ~(CBFS_ALIGNMENT - 1);
			continue;
		}
		if (g_strcmp0 (filename, name) != 0) {
			offset += g_ntohl (file->offset) + g_ntohl (file->len);
			offset = (offset + (CBFS_ALIGNMENT - 1)) & ~(CBFS_ALIGNMENT - 1);
			continue;
		}
		// FOUND
		return file;
	}

	return NULL;
}

/* Returns a pointer to the raw file contents in "mem" if found, NULL otherwise.
 * The file might be compressed.
 */
const guint8 *
fu_plugin_coreboot_get_raw_cbfs_file (const void *mem, const guint region_size, const gchar *name)
{
	const struct cbfs_file *file;

	file = fu_plugin_coreboot_find_cbfs_file (mem, region_size, name);
	if (!file)
		return NULL;

	return (guint8 *)file + g_ntohl (file->offset);
}
