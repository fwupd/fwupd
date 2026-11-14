/*
 * Copyright 2026 Arno Dubois <a.dubois@criteo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-dell-psb-device.h"
#include "fu-dell-psb-struct.h"

struct _FuDellPsbDevice {
	FuBlockDevice parent_instance;
	gchar *banner; /* the whole "@(#)PCI-<version> <date>" string */
};

G_DEFINE_TYPE(FuDellPsbDevice, fu_dell_psb_device, FU_TYPE_BLOCK_DEVICE)

#define FU_DELL_PSB_VERSION_LENGTH 0x100     /* enough for the banner at 0x44 */
#define FU_DELL_PSB_CHUNK_SIZE	   0x8000    /* 32 KiB */
#define FU_DELL_PSB_BUFFER_ID	   0x00	     /* flat window onto the firmware image */
#define FU_DELL_PSB_MAX_IMAGE_SIZE 0x1000000 /* 16 MiB: the offset field is 24 bits */

static gboolean
fu_dell_psb_device_probe(FuDevice *device, GError **error)
{
	g_autofree gchar *instance_id = NULL;
	g_autofree gchar *guid = NULL;
	g_autoptr(FuDevice) device_scsi = NULL;
	g_autoptr(FuDevice) pci = NULL;

	/* the udev backend has no scsi physical-ID scheme, so build one from the
	 * scsi_device parent, as FuScsiDevice does */
	device_scsi = fu_device_get_backend_parent_with_subsystem(device, "scsi:scsi_device", NULL);
	if (device_scsi != NULL) {
		g_autofree gchar *devpath = fu_udev_device_get_devpath(FU_UDEV_DEVICE(device_scsi));
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

	fu_device_incorporate(
	    device,
	    pci,
	    FU_DEVICE_INCORPORATE_FLAG_VENDOR | FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS |
		FU_DEVICE_INCORPORATE_FLAG_VID | FU_DEVICE_INCORPORATE_FLAG_PID |
		FU_DEVICE_INCORPORATE_FLAG_INSTANCE_KEYS | FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);
	if (!fu_device_build_instance_id(device, error, "PCI", "VEN", "DEV", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "PCI", "VEN", "DEV", "SUBSYS", NULL))
		return FALSE;

	/* the quirk database, not the plugin, decides which PCI function is the
	 * switch; anything else on scsi_generic is not ours */
	instance_id = g_strdup_printf("PCI\\VEN_%s&DEV_%s",
				      fu_device_get_instance_str(pci, "VEN"),
				      fu_device_get_instance_str(pci, "DEV"));
	guid = fwupd_guid_hash_string(instance_id);
	if (fu_context_lookup_quirk_by_id(fu_device_get_context(device), guid, "DellPsbDevice") ==
	    NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not a Dell PSB switch");
		return FALSE;
	}

	/* set after incorporate, which may have copied a donor name */
	fu_device_set_name(device, "PCIe Switch Firmware");
	fu_device_set_summary(device, "Dell PowerEdge PSB PCIe switch");

	/* success */
	return TRUE;
}

/* the image starts with an "@(#)PCI-<version> <date>" banner, e.g.
 * "@(#)PCI-4.160.3.0 09/27/2024" at offset 0x44 */
static gchar *
fu_dell_psb_device_parse_banner(const guint8 *buf, gsize bufsz, GError **error)
{
	const gchar *prefix = "@(#)PCI-";
	const gchar *banner_start = NULL;
	const gchar *start = NULL;
	const gchar *end = NULL;
	const gchar *banner_end = NULL;
	g_autofree gchar *banner = NULL;
	g_autofree gchar *version = NULL;

	/* the banner is NUL terminated, so bound the search by the buffer */
	for (gsize i = 0; i + strlen(prefix) < bufsz; i++) {
		if (memcmp(buf + i, prefix, strlen(prefix)) == 0) {
			banner_start = (const gchar *)buf + i;
			start = banner_start + strlen(prefix);
			break;
		}
	}
	if (start == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no @(#)PCI- version banner found in the firmware image");
		return NULL;
	}

	/* the version runs up to the first space or NUL */
	end = start;
	while ((const guint8 *)end < buf + bufsz && *end != '\0' && *end != ' ')
		end++;
	if (end == start) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "empty version in the @(#)PCI- banner");
		return NULL;
	}

	/* keep the whole banner: the trailing date is useful when debugging */
	banner_end = end;
	while ((const guint8 *)banner_end < buf + bufsz && *banner_end != '\0')
		banner_end++;
	banner = g_strndup(banner_start, (gsize)(banner_end - banner_start));

	version = g_strndup(start, (gsize)(end - start));
	if (fu_version_guess_format(version) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid version %s in the @(#)PCI- banner",
			    version);
		return NULL;
	}
	return g_steal_pointer(&banner);
}

static gboolean
fu_dell_psb_device_setup(FuDevice *device, GError **error)
{
	FuDellPsbDevice *self = FU_DELL_PSB_DEVICE(device);
	gsize bufsz_out = 0;
	guint8 buf[FU_DELL_PSB_VERSION_LENGTH] = {0};
	g_autoptr(FuStructDellPsbReadBuffer) st_cdb = NULL;
	g_autofree gchar *banner = NULL;
	g_autofree gchar *version = NULL;

	/* buffer ID 0x00 is a flat window onto the firmware image starting at
	 * address 0; the version is the "@(#)PCI-<version> <date>" banner near
	 * the start of it */
	/* nocheck:magic */
	st_cdb = fu_struct_dell_psb_read_buffer_new();
	fu_struct_dell_psb_read_buffer_set_buffer_id(st_cdb, FU_DELL_PSB_BUFFER_ID);
	fu_struct_dell_psb_read_buffer_set_offset(st_cdb, 0x0);
	fu_struct_dell_psb_read_buffer_set_length(st_cdb, FU_DELL_PSB_VERSION_LENGTH);
	fu_struct_dell_psb_read_buffer_set_control(st_cdb, 0x00);

	if (!fu_block_device_sg_io_cmd_read_full(FU_BLOCK_DEVICE(device),
						 st_cdb->buf->data,
						 st_cdb->buf->len,
						 buf,
						 sizeof(buf),
						 &bufsz_out,
						 error)) {
		g_prefix_error_literal(error, "failed to read PSB firmware version: ");
		return FALSE;
	}

	/* a short read leaves the banner incomplete */
	if (bufsz_out < FU_DELL_PSB_VERSION_LENGTH) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "short read: got %" G_GSIZE_FORMAT " bytes, expected %u",
			    bufsz_out,
			    (guint)FU_DELL_PSB_VERSION_LENGTH);
		return FALSE;
	}

	banner = fu_dell_psb_device_parse_banner(buf, bufsz_out, error);
	if (banner == NULL)
		return FALSE;
	g_free(self->banner);
	self->banner = g_steal_pointer(&banner);

	/* the version is the banner up to the first space */
	version = g_strdup(self->banner + strlen("@(#)PCI-"));
	for (gchar *p = version; *p != '\0'; p++) {
		if (*p == ' ') {
			*p = '\0';
			break;
		}
	}
	fu_device_set_version(device, version);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_dell_psb_device_prepare_firmware(FuDevice *device,
				    FuInputStream *stream,
				    FuProgress *progress,
				    FuFirmwareParseFlags flags,
				    GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_firmware_new();

	/* the payload is written verbatim: no header stripping, no trailer */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	/* the WRITE BUFFER(10) offset field is 24 bits, so images larger than
	 * 16 MiB silently wrap around the device address space */
	if (fu_firmware_get_size(firmware) > FU_DELL_PSB_MAX_IMAGE_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware image too large: got %" G_GSIZE_FORMAT " bytes, "
			    "maximum is %u bytes (16 MiB)",
			    fu_firmware_get_size(firmware),
			    (guint)FU_DELL_PSB_MAX_IMAGE_SIZE);
		return NULL;
	}

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
	g_autoptr(FuInputStream) stream = NULL;
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
		g_autoptr(FuStructDellPsbWriteBuffer) st_cdb = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		st_cdb = fu_struct_dell_psb_write_buffer_new();
		fu_struct_dell_psb_write_buffer_set_buffer_id(st_cdb, 0x00);
		fu_struct_dell_psb_write_buffer_set_offset(st_cdb, offset);
		fu_struct_dell_psb_write_buffer_set_length(st_cdb, fu_chunk_get_data_sz(chk));
		fu_struct_dell_psb_write_buffer_set_control(st_cdb, 0x00);

		if (!fu_block_device_sg_io_cmd_write(FU_BLOCK_DEVICE(device),
						     st_cdb->buf->data,
						     st_cdb->buf->len,
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

	/* success */
	return TRUE;
}

static void
fu_dell_psb_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuDellPsbDevice *self = FU_DELL_PSB_DEVICE(device);
	fwupd_codec_string_append(str, idt, "Banner", self->banner);
	fwupd_codec_string_append_hex(str, idt, "ChunkSize", FU_DELL_PSB_CHUNK_SIZE);
	fwupd_codec_string_append_hex(str, idt, "MaxImageSize", FU_DELL_PSB_MAX_IMAGE_SIZE);
}

static void
fu_dell_psb_device_finalize(GObject *object)
{
	FuDellPsbDevice *self = FU_DELL_PSB_DEVICE(object);
	g_free(self->banner);
	G_OBJECT_CLASS(fu_dell_psb_device_parent_class)->finalize(object);
}

static void
fu_dell_psb_device_init(FuDellPsbDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.psb");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	/* the switch only activates the new image on the next power cycle, so the
	 * user knows before the update starts */
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
}

static void
fu_dell_psb_device_class_init(FuDellPsbDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_dell_psb_device_finalize;
	device_class->to_string = fu_dell_psb_device_to_string;
	device_class->probe = fu_dell_psb_device_probe;
	device_class->setup = fu_dell_psb_device_setup;
	device_class->prepare_firmware = fu_dell_psb_device_prepare_firmware;
	device_class->write_firmware = fu_dell_psb_device_write_firmware;
}
