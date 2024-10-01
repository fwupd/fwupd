/*
 * Copyright 2018 Evan Lojewski
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <glib/gstdio.h>
#include <string.h>
#ifdef HAVE_ETHTOOL_H
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#endif
#ifdef HAVE_SOCKET_H
#include <sys/socket.h>
#endif

#include "fu-bcm57xx-common.h"
#include "fu-bcm57xx-device.h"
#include "fu-bcm57xx-dict-image.h"
#include "fu-bcm57xx-firmware.h"
#include "fu-bcm57xx-recovery-device.h"

#define FU_BCM57XX_BLOCK_SZ 0x4000 /* 16kb */

struct _FuBcm57xxDevice {
	FuPciDevice parent_instance;
	gchar *ethtool_iface;
	int ethtool_fd;
};

G_DEFINE_TYPE(FuBcm57xxDevice, fu_bcm57xx_device, FU_TYPE_PCI_DEVICE)

enum { PROP_0, PROP_IFACE, PROP_LAST };

static void
fu_bcm57xx_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE(device);
	fwupd_codec_string_append(str, idt, "EthtoolIface", self->ethtool_iface);
}

static gboolean
fu_bcm57xx_device_probe(FuDevice *device, GError **error)
{
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "pci", error);
}

static gboolean
fu_bcm57xx_device_nvram_write(FuBcm57xxDevice *self,
			      guint32 address,
			      const guint8 *buf,
			      gsize bufsz,
			      GError **error)
{
#ifdef HAVE_ETHTOOL_H
	gsize eepromsz;
	struct ifreq ifr = {0};
	g_autofree struct ethtool_eeprom *eeprom = NULL;

	/* failed to load tg3 */
	if (self->ethtool_iface == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Not supported as ethtool interface disabled");
		return FALSE;
	}

	/* sanity check */
	if (address + bufsz > fu_device_get_firmware_size_max(FU_DEVICE(self))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "tried to read outside of EEPROM size [0x%x]",
			    (guint)fu_device_get_firmware_size_max(FU_DEVICE(self)));
		return FALSE;
	}

	/* write EEPROM (NVRAM) data */
	eepromsz = sizeof(struct ethtool_eeprom) + bufsz;
	eeprom = (struct ethtool_eeprom *)g_malloc0(eepromsz);
	eeprom->cmd = ETHTOOL_SEEPROM;
	eeprom->magic = BCM_NVRAM_MAGIC;
	eeprom->len = bufsz;
	eeprom->offset = address;
	memcpy(eeprom->data, buf, eeprom->len); /* nocheck:blocked */
	strncpy(ifr.ifr_name, self->ethtool_iface, IFNAMSIZ - 1);
	ifr.ifr_data = (char *)eeprom;
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  SIOCETHTOOL,
				  (guint8 *)&ifr,
				  sizeof(ifr),
				  NULL,
				  500, /* ms */
				  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
				  error)) {
		g_prefix_error(error, "cannot write eeprom: ");
		return FALSE;
	}

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as <linux/ethtool.h> not found");
	return FALSE;
#endif
}

static gboolean
fu_bcm57xx_device_nvram_read(FuBcm57xxDevice *self,
			     guint32 address,
			     guint8 *buf,
			     gsize bufsz,
			     GError **error)
{
#ifdef HAVE_ETHTOOL_H
	gsize eepromsz;
	struct ifreq ifr = {0};
	g_autofree struct ethtool_eeprom *eeprom = NULL;

	/* failed to load tg3 */
	if (self->ethtool_iface == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Not supported as ethtool interface disabled");
		return FALSE;
	}

	/* sanity check */
	if (address + bufsz > fu_device_get_firmware_size_max(FU_DEVICE(self))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "tried to read outside of EEPROM size [0x%x]",
			    (guint)fu_device_get_firmware_size_max(FU_DEVICE(self)));
		return FALSE;
	}

	/* read EEPROM (NVRAM) data */
	eepromsz = sizeof(struct ethtool_eeprom) + bufsz;
	eeprom = (struct ethtool_eeprom *)g_malloc0(eepromsz);
	eeprom->cmd = ETHTOOL_GEEPROM;
	eeprom->len = bufsz;
	eeprom->offset = address;
	strncpy(ifr.ifr_name, self->ethtool_iface, IFNAMSIZ - 1);
	ifr.ifr_data = (char *)eeprom;
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  SIOCETHTOOL,
				  (guint8 *)&ifr,
				  sizeof(ifr),
				  NULL,
				  500, /* ms */
				  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
				  error)) {
		g_prefix_error(error, "cannot read eeprom: ");
		return FALSE;
	}

	/* copy back data */
	if (!fu_memcpy_safe(buf,
			    bufsz,
			    0x0, /* dst */
			    (guint8 *)eeprom,
			    eepromsz, /* src */
			    G_STRUCT_OFFSET(struct ethtool_eeprom, data),
			    bufsz,
			    error))
		return FALSE;

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as <linux/ethtool.h> not found");
	return FALSE;
#endif
}

static gboolean
fu_bcm57xx_device_nvram_check(FuBcm57xxDevice *self, GError **error)
{
#ifdef HAVE_ETHTOOL_H
	struct ethtool_drvinfo drvinfo = {0};
	struct ifreq ifr = {0};

	/* failed to load tg3 */
	if (self->ethtool_iface == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Not supported as ethtool interface disabled");
		return FALSE;
	}

	/* get driver info */
	drvinfo.cmd = ETHTOOL_GDRVINFO;
	strncpy(ifr.ifr_name, self->ethtool_iface, IFNAMSIZ - 1);
	ifr.ifr_data = (char *)&drvinfo;
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  SIOCETHTOOL,
				  (guint8 *)&ifr,
				  sizeof(ifr),
				  NULL,
				  500, /* ms */
				  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
				  error)) {
		g_prefix_error(error, "cannot get driver information: ");
		return FALSE;
	}
	g_debug("FW version %s", drvinfo.fw_version);

	/* detect more OEM cards */
	if (drvinfo.eedump_len == fu_device_get_firmware_size_max(FU_DEVICE(self)) * 2) {
		g_autofree gchar *subsys =
		    g_strdup_printf("%04X%04X",
				    fu_pci_device_get_subsystem_vid(FU_PCI_DEVICE(self)),
				    fu_pci_device_get_subsystem_pid(FU_PCI_DEVICE(self)));
		g_debug("auto-sizing expected EEPROM size for OEM SUBSYS %s", subsys);
		fu_device_set_firmware_size(FU_DEVICE(self), drvinfo.eedump_len);
	} else if (drvinfo.eedump_len != fu_device_get_firmware_size_max(FU_DEVICE(self))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "EEPROM size invalid, got 0x%x, expected 0x%x",
			    drvinfo.eedump_len,
			    (guint)fu_device_get_firmware_size_max(FU_DEVICE(self)));
		return FALSE;
	}

	/* success */
	return TRUE;
#else
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "Not supported as <linux/ethtool.h> not found");
	return FALSE;
#endif
}

static GBytes *
fu_bcm57xx_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE(device);
	const gsize bufsz = fu_device_get_firmware_size_max(FU_DEVICE(self));
	g_autofree guint8 *buf = g_malloc0(bufsz);
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_mutable_new(buf, bufsz, 0x0, 0x0, FU_BCM57XX_BLOCK_SZ);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_bcm57xx_device_nvram_read(self,
						  fu_chunk_get_address(chk),
						  fu_chunk_get_data_out(chk),
						  fu_chunk_get_data_sz(chk),
						  error))
			return NULL;
		fu_progress_step_done(progress);
	}

	/* read from hardware */
	return g_bytes_new_take(g_steal_pointer(&buf), bufsz);
}

static FuFirmware *
fu_bcm57xx_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_bcm57xx_firmware_new();
	g_autoptr(GBytes) fw = NULL;

	/* read from hardware */
	fw = fu_bcm57xx_device_dump_firmware(device, progress, error);
	if (fw == NULL)
		return NULL;
	if (!fu_firmware_parse(firmware, fw, FWUPD_INSTALL_FLAG_NO_SEARCH, error))
		return NULL;

	/* remove images that will contain user-data */
	if (!fu_firmware_remove_image_by_id(firmware, "info", error))
		return NULL;
	if (!fu_firmware_remove_image_by_id(firmware, "info2", error))
		return NULL;
	if (!fu_firmware_remove_image_by_id(firmware, "vpd", error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static FuFirmware *
fu_bcm57xx_device_prepare_firmware(FuDevice *device,
				   GInputStream *stream,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	guint dict_cnt = 0;
	g_autofree gchar *str_existing = NULL;
	g_autofree gchar *str_proposed = NULL;
	g_autoptr(GBytes) fw_old = NULL;
	g_autoptr(FuFirmware) firmware = fu_bcm57xx_firmware_new();
	g_autoptr(FuFirmware) firmware_tmp = fu_bcm57xx_firmware_new();
	g_autoptr(FuFirmware) img_ape = NULL;
	g_autoptr(FuFirmware) img_stage1 = NULL;
	g_autoptr(FuFirmware) img_stage2 = NULL;
	g_autoptr(GPtrArray) images = NULL;

	/* try to parse NVRAM, stage1 or APE */
	if (!fu_firmware_parse_stream(firmware_tmp, stream, 0x0, flags, error)) {
		g_prefix_error(error, "failed to parse new firmware: ");
		return NULL;
	}

	/* for full NVRAM image, verify if correct device */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) == 0) {
		guint16 vid = fu_bcm57xx_firmware_get_vendor(FU_BCM57XX_FIRMWARE(firmware_tmp));
		guint16 did = fu_bcm57xx_firmware_get_model(FU_BCM57XX_FIRMWARE(firmware_tmp));
		if (vid != 0x0 && did != 0x0 &&
		    (fu_device_get_vid(device) != vid || fu_device_get_pid(device) != did)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "PCI vendor or model incorrect, "
				    "got: %04X:%04X expected %04X:%04X",
				    vid,
				    did,
				    fu_device_get_vid(device),
				    fu_device_get_pid(device));
			return NULL;
		}
	}

	/* get the existing firmware from the device */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	fw_old = fu_bcm57xx_device_dump_firmware(device, progress, error);
	if (fw_old == NULL)
		return NULL;
	if (!fu_firmware_parse(firmware, fw_old, FWUPD_INSTALL_FLAG_NO_SEARCH, error)) {
		g_prefix_error(error, "failed to parse existing firmware: ");
		return NULL;
	}
	str_existing = fu_firmware_to_string(firmware);
	g_info("existing device firmware: %s", str_existing);

	/* merge in all the provided images into the existing firmware */
	img_stage1 = fu_firmware_get_image_by_id(firmware_tmp, "stage1", NULL);
	if (img_stage1 != NULL)
		fu_firmware_add_image(firmware, img_stage1);
	img_stage2 = fu_firmware_get_image_by_id(firmware_tmp, "stage2", NULL);
	if (img_stage2 != NULL)
		fu_firmware_add_image(firmware, img_stage2);
	img_ape = fu_firmware_get_image_by_id(firmware_tmp, "ape", NULL);
	if (img_ape != NULL)
		fu_firmware_add_image(firmware, img_ape);

	/* the src and dst dictionaries may be in different order */
	images = fu_firmware_get_images(firmware);
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		if (FU_IS_BCM57XX_DICT_IMAGE(img)) {
			fu_firmware_set_idx(img, 0x80 + dict_cnt);
			dict_cnt++;
		}
	}
	str_proposed = fu_firmware_to_string(firmware);
	g_info("proposed device firmware: %s", str_proposed);

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_bcm57xx_device_write_chunks(FuBcm57xxDevice *self,
			       FuChunkArray *chunks,
			       FuProgress *progress,
			       GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_bcm57xx_device_nvram_write(self,
						   fu_chunk_get_address(chk),
						   fu_chunk_get_data(chk),
						   fu_chunk_get_data_sz(chk),
						   error))
			return FALSE;
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_bcm57xx_device_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE(device);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob_verify = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "build-img");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, "write-chunks");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 19, NULL);

	/* build the images into one linear blob of the correct size */
	blob = fu_firmware_write(firmware, error);
	if (blob == NULL)
		return FALSE;
	fu_progress_step_done(progress);

	/* hit hardware */
	chunks = fu_chunk_array_new_from_bytes(blob, 0x0, FU_BCM57XX_BLOCK_SZ);
	if (!fu_bcm57xx_device_write_chunks(self, chunks, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify */
	blob_verify =
	    fu_bcm57xx_device_dump_firmware(device, fu_progress_get_child(progress), error);
	if (blob_verify == NULL)
		return FALSE;
	if (!fu_bytes_compare(blob, blob_verify, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_bcm57xx_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FwupdRequest) request = fwupd_request_new();

	/* APE reset cannot be done at runtime */
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_POST);
	fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_NON_GENERIC_MESSAGE);
	fwupd_request_set_message(request,
				  "After shutting down, disconnect the computer from all "
				  "power sources for 30 seconds to complete the update.");
	if (!fu_device_emit_request(device, request, progress, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_bcm57xx_device_setup(FuDevice *device, GError **error)
{
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE(device);
	guint32 fwversion = 0;

	/* check the EEPROM size */
	if (!fu_bcm57xx_device_nvram_check(self, error))
		return FALSE;

	/* get NVRAM version */
	if (!fu_bcm57xx_device_nvram_read(self,
					  BCM_NVRAM_STAGE1_BASE + BCM_NVRAM_STAGE1_VERSION,
					  (guint8 *)&fwversion,
					  sizeof(guint32),
					  error))
		return FALSE;
	if (fwversion != 0x0) {
		/* this is only set on the OSS firmware */
		fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
		fu_device_set_version_raw(device, GUINT32_FROM_BE(fwversion));
		fu_device_set_branch(device, BCM_FW_BRANCH_OSS_FIRMWARE);
	} else {
		guint8 bufver[16] = {0x0};
		guint32 veraddr = 0;
		g_autoptr(Bcm57xxVeritem) veritem = NULL;

		/* fall back to the string, e.g. '5719-v1.43' */
		if (!fu_bcm57xx_device_nvram_read(self,
						  BCM_NVRAM_STAGE1_BASE + BCM_NVRAM_STAGE1_VERADDR,
						  (guint8 *)&veraddr,
						  sizeof(guint32),
						  error))
			return FALSE;
		veraddr = GUINT32_FROM_BE(veraddr);
		if (veraddr > BCM_PHYS_ADDR_DEFAULT)
			veraddr -= BCM_PHYS_ADDR_DEFAULT;
		if (!fu_bcm57xx_device_nvram_read(self,
						  BCM_NVRAM_STAGE1_BASE + veraddr,
						  bufver,
						  sizeof(bufver),
						  error))
			return FALSE;
		veritem = fu_bcm57xx_veritem_new(bufver, sizeof(bufver));
		if (veritem != NULL) {
			fu_device_set_version_format(device, veritem->verfmt);
			fu_device_set_version(device, veritem->version); /* nocheck:set-version */
			fu_device_set_branch(device, veritem->branch);
		}
	}

	/* success */
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_BACKUP_BEFORE_INSTALL);
	return TRUE;
}

static gboolean
fu_bcm57xx_device_open(FuDevice *device, GError **error)
{
#ifdef HAVE_SOCKET_H
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE(device);
	self->ethtool_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (self->ethtool_fd < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to open socket: %s",
#ifdef HAVE_ERRNO_H
			    g_strerror(errno));
#else
			    "unspecified error");
#endif
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "socket() not supported as sys/socket.h not available");
	return FALSE;
#endif
}

static gboolean
fu_bcm57xx_device_close(FuDevice *device, GError **error)
{
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE(device);
	return g_close(self->ethtool_fd, error);
}

static void
fu_bcm57xx_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static gchar *
fu_bcm57xx_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_bcm57xx_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE(object);
	switch (prop_id) {
	case PROP_IFACE:
		g_value_set_string(value, self->ethtool_iface);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_bcm57xx_device_set_property(GObject *object,
			       guint prop_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE(object);
	switch (prop_id) {
	case PROP_IFACE:
		g_free(self->ethtool_iface);
		self->ethtool_iface = g_value_dup_string(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_bcm57xx_device_init(FuBcm57xxDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN);
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_NON_GENERIC_MESSAGE);
	fu_device_add_protocol(FU_DEVICE(self), "com.broadcom.bcm57xx");
	fu_device_add_icon(FU_DEVICE(self), "network-wired");

	/* other values are set from a quirk */
	fu_device_set_firmware_size(FU_DEVICE(self), BCM_FIRMWARE_SIZE);
}

static void
fu_bcm57xx_device_finalize(GObject *object)
{
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE(object);
	g_free(self->ethtool_iface);
	G_OBJECT_CLASS(fu_bcm57xx_device_parent_class)->finalize(object);
}

static void
fu_bcm57xx_device_class_init(FuBcm57xxDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->get_property = fu_bcm57xx_device_get_property;
	object_class->set_property = fu_bcm57xx_device_set_property;
	object_class->finalize = fu_bcm57xx_device_finalize;
	device_class->prepare_firmware = fu_bcm57xx_device_prepare_firmware;
	device_class->setup = fu_bcm57xx_device_setup;
	device_class->reload = fu_bcm57xx_device_setup;
	device_class->open = fu_bcm57xx_device_open;
	device_class->close = fu_bcm57xx_device_close;
	device_class->write_firmware = fu_bcm57xx_device_write_firmware;
	device_class->attach = fu_bcm57xx_device_attach;
	device_class->read_firmware = fu_bcm57xx_device_read_firmware;
	device_class->dump_firmware = fu_bcm57xx_device_dump_firmware;
	device_class->probe = fu_bcm57xx_device_probe;
	device_class->to_string = fu_bcm57xx_device_to_string;
	device_class->set_progress = fu_bcm57xx_device_set_progress;
	device_class->convert_version = fu_bcm57xx_device_convert_version;

	pspec =
	    g_param_spec_string("iface",
				NULL,
				NULL,
				NULL,
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_IFACE, pspec);
}
