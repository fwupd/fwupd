/*
 * Copyright 2026 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-dell-psb-device.h"

struct _FuDellPsbDevice {
	FuBlockDevice parent_instance;
};

G_DEFINE_TYPE(FuDellPsbDevice, fu_dell_psb_device, FU_TYPE_BLOCK_DEVICE)

/* READ BUFFER(10), mode 0x01, offset 0xE6E007, length 0x3C */
#define FU_DELL_PSB_READ_BUFFER_CMD  0x3C
#define FU_DELL_PSB_WRITE_BUFFER_CMD 0x3B
#define FU_DELL_PSB_VERSION_LENGTH   0x3C
#define FU_DELL_PSB_CHUNK_SIZE	     0x8000 /* 32 KiB */

static gboolean
fu_dell_psb_device_probe(FuDevice *device, GError **error)
{
	g_autoptr(FuDevice) device_scsi = NULL;
	g_autoptr(FuDevice) pci = NULL;

	/* the udev backend has no scsi physical-ID scheme, so build one from the
	 * scsi_device parent, as FuScsiDevice does */
	device_scsi = fu_device_get_backend_parent_with_subsystem(device, "scsi:scsi_device", NULL);
	if (device_scsi != NULL) {
		g_autofree gchar *devpath =
		    fu_udev_device_get_devpath(FU_UDEV_DEVICE(device_scsi));
		if (devpath != NULL) {
			g_autofree gchar *physical_id = g_strdup_printf("DEVPATH=%s", devpath);
			fu_device_set_physical_id(device, physical_id);
		}
	}

	/* the switch is a PCI function; its IDs are what the metadata matches on */
	pci = fu_device_get_backend_parent_with_subsystem(device, "pci", error);
	if (pci == NULL)
		return FALSE;
	if (!fu_device_probe(pci, error))
		return FALSE;
	fu_device_incorporate(device,
			      pci,
			      FU_DEVICE_INCORPORATE_FLAG_VENDOR |
				  FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS |
				  FU_DEVICE_INCORPORATE_FLAG_VID |
				  FU_DEVICE_INCORPORATE_FLAG_PID |
				  FU_DEVICE_INCORPORATE_FLAG_INSTANCE_KEYS |
				  FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);
	if (!fu_device_build_instance_id(device, error, "PCI", "VEN", "DEV", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "PCI", "VEN", "DEV", "SUBSYS", NULL))
		return FALSE;

	/* set after incorporate, which may have copied a donor name */
	fu_device_set_name(device, "PCIe Switch Firmware");
	fu_device_set_summary(device, "Dell PowerEdge PSB PCIe switch");

	/* success */
	return TRUE;
}

static gboolean
fu_dell_psb_device_setup(FuDevice *device, GError **error)
{
	/* nocheck:magic */
	const guint8 cdb[] = {FU_DELL_PSB_READ_BUFFER_CMD,
			       0x01, /* mode */
			       0xE6, /* buffer offset, big endian */
			       0xE0,
			       0x07,
			       0x00, /* length, big endian */
			       FU_DELL_PSB_VERSION_LENGTH,
			       0x00,
			       0x00,
			       0x00};
	guint8 buf[FU_DELL_PSB_VERSION_LENGTH] = {0};
	g_autofree gchar *version = NULL;

	if (!fu_block_device_sg_io_cmd_read(FU_BLOCK_DEVICE(device),
					    cdb,
					    sizeof(cdb),
					    buf,
					    sizeof(buf),
					    error)) {
		g_prefix_error_literal(error, "failed to read PSB firmware version: ");
		return FALSE;
	}

	version = g_strdup_printf("%u.%u.%u.%u", buf[12], buf[13], buf[14], buf[15]);
	fu_device_set_version(device, version);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_dell_psb_device_prepare_firmware(FuDevice *device,
				    GInputStream *stream,
				    FuProgress *progress,
				    FuFirmwareParseFlags flags,
				    GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_firmware_new();

	/* the payload is written verbatim: no header stripping, no trailer */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_dell_psb_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	guint32 offset = 0;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* prepare chunks */
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						FU_DELL_PSB_CHUNK_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	/* write each block */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		guint8 cdb[10] = {FU_DELL_PSB_WRITE_BUFFER_CMD,
				  0x02, /* mode */
				  0x00 /* buffer id */};

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		fu_memwrite_uint24(cdb + 3, offset, G_BIG_ENDIAN);
		fu_memwrite_uint24(cdb + 6, fu_chunk_get_data_sz(chk), G_BIG_ENDIAN);
		if (!fu_block_device_sg_io_cmd_write(FU_BLOCK_DEVICE(device),
						     cdb,
						     sizeof(cdb),
						     fu_chunk_get_data(chk),
						     fu_chunk_get_data_sz(chk),
						     error)) {
			g_prefix_error(error,
				       "failed to write PSB chunk 0x%x at offset 0x%x: ",
				       fu_chunk_get_idx(chk),
				       offset);
			return FALSE;
		}

		fu_progress_step_done(progress);
		offset += fu_chunk_get_data_sz(chk);
	}

	/* the switch activates the new image on the next power cycle */
	fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_UPDATE_PENDING);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);

	/* success */
	return TRUE;
}

static void
fu_dell_psb_device_init(FuDellPsbDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.psb");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
}

static void
fu_dell_psb_device_class_init(FuDellPsbDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_dell_psb_device_probe;
	device_class->setup = fu_dell_psb_device_setup;
	device_class->prepare_firmware = fu_dell_psb_device_prepare_firmware;
	device_class->write_firmware = fu_dell_psb_device_write_firmware;
}
