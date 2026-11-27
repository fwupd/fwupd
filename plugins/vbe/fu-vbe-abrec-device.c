/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fcntl.h>

#include "fu-vbe-abrec-device.h"
#include "fu-vbe-abrec-struct.h"

/*
 * The A/B/recovery method keeps three copies of the firmware: A is the one
 * normally booted, B is used to try out an update and recovery is never
 * written. An update is written to B and the state is set so that the
 * firmware tries B a few times. Each try uses up one of the tries. If B gets
 * as far as the OS, this device sees that B was booted and copies it to A,
 * making it the firmware used from then on. If B runs out of tries first, the
 * firmware marks it bad and goes back to A, so the update has failed.
 *
 * The state is kept in a small block with a CRC8, as described in the README
 */

#define FU_VBE_ABREC_NVDATA_SIZE      0x40
#define FU_VBE_ABREC_NVDATA_MIN	      0x10 /* holds the fields up to the flags */
#define FU_VBE_ABREC_NVDATA_HDR_VER   1
#define FU_VBE_ABREC_TRY_COUNT	      3
#define FU_VBE_ABREC_FDT_MAGIC	      0xd00dfeed
#define FU_VBE_ABREC_SLOT_INFO_MIN    0x100
#define FU_VBE_ABREC_FDT_VERSION      17 /* what libfdt in the firmware expects */
#define FU_VBE_ABREC_SLOT_INFO_MAX    0x10000
#define FU_VBE_ABREC_SLOT_SIZE_MAX    (64 * FU_MB)
#define FU_VBE_ABREC_SLOT_INFO_COMPAT "fwupd,vbe-slot-info"

/* flags in the nvdata */
#define FU_VBE_ABREC_FLAG_TRY_COUNT_MASK 0x3
#define FU_VBE_ABREC_FLAG_TRY_B		 (1u << 2)
#define FU_VBE_ABREC_FLAG_RECOVERY	 (1u << 3)
#define FU_VBE_ABREC_FLAG_RESULT_SHIFT	 4
#define FU_VBE_ABREC_FLAG_RESULT_MASK	 (3u << FU_VBE_ABREC_FLAG_RESULT_SHIFT)
#define FU_VBE_ABREC_FLAG_PICK_SHIFT	 6
#define FU_VBE_ABREC_FLAG_PICK_MASK	 (3u << FU_VBE_ABREC_FLAG_PICK_SHIFT)

/* the board has recovery firmware */
#define FU_VBE_ABREC_DEVICE_FLAG_HAS_RECOVERY "has-recovery"

/* recovery was used before the update in B was tried */
#define FU_VBE_ABREC_DEVICE_FLAG_UPDATE_WAITING "update-waiting"

/* the firmware gave up on the update in B */
#define FU_VBE_ABREC_DEVICE_FLAG_UPDATE_FAILED "update-failed"

struct _FuVbeAbrecDevice {
	FuVbeDevice parent_instance;
	guint32 area_start;
	guint32 state_offset;
	guint32 state_size;
	guint32 slot_a;
	guint32 slot_b;
	guint32 slot_recovery;
	guint32 slot_size;
	guint32 slot_info_offset; /* within each slot */
	guint32 slot_info_size;
	FuVbeAbrecSlot booted_slot; /* unknown if the firmware does not say */

	/* state read from the media, with the raw block so other fields are kept */
	guint8 nvdata[FU_VBE_ABREC_NVDATA_SIZE];
	guint32 fw_vernum;
	guint32 flags;
};

G_DEFINE_TYPE(FuVbeAbrecDevice, fu_vbe_abrec_device, FU_TYPE_VBE_DEVICE)

static FuVbeAbrecResult
fu_vbe_abrec_device_get_result(FuVbeAbrecDevice *self)
{
	return (self->flags & FU_VBE_ABREC_FLAG_RESULT_MASK) >> FU_VBE_ABREC_FLAG_RESULT_SHIFT;
}

static void
fu_vbe_abrec_device_set_result(FuVbeAbrecDevice *self, FuVbeAbrecResult result)
{
	self->flags &= ~FU_VBE_ABREC_FLAG_RESULT_MASK;
	self->flags |= result << FU_VBE_ABREC_FLAG_RESULT_SHIFT;
}

static void
fu_vbe_abrec_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuVbeAbrecDevice *self = FU_VBE_ABREC_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "AreaStart", self->area_start);
	fwupd_codec_string_append_hex(str, idt, "StateOffset", self->state_offset);
	fwupd_codec_string_append_hex(str, idt, "StateSize", self->state_size);
	fwupd_codec_string_append_hex(str, idt, "SlotA", self->slot_a);
	fwupd_codec_string_append_hex(str, idt, "SlotB", self->slot_b);
	if (fu_device_has_private_flag(device, FU_VBE_ABREC_DEVICE_FLAG_HAS_RECOVERY))
		fwupd_codec_string_append_hex(str, idt, "SlotRecovery", self->slot_recovery);
	fwupd_codec_string_append_hex(str, idt, "SlotSize", self->slot_size);
	fwupd_codec_string_append_hex(str, idt, "SlotInfoOffset", self->slot_info_offset);
	fwupd_codec_string_append_hex(str, idt, "SlotInfoSize", self->slot_info_size);
	if (self->booted_slot != FU_VBE_ABREC_SLOT_UNKNOWN) {
		fwupd_codec_string_append(str,
					  idt,
					  "BootedSlot",
					  fu_vbe_abrec_slot_to_string(self->booted_slot));
	}
	fwupd_codec_string_append_hex(str, idt, "FwVernum", self->fw_vernum);
	fwupd_codec_string_append_hex(str,
				      idt,
				      "TryCount",
				      self->flags & FU_VBE_ABREC_FLAG_TRY_COUNT_MASK);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "TryB",
				       (self->flags & FU_VBE_ABREC_FLAG_TRY_B) != 0);
	fwupd_codec_string_append(
	    str,
	    idt,
	    "Result",
	    fu_vbe_abrec_result_to_string(fu_vbe_abrec_device_get_result(self)));
	fwupd_codec_string_append_bool(str,
				       idt,
				       "Recovery",
				       (self->flags & FU_VBE_ABREC_FLAG_RECOVERY) != 0);
	fwupd_codec_string_append(
	    str,
	    idt,
	    "Pick",
	    fu_vbe_abrec_slot_to_string((self->flags & FU_VBE_ABREC_FLAG_PICK_MASK) >>
					FU_VBE_ABREC_FLAG_PICK_SHIFT));
}

/*
 * the state and slots must not overlap, or writing one would overwrite another;
 * fwupd never writes recovery, but must not overwrite it either
 */
static gboolean
fu_vbe_abrec_device_check_layout(FuVbeAbrecDevice *self, GError **error)
{
	struct {
		const gchar *name;
		guint64 start;
		guint64 size;
	} regions[] = {
	    {"state", (guint64)self->area_start + self->state_offset, self->state_size},
	    {"slot A", self->slot_a, self->slot_size},
	    {"slot B", self->slot_b, self->slot_size},
	    {"slot recovery", self->slot_recovery, self->slot_size},
	};
	guint count =
	    G_N_ELEMENTS(regions) -
	    (fu_device_has_private_flag(FU_DEVICE(self), FU_VBE_ABREC_DEVICE_FLAG_HAS_RECOVERY)
		 ? 0
		 : 1);

	for (guint i = 0; i < count; i++) {
		/* offsets are worked out in 32 bits, so would wrap */
		if (regions[i].start + regions[i].size > G_MAXUINT32) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "%s ends beyond 4GiB",
				    regions[i].name);
			return FALSE;
		}
		for (guint j = i + 1; j < count; j++) {
			if (regions[i].start < regions[j].start + regions[j].size &&
			    regions[j].start < regions[i].start + regions[i].size) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "%s overlaps %s",
					    regions[i].name,
					    regions[j].name);
				return FALSE;
			}
		}
	}
	return TRUE;
}

/*
 * Get the copy of the firmware which was booted. The firmware may not say,
 * but it records its pick in the state, which is the same thing
 */
static FuVbeAbrecSlot
fu_vbe_abrec_device_get_booted_slot(FuVbeAbrecDevice *self)
{
	if (self->booted_slot != FU_VBE_ABREC_SLOT_UNKNOWN)
		return self->booted_slot;
	return (self->flags & FU_VBE_ABREC_FLAG_PICK_MASK) >> FU_VBE_ABREC_FLAG_PICK_SHIFT;
}

static gboolean
fu_vbe_abrec_device_probe(FuDevice *device, GError **error)
{
	FuVbeAbrecDevice *self = FU_VBE_ABREC_DEVICE(device);
	FuFdtImage *fdt_node = fu_vbe_device_get_fdt_node(FU_VBE_DEVICE(self));
	g_autofree gchar *booted_slot = NULL;
	struct {
		const gchar *name;
		guint32 *value;
	} props[] = {
	    {"area-start", &self->area_start},
	    {"state-offset", &self->state_offset},
	    {"state-size", &self->state_size},
	    {"slot-a-offset", &self->slot_a},
	    {"slot-b-offset", &self->slot_b},
	    {"slot-size", &self->slot_size},
	    {"slot-info-offset", &self->slot_info_offset},
	    {"slot-info-size", &self->slot_info_size},
	};

	/* FuVbeDevice->probe */
	if (!FU_DEVICE_CLASS(fu_vbe_abrec_device_parent_class)->probe(device, error))
		return FALSE;

	for (guint i = 0; i < G_N_ELEMENTS(props); i++) {
		if (!fu_fdt_image_get_attr_u32(fdt_node, props[i].name, props[i].value, error))
			return FALSE;
	}
	if (self->state_size > FU_VBE_ABREC_NVDATA_SIZE ||
	    self->state_size < FU_VBE_ABREC_NVDATA_MIN ||
	    (self->state_size & (self->state_size - 1)) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid state size 0x%x",
			    (guint)self->state_size);
		return FALSE;
	}
	if (self->slot_size == 0 || self->slot_size > FU_VBE_ABREC_SLOT_SIZE_MAX) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid slot size 0x%x",
			    (guint)self->slot_size);
		return FALSE;
	}
	if (self->slot_info_size < FU_VBE_ABREC_SLOT_INFO_MIN ||
	    self->slot_info_size > FU_VBE_ABREC_SLOT_INFO_MAX) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid slot-info size 0x%x",
			    (guint)self->slot_info_size);
		return FALSE;
	}
	if ((guint64)self->slot_info_offset + self->slot_info_size > self->slot_size) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "slot info does not fit in the slot");
		return FALSE;
	}

	/* recovery firmware is optional */
	if (fu_fdt_image_get_attr_u32(fdt_node, "slot-recovery-offset", &self->slot_recovery, NULL))
		fu_device_add_private_flag(device, FU_VBE_ABREC_DEVICE_FLAG_HAS_RECOVERY);
	else
		fu_device_remove_private_flag(device, FU_VBE_ABREC_DEVICE_FLAG_HAS_RECOVERY);
	if (!fu_vbe_abrec_device_check_layout(self, error))
		return FALSE;

	/* this is only provided when the firmware knows which copy it picked */
	self->booted_slot = FU_VBE_ABREC_SLOT_UNKNOWN;
	if (fu_fdt_image_get_attr_str(fdt_node, "booted-slot", &booted_slot, NULL))
		self->booted_slot = fu_vbe_abrec_slot_from_string(booted_slot);

	/* firmware which has never been updated has no version */
	if (fu_device_get_version(device) == NULL)
		fu_device_set_version(device, "0.0.0");

	/* success */
	return TRUE;
}

static gboolean
fu_vbe_abrec_device_read_state(FuVbeAbrecDevice *self, GError **error)
{
	guint8 buf[FU_VBE_ABREC_NVDATA_SIZE] = {0};
	guint hdr_ver;
	guint data_size;

	if (!fu_udev_device_pread(FU_UDEV_DEVICE(self),
				  self->area_start + self->state_offset,
				  buf,
				  self->state_size,
				  error)) {
		g_prefix_error_literal(error, "failed to read the state: ");
		return FALSE;
	}

	/* the firmware also starts with empty state if this is not valid */
	hdr_ver = buf[1] & 0xf;
	data_size = 1u << (buf[1] >> 4);
	if (hdr_ver != FU_VBE_ABREC_NVDATA_HDR_VER || data_size < FU_VBE_ABREC_NVDATA_MIN ||
	    data_size > self->state_size ||
	    fu_crc8(FU_CRC_KIND_B8_STANDARD, buf + 1, data_size - 1) != buf[0]) {
		g_debug("no valid state, so using empty state");
		memset(self->nvdata, '\0', sizeof(self->nvdata)); /* nocheck:blocked */
		self->fw_vernum = 0;
		self->flags = 0;
		return TRUE;
	}
	if (!fu_memcpy_safe(self->nvdata,
			    sizeof(self->nvdata),
			    0x0,
			    buf,
			    sizeof(buf),
			    0x0,
			    sizeof(buf),
			    error))
		return FALSE;
	self->fw_vernum = fu_memread_uint32(buf + 4, G_LITTLE_ENDIAN);
	self->flags = fu_memread_uint32(buf + 8, G_LITTLE_ENDIAN);

	/* success */
	return TRUE;
}

/*
 * Read the version of the firmware in a slot from its slot info, which is a
 * small devicetree after the firmware. A slot which has never been written,
 * or was written without slot info, gives no version. Any other devicetree
 * found there, such as part of older firmware, lacks the compatible string
 */
static gboolean
fu_vbe_abrec_device_read_slot_version(FuVbeAbrecDevice *self,
				      guint32 slot,
				      gchar **versionp,
				      GError **error)
{
	g_autofree guint8 *buf = g_malloc0(self->slot_info_size);
	g_autoptr(FuFirmware) fdt = fu_fdt_firmware_new();
	g_autoptr(FuFirmware) root = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error_local = NULL;
	g_auto(GStrv) compat = NULL;

	*versionp = NULL;
	if (!fu_udev_device_pread(FU_UDEV_DEVICE(self),
				  slot + self->slot_info_offset,
				  buf,
				  self->slot_info_size,
				  error)) {
		g_prefix_error(error,
			       "failed to read the slot info at 0x%x: ",
			       (guint)(slot + self->slot_info_offset));
		return FALSE;
	}
	if (fu_memread_uint32(buf, G_BIG_ENDIAN) != FU_VBE_ABREC_FDT_MAGIC) {
		g_debug("no slot info at 0x%x", (guint)(slot + self->slot_info_offset));
		return TRUE;
	}
	blob = g_bytes_new(buf, self->slot_info_size);
	if (!fu_firmware_parse_bytes(fdt, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error_local)) {
		g_debug("ignoring invalid slot info: %s", error_local->message);
		return TRUE;
	}
	root = fu_firmware_get_image_by_id(fdt, NULL, NULL);
	if (root == NULL)
		return TRUE;
	if (!fu_fdt_image_get_attr_strlist(FU_FDT_IMAGE(root), "compatible", &compat, NULL) ||
	    !g_strv_contains((const gchar *const *)compat, FU_VBE_ABREC_SLOT_INFO_COMPAT)) {
		g_debug("ignoring devicetree which is not slot info at 0x%x",
			(guint)(slot + self->slot_info_offset));
		return TRUE;
	}
	fu_fdt_image_get_attr_str(FU_FDT_IMAGE(root), "version", versionp, NULL);
	return TRUE;
}

/* use the version of A, which is up to date even if the firmware did not provide one */
static gboolean
fu_vbe_abrec_device_read_version(FuVbeAbrecDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;

	if (!fu_vbe_abrec_device_read_slot_version(self, self->slot_a, &version, error))
		return FALSE;
	if (version != NULL)
		fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

/* drop any cached copy of a region, so that reading it goes to the storage */
static void
fu_vbe_abrec_device_drop_cache(FuVbeAbrecDevice *self, goffset offset, gsize size)
{
	FuIOChannel *io_channel = fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self));

	if (io_channel != NULL)
		(void)posix_fadvise(fu_io_channel_unix_get_fd(io_channel),
				    offset,
				    size,
				    POSIX_FADV_DONTNEED);
}

/*
 * check that data reached the storage intact, reading it back from the storage
 * itself rather than from the cache
 */
static gboolean
fu_vbe_abrec_device_check_written(FuVbeAbrecDevice *self,
				  guint32 offset,
				  const guint8 *data,
				  gsize size,
				  GError **error)
{
	const gsize chunksz = 1 * FU_MB;
	g_autofree guint8 *buf = g_malloc(MIN(chunksz, size));

	fu_vbe_abrec_device_drop_cache(self, offset, size);
	for (gsize pos = 0; pos < size; pos += chunksz) {
		gsize len = MIN(chunksz, size - pos);

		if (!fu_udev_device_pread(FU_UDEV_DEVICE(self), offset + pos, buf, len, error)) {
			g_prefix_error(error,
				       "failed to read back the write at 0x%x: ",
				       (guint)(offset + pos));
			return FALSE;
		}
		if (!fu_memcmp_safe(buf, len, 0x0, data + pos, len, 0x0, len, error)) {
			g_prefix_error(error,
				       "write at 0x%x is not intact: ",
				       (guint)(offset + pos));
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * Write the slot info for firmware in a slot: its version, size and checksums,
 * so that it can be checked and its version reported from the slot alone
 */
static gboolean
fu_vbe_abrec_device_write_slot_info(FuVbeAbrecDevice *self,
				    guint32 slot,
				    const gchar *version,
				    GBytes *fw,
				    GError **error)
{
	gsize digestsz = 32;
	guint8 digest[32] = {0};
	g_autofree guint8 *buf = g_malloc0(self->slot_info_size);
	g_autoptr(FuFirmware) fdt = fu_fdt_firmware_new();
	g_autoptr(FuFirmware) root = fu_fdt_image_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) sha256 = NULL;
	g_autoptr(GChecksum) csum = g_checksum_new(G_CHECKSUM_SHA256);

	g_checksum_update(csum, g_bytes_get_data(fw, NULL), g_bytes_get_size(fw));
	g_checksum_get_digest(csum, digest, &digestsz);
	sha256 = g_bytes_new(digest, digestsz);
	fu_fdt_image_set_attr_str(FU_FDT_IMAGE(root), "compatible", FU_VBE_ABREC_SLOT_INFO_COMPAT);
	if (version != NULL)
		fu_fdt_image_set_attr_str(FU_FDT_IMAGE(root), "version", version);
	fu_fdt_image_set_attr_uint32(FU_FDT_IMAGE(root), "size", g_bytes_get_size(fw));
	fu_fdt_image_set_attr_uint32(
	    FU_FDT_IMAGE(root),
	    "crc32",
	    fu_crc32(FU_CRC_KIND_B32_STANDARD, g_bytes_get_data(fw, NULL), g_bytes_get_size(fw)));
	fu_fdt_image_set_attr(FU_FDT_IMAGE(root), "sha256", sha256);
	fu_firmware_set_version_raw(fdt, FU_VBE_ABREC_FDT_VERSION);
	if (!fu_firmware_add_image(fdt, root, error))
		return FALSE;
	blob = fu_firmware_write(fdt, error);
	if (blob == NULL)
		return FALSE;

	/* fill the whole area, so nothing is left of any earlier slot info */
	if (!fu_memcpy_safe(buf,
			    self->slot_info_size,
			    0x0,
			    g_bytes_get_data(blob, NULL),
			    g_bytes_get_size(blob),
			    0x0,
			    g_bytes_get_size(blob),
			    error)) {
		g_prefix_error_literal(error, "slot info is too large: ");
		return FALSE;
	}
	if (!fu_udev_device_pwrite(FU_UDEV_DEVICE(self),
				   slot + self->slot_info_offset,
				   buf,
				   self->slot_info_size,
				   error)) {
		g_prefix_error(error,
			       "failed to write the slot info at 0x%x: ",
			       (guint)(slot + self->slot_info_offset));
		return FALSE;
	}
	return fu_vbe_abrec_device_check_written(self,
						 slot + self->slot_info_offset,
						 buf,
						 self->slot_info_size,
						 error);
}

static gboolean
fu_vbe_abrec_device_write_state(FuVbeAbrecDevice *self, GError **error)
{
	guint8 buf[FU_VBE_ABREC_NVDATA_SIZE] = {0};

	/* keep any fields this device does not know about */
	if (!fu_memcpy_safe(buf,
			    sizeof(buf),
			    0x0,
			    self->nvdata,
			    sizeof(self->nvdata),
			    0x0,
			    sizeof(self->nvdata),
			    error))
		return FALSE;
	buf[1] = FU_VBE_ABREC_NVDATA_HDR_VER | g_bit_nth_lsf(self->state_size, -1) << 4;
	fu_memwrite_uint32(buf + 4, self->fw_vernum, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(buf + 8, self->flags, G_LITTLE_ENDIAN);
	buf[0] = fu_crc8(FU_CRC_KIND_B8_STANDARD, buf + 1, self->state_size - 1);
	/*
	 * the storage is opened with O_SYNC, so the state is not written until
	 * everything before it has reached the storage, and the firmware never
	 * sees state which refers to firmware that is not there yet
	 */
	if (!fu_udev_device_pwrite(FU_UDEV_DEVICE(self),
				   self->area_start + self->state_offset,
				   buf,
				   self->state_size,
				   error)) {
		g_prefix_error_literal(error, "failed to write the state: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* copy the firmware from one slot to another */
static gboolean
fu_vbe_abrec_device_copy_slot(FuVbeAbrecDevice *self, guint32 from, guint32 to, GError **error)
{
	const gsize chunksz = 1 * FU_MB;
	g_autofree guint8 *buf = g_malloc(chunksz);

	for (gsize offset = 0; offset < self->slot_size; offset += chunksz) {
		gsize len = MIN(chunksz, self->slot_size - offset);

		if (!fu_udev_device_pread(FU_UDEV_DEVICE(self), from + offset, buf, len, error) ||
		    !fu_udev_device_pwrite(FU_UDEV_DEVICE(self), to + offset, buf, len, error)) {
			g_prefix_error(error,
				       "failed to copy 0x%x to 0x%x: ",
				       (guint)(from + offset),
				       (guint)(to + offset));
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * check that a copy reached the storage intact, reading it back from the
 * storage itself rather than from the cache
 */
static gboolean
fu_vbe_abrec_device_verify_slot(FuVbeAbrecDevice *self, guint32 from, guint32 to, GError **error)
{
	const gsize chunksz = 1 * FU_MB;
	g_autofree guint8 *buf_from = g_malloc(chunksz);
	g_autofree guint8 *buf_to = g_malloc(chunksz);

	fu_vbe_abrec_device_drop_cache(self, to, self->slot_size);
	for (gsize offset = 0; offset < self->slot_size; offset += chunksz) {
		gsize len = MIN(chunksz, self->slot_size - offset);

		if (!fu_udev_device_pread(FU_UDEV_DEVICE(self),
					  from + offset,
					  buf_from,
					  len,
					  error) ||
		    !fu_udev_device_pread(FU_UDEV_DEVICE(self), to + offset, buf_to, len, error)) {
			g_prefix_error(error,
				       "failed to read back the copy at 0x%x: ",
				       (guint)(to + offset));
			return FALSE;
		}
		if (!fu_memcmp_safe(buf_to, len, 0x0, buf_from, len, 0x0, len, error)) {
			g_prefix_error(error, "copy at 0x%x is not intact: ", (guint)(to + offset));
			return FALSE;
		}
	}
	return TRUE;
}

/* B works, so copy it to A, which is the firmware normally used */
static gboolean
fu_vbe_abrec_device_commit(FuVbeAbrecDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;

	if (!fu_vbe_abrec_device_copy_slot(self, self->slot_b, self->slot_a, error))
		return FALSE;
	if (!fu_vbe_abrec_device_verify_slot(self, self->slot_b, self->slot_a, error))
		return FALSE;

	/* the slot info came with the copy, so A now has the new version */
	if (!fu_vbe_abrec_device_read_slot_version(self, self->slot_a, &version, error))
		return FALSE;
	if (version != NULL)
		fu_device_set_version(FU_DEVICE(self), version);
	self->flags &= ~(FU_VBE_ABREC_FLAG_TRY_B | FU_VBE_ABREC_FLAG_TRY_COUNT_MASK);
	fu_vbe_abrec_device_set_result(self, FU_VBE_ABREC_RESULT_OK);
	return fu_vbe_abrec_device_write_state(self, error);
}

/*
 * Get why the last update failed, if it did. This is worked out from the state
 * and private flags rather than kept in the update error, since FuDevice
 * replaces that with the inhibit reasons whenever the problems change
 */
static const gchar *
fu_vbe_abrec_device_get_failure(FuVbeAbrecDevice *self)
{
	FuVbeAbrecSlot booted_slot = fu_vbe_abrec_device_get_booted_slot(self);

	/* B ran out of tries or could not be loaded */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_VBE_ABREC_DEVICE_FLAG_UPDATE_FAILED)) {
		if (booted_slot == FU_VBE_ABREC_SLOT_RECOVERY)
			return "new firmware failed to boot and A could not be loaded, so "
			       "recovery is in use";
		return "new firmware failed to boot, so A is still in use";
	}

	/* B reached the OS but is still being tried, so the copy to A failed */
	if ((self->flags & FU_VBE_ABREC_FLAG_TRY_B) != 0 &&
	    fu_vbe_abrec_device_get_result(self) == FU_VBE_ABREC_RESULT_TRYING &&
	    booted_slot == FU_VBE_ABREC_SLOT_B)
		return "new firmware booted but could not be copied to A";

	return NULL;
}

/*
 * Finish off an update once the firmware in B has booted, or note that it
 * failed
 */
static gboolean
fu_vbe_abrec_device_check_update(FuVbeAbrecDevice *self, GError **error)
{
	FuVbeAbrecResult result = fu_vbe_abrec_device_get_result(self);
	FuVbeAbrecSlot booted_slot = fu_vbe_abrec_device_get_booted_slot(self);
	guint32 flags = self->flags;
	g_autoptr(GError) error_local = NULL;

	/* these describe this boot only */
	fu_device_remove_private_flag(FU_DEVICE(self), FU_VBE_ABREC_DEVICE_FLAG_UPDATE_WAITING);
	fu_device_remove_private_flag(FU_DEVICE(self), FU_VBE_ABREC_DEVICE_FLAG_UPDATE_FAILED);

	/*
	 * B ran out of tries or could not be loaded, so the firmware went back
	 * to A, or to recovery if A could not be loaded either
	 */
	if (result == FU_VBE_ABREC_RESULT_BAD) {
		fu_device_add_private_flag(FU_DEVICE(self), FU_VBE_ABREC_DEVICE_FLAG_UPDATE_FAILED);
		g_info("%s", fu_vbe_abrec_device_get_failure(self));

		/* if this cannot be written, the next boot reports the failure again */
		fu_vbe_abrec_device_set_result(self, FU_VBE_ABREC_RESULT_UNKNOWN);
		if (!fu_vbe_abrec_device_write_state(self, &error_local))
			g_info("cannot clear the result: %s", error_local->message);
		return TRUE;
	}

	/* recovery was requested, so B is still waiting to be tried */
	if ((self->flags & FU_VBE_ABREC_FLAG_TRY_B) != 0 &&
	    booted_slot == FU_VBE_ABREC_SLOT_RECOVERY) {
		fu_device_add_private_flag(FU_DEVICE(self),
					   FU_VBE_ABREC_DEVICE_FLAG_UPDATE_WAITING);
		return TRUE;
	}

	/* nothing to do unless B was tried and reached the OS */
	if ((self->flags & FU_VBE_ABREC_FLAG_TRY_B) == 0 || result != FU_VBE_ABREC_RESULT_TRYING ||
	    booted_slot != FU_VBE_ABREC_SLOT_B)
		return TRUE;

	/*
	 * If this fails, the state still asks for B, so each boot tries B and
	 * this copies it again, until B runs out of tries. Then the firmware
	 * goes back to A, which may be broken by now, and from there to
	 * recovery. Retrying cannot help if the storage cannot be written, so
	 * report the failure rather than hiding the device
	 */
	g_info("new firmware booted, copying it to A");
	if (!fu_vbe_abrec_device_commit(self, &error_local)) {
		self->flags = flags; /* the state was not written */
		g_info("%s: %s", fu_vbe_abrec_device_get_failure(self), error_local->message);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_vbe_abrec_device_setup(FuDevice *device, GError **error)
{
	FuVbeAbrecDevice *self = FU_VBE_ABREC_DEVICE(device);

	if (!fu_vbe_abrec_device_read_version(self, error))
		return FALSE;
	if (!fu_vbe_abrec_device_read_state(self, error))
		return FALSE;
	if (!fu_vbe_abrec_device_check_update(self, error)) {
		g_prefix_error_literal(error, "cannot complete update: ");
		return FALSE;
	}

	/* while B is waiting to be tried, another update would overwrite it */
	if ((self->flags & FU_VBE_ABREC_FLAG_TRY_B) != 0)
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_UPDATE_PENDING);
	else
		fu_device_remove_problem(device, FWUPD_DEVICE_PROBLEM_UPDATE_PENDING);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_vbe_abrec_device_prepare_firmware(FuDevice *device,
				     FuInputStream *stream,
				     FuProgress *progress,
				     FuFirmwareParseFlags flags,
				     GError **error)
{
	FuVbeAbrecDevice *self = FU_VBE_ABREC_DEVICE(device);
	gsize streamsz = 0;
	guint32 magic = 0;
	g_autoptr(FuFirmware) firmware = fu_firmware_new();

	/* the image for a slot starts with a FIT */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return NULL;
	if (streamsz > self->slot_info_offset) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "image size 0x%x does not fit below the slot info at 0x%x",
			    (guint)streamsz,
			    (guint)self->slot_info_offset);
		return NULL;
	}
	if (!fu_input_stream_read_u32(stream, 0x0, &magic, G_BIG_ENDIAN, error))
		return NULL;
	if (magic != FU_VBE_ABREC_FDT_MAGIC) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "image does not start with a FIT, magic 0x%08x",
			    magic);
		return NULL;
	}
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	/* success */
	return g_steal_pointer(&firmware);
}

/* the layout comes from probe(), so an unprobed device has nowhere to write */
static gboolean
fu_vbe_abrec_device_check_probed(FuVbeAbrecDevice *self, GError **error)
{
	if (self->slot_size == 0 || self->slot_info_size == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "the layout of the firmware is not known");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_vbe_abrec_device_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuVbeAbrecDevice *self = FU_VBE_ABREC_DEVICE(device);
	const gchar *version = fu_firmware_get_version(firmware);
	gsize streamsz = 0;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(GBytes) fw = NULL;

	if (!fu_vbe_abrec_device_check_probed(self, error))
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "state");

	/* write the new firmware to B */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream, self->slot_b, 0x0, 1 * FU_MB, error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(fu_progress_get_child(progress), G_STRLOC);
	fu_progress_set_steps(fu_progress_get_child(progress), fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_udev_device_pwrite(FU_UDEV_DEVICE(self),
					   fu_chunk_get_address(chk),
					   fu_chunk_get_data(chk),
					   fu_chunk_get_data_sz(chk),
					   error)) {
			g_prefix_error(error,
				       "failed to write B at 0x%x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_step_done(fu_progress_get_child(progress));
	}

	fu_progress_step_done(progress);

	/* describe the new firmware, so its version goes with it when B is copied to A */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	fw = fu_input_stream_read_bytes(stream, 0x0, streamsz, NULL, error);
	if (fw == NULL)
		return FALSE;
	if (!fu_vbe_abrec_device_check_written(self,
					       self->slot_b,
					       g_bytes_get_data(fw, NULL),
					       g_bytes_get_size(fw),
					       error))
		return FALSE;
	if (!fu_vbe_abrec_device_write_slot_info(self, self->slot_b, version, fw, error))
		return FALSE;

	/* ask the firmware to try B */
	self->flags &= ~(FU_VBE_ABREC_FLAG_TRY_COUNT_MASK | FU_VBE_ABREC_FLAG_RECOVERY);
	self->flags |= FU_VBE_ABREC_FLAG_TRY_B | FU_VBE_ABREC_TRY_COUNT;
	fu_vbe_abrec_device_set_result(self, FU_VBE_ABREC_RESULT_UNKNOWN);
	if (!fu_vbe_abrec_device_write_state(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static GBytes *
fu_vbe_abrec_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuVbeAbrecDevice *self = FU_VBE_ABREC_DEVICE(device);
	g_autofree guint8 *buf = NULL;

	if (!fu_vbe_abrec_device_check_probed(self, error))
		return NULL;
	buf = g_malloc(self->slot_size);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	if (!fu_udev_device_pread(FU_UDEV_DEVICE(self),
				  self->slot_a,
				  buf,
				  self->slot_size,
				  error)) {
		g_prefix_error_literal(error, "failed to read A: ");
		return NULL;
	}
	return g_bytes_new_take(g_steal_pointer(&buf), self->slot_size);
}

static void
fu_vbe_abrec_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_vbe_abrec_device_init(FuVbeAbrecDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "abrec");
	fu_device_set_summary(FU_DEVICE(self), "A/B/recovery firmware");
	fu_device_set_vendor(FU_DEVICE(self), "U-Boot");
	fu_device_build_vendor_id(FU_DEVICE(self), "VBE", "U-Boot");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	self->booted_slot = FU_VBE_ABREC_SLOT_UNKNOWN;
}

/* report the outcome of an update after the reboot which tried it */
static gboolean
fu_vbe_abrec_device_get_results(FuDevice *device, GError **error)
{
	FuVbeAbrecDevice *self = FU_VBE_ABREC_DEVICE(device);
	const gchar *failure;

	/* keep the update pending, since it has not been tried yet */
	if (fu_device_has_private_flag(device, FU_VBE_ABREC_DEVICE_FLAG_UPDATE_WAITING)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "recovery was requested, so the update has not been tried");
		return FALSE;
	}
	failure = fu_vbe_abrec_device_get_failure(self);
	if (failure != NULL) {
		fu_device_set_update_state(device, FWUPD_UPDATE_STATE_FAILED);
		fu_device_set_update_error(device, failure);
	}
	return TRUE;
}

static void
fu_vbe_abrec_device_class_init(FuVbeAbrecDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	fu_device_register_private_flag(device_class, FU_VBE_ABREC_DEVICE_FLAG_HAS_RECOVERY);
	fu_device_register_private_flag(device_class, FU_VBE_ABREC_DEVICE_FLAG_UPDATE_WAITING);
	fu_device_register_private_flag(device_class, FU_VBE_ABREC_DEVICE_FLAG_UPDATE_FAILED);
	device_class->to_string = fu_vbe_abrec_device_to_string;
	device_class->probe = fu_vbe_abrec_device_probe;
	device_class->setup = fu_vbe_abrec_device_setup;
	device_class->get_results = fu_vbe_abrec_device_get_results;
	device_class->set_progress = fu_vbe_abrec_device_set_progress;
	device_class->prepare_firmware = fu_vbe_abrec_device_prepare_firmware;
	device_class->write_firmware = fu_vbe_abrec_device_write_firmware;
	device_class->dump_firmware = fu_vbe_abrec_device_dump_firmware;
}
