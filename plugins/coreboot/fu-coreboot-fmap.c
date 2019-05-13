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
#include <glib.h>
#include <glib/gstdio.h>

#include "fu-plugin-vfuncs.h"
#include "fu-device-metadata.h"
#include "fu-device-private.h"
#include "fu-plugin-coreboot.h"
#include "fmap_serialized.h"

/* Depends on the kernel module coreboot-fmap.ko */

/*
 * Provides the FMAP through a kernel module if available.
 * The caller has to free the returned object.
 */
struct fmap *
fu_plugin_coreboot_find_fmap (FuPlugin *plugin, GError **error)
{
	gchar *tmp = NULL;

	if (!g_file_get_contents ("/sys/firmware/fmap", &tmp, NULL, error)) {
		g_prefix_error (error, "Failed to read /sys/firmware/fmap: ");
		return NULL;
	}

	// FIXME: Do sanity check
	return (struct fmap *)tmp;
}

/*
 * Provides the active CBFS partition through a kernel module if available.
 * The caller has to free the returned object.
 */
gchar *
fu_plugin_coreboot_find_cbfs_active_partition (FuPlugin *plugin, GError **error)
{
        gchar *tmp = NULL;

        if (!g_file_get_contents ("/sys/firmware/cbfs_active_partition", &tmp, NULL, error)) {
		g_prefix_error (error, "Failed to read /sys/firmware/cbfs_active_partition: ");
                return NULL;
	}

        // FIXME: Do sanity check
        return tmp;
}

/* FIXME: The following only works on some x86 platforms that have STRICT_DEV_MEM disabled */
static GBytes *
fu_plugin_coreboot_clone_region (FuPlugin *plugin, GError **error, const struct fmap *fmap, const struct fmap_area *fmap_area)
{
	gint fd;
	void *bios_mmio;
	goffset off;
	GBytes *tmp;
	gint size;

	size = (fmap_area->size + getpagesize()) & ~(getpagesize() - 1);

	// on x86 the firmware is memory mapped below 4GiB
	fd = open ("/dev/mem", O_RDONLY);
	if (fd < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "Failed to open /dev/mem");
		return NULL;
	}

	off = (1ULL << 32) - fmap->size + fmap_area->offset;
	g_debug ("Cloning region '%s' %lx @ %x", fmap_area->name, (unsigned long int)off, (unsigned int)size);

	bios_mmio = mmap (NULL, size, PROT_READ, MAP_SHARED, fd, off);
	if (bios_mmio == MAP_FAILED) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "Failed to mmap /dev/mem");
		close (fd);
		return NULL;
	}
	tmp = g_malloc0 (size);
	if (!tmp) {
		munmap (bios_mmio, size);
		close (fd);
		return NULL;
	}
	memcpy (tmp, bios_mmio, size);
	munmap (bios_mmio, size);
	close (fd);
	return tmp;
}

static FuDevice*
fu_plugin_coreboot_add_fmap_device (FuPlugin *plugin,
				   GError **error,
				   FuDevice *parent,
				   struct fmap *fmap,
				   struct fmap_area *fmap_area,
				   gboolean immutable)
{
	g_autofree gchar *sum = NULL;
	FuDevice *dev;
	//GBytes *region;
	g_autofree const gchar *triplet = NULL;
	g_autofree gchar *inst_id = NULL;
	g_autofree gchar *name = NULL;

	dev = fu_device_new ();
	if (!dev)
		return NULL;

	fu_device_set_vendor (dev, fu_device_get_vendor (parent));
	fu_device_set_id (dev, fu_device_get_id (parent));
	name = fu_plugin_coreboot_get_name_for_type (plugin, (const char *)fmap_area->name);
	if (!name)
		name = fu_device_get_name (parent);
	fu_device_set_name (dev, name);
	fu_device_set_vendor (dev, fu_device_get_vendor (parent));

	sum = g_strdup_printf ("%s, partition '%s'\n", fu_device_get_summary (parent), fmap_area->name);
	fu_device_set_summary (dev, sum);

	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon (dev, "computer");
	inst_id = g_strdup_printf ("system-fimware-partition-%s\n", fmap_area->name);
	fu_device_add_instance_id (dev, "system-fimware-partition");
	fu_device_add_instance_id (dev, inst_id);
	fu_device_add_parent_guid (dev, "main-system-firmware");

	fu_device_set_metadata (dev, FU_DEVICE_METADATA_FLASHROM_DEVICE_KIND, "system-firmware");
	fu_device_set_metadata (dev, FU_DEVICE_METADATA_FLASHROM_FMAP_NAME, (const char *)fmap_area->name);

	if (!immutable)
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_firmware_size_max (dev, fmap_area->size);

	// Fill in parent version as fallback
	//fu_device_set_version (dev, fu_device_get_version (parent), FWUPD_VERSION_FORMAT_TRIPLET);

	//FIXME: that only works on x86 with STRICT_DEV_MEM disabled
	//The correct way would be to use libflashrom to read parts of the flash
#if 0
	// try to get version from CBFS in current partition
	region = fu_plugin_coreboot_clone_region (plugin, error, fmap, fmap_area);
	if (region) {
		guint8 *file = fu_plugin_coreboot_get_raw_cbfs_file (region, fmap_area->size, "revision");
		if (file) {
			g_debug ("Found file 'revision' in CBFS@%s", fmap_area->name);
			triplet = fu_plugin_coreboot_parse_revision_file ((const gchar *)file, error);
			g_debug ("FMAP coreboot version %s", triplet);
			if (triplet)
				fu_device_set_version (dev, triplet, FWUPD_VERSION_FORMAT_TRIPLET);
		}
		g_free (region);
	} else {
		g_debug ("Failed to MMAP region %s", fmap_area->name);
	}
#endif
	return dev;

}

/* Return a pointer to the corresponding fmap_area by name.
 * Returns NULL if not found.
 */
static struct fmap_area *
fu_plugin_coreboot_fmap_area_by_name (const struct fmap *fmap, const char *name)
{
	for (gint i = 0; i < fmap->nareas; i++) {
		if (g_strcmp0 (name, (const char *)fmap->areas[i].name) == 0)
			return &fmap->areas[i];
	}
	return NULL;
}

/* Add FMAP partitions based on 'known' names */
gboolean
fu_plugin_coreboot_add_fmap_devices (FuPlugin *plugin, GError **error, FuDevice *parent, const struct fmap *fmap)
{
	static const char *regions[] = {
		// VBOOT enabled devices have up to two R/W partitions:
		"RW_SECTION_A",
		"RW_SECTION_B",
		// some VBOOT enabled devices have one RO section:
		"RO_SECTION",
		// Autogenerated FMAP on x86 has one region:
		"BIOS",
	};
	struct fmap_area *area;
	gint vboot_rw_partition_cnt = 0;
	FuDevice *dev;

	if (fu_plugin_coreboot_fmap_area_by_name (fmap, "RW_SECTION_A"))
		vboot_rw_partition_cnt++;
	if (fu_plugin_coreboot_fmap_area_by_name (fmap, "RW_SECTION_B"))
                vboot_rw_partition_cnt++;

	for (guint i = 0; i < G_N_ELEMENTS(regions); i++) {
		gboolean imm = FALSE;
		area = fu_plugin_coreboot_fmap_area_by_name (fmap, regions[i]);
		if (!area)
			continue;

		if (g_strstr_len(regions[i], 3, "RO_") != NULL)
			imm = TRUE;

		// FIXME: Add priorities to partion devices

		dev = fu_plugin_coreboot_add_fmap_device (plugin, error, parent, fmap, area, imm);
		if (!dev)
			return FALSE;

		// Convert instances to GUID
		fu_device_convert_instance_ids (dev);

		// Now register with flashrom
		fu_plugin_device_register (plugin, dev);
	}


	return TRUE;
}
