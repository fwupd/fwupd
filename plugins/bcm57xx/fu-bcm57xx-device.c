/*
 * Copyright (C) 2018 Evan Lojewski
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2+
 */

#include "config.h"

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <string.h>
#include <glib/gstdio.h>
#ifdef HAVE_ETHTOOL_H
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#endif
#ifdef HAVE_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SOCKET_H
#include <sys/socket.h>
#endif

#include <fwupdplugin.h>

#include "fu-bcm57xx-common.h"
#include "fu-bcm57xx-device.h"
#include "fu-bcm57xx-recovery-device.h"
#include "fu-bcm57xx-firmware.h"
#include "fu-bcm57xx-dict-image.h"

#define FU_BCM57XX_BLOCK_SZ		0x4000 /* 16kb */

struct _FuBcm57xxDevice {
	FuUdevDevice		 parent_instance;
	FuBcm57xxRecoveryDevice	*recovery;
	gchar			*ethtool_iface;
	int			 ethtool_fd;
};

G_DEFINE_TYPE (FuBcm57xxDevice, fu_bcm57xx_device, FU_TYPE_UDEV_DEVICE)

static void
fu_bcm57xx_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE (device);
	FU_DEVICE_CLASS (fu_bcm57xx_device_parent_class)->to_string (device, idt, str);
	fu_common_string_append_kv (str, idt, "EthtoolIface", self->ethtool_iface);
}

static gboolean
fu_bcm57xx_device_probe (FuDevice *device, GError **error)
{
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE (device);
	g_autofree gchar *fn = NULL;
	g_autoptr(GPtrArray) ifaces = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS (fu_bcm57xx_device_parent_class)->probe (device, error))
		return FALSE;

	/* only enumerate number 0 */
	if (fu_udev_device_get_number (FU_UDEV_DEVICE (device)) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "only device 0 supported on multi-device card");
		return FALSE;
	}

	/* we need this even for non-recovery to reset APE */
	fu_device_set_context (FU_DEVICE (self->recovery),
			       fu_device_get_context (FU_DEVICE (self)));
	fu_device_incorporate (FU_DEVICE (self->recovery), FU_DEVICE (self));
	if (!fu_device_probe (FU_DEVICE (self->recovery), error))
		return FALSE;

	/* only if has an interface */
	fn = g_build_filename (fu_udev_device_get_sysfs_path (FU_UDEV_DEVICE (device)), "net", NULL);
	if (!g_file_test (fn, G_FILE_TEST_EXISTS)) {
		g_debug ("waiting for net devices to appear");
		g_usleep (50 * 1000);
	}
	ifaces = fu_common_filename_glob (fn, "en*", NULL);
	if (ifaces == NULL || ifaces->len == 0) {
		fu_device_add_child (FU_DEVICE (self), FU_DEVICE (self->recovery));
	} else {
		self->ethtool_iface = g_path_get_basename (g_ptr_array_index (ifaces, 0));
	}

	/* success */
	return fu_udev_device_set_physical_id (FU_UDEV_DEVICE (device), "pci", error);
}

static gboolean
fu_bcm57xx_device_nvram_write (FuBcm57xxDevice *self,
			       guint32 address,
			       const guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
#ifdef HAVE_ETHTOOL_H
	gsize eepromsz;
	gint rc = -1;
	struct ifreq ifr = { 0 };
	g_autofree struct ethtool_eeprom *eeprom = NULL;

	/* failed to load tg3 */
	if (self->ethtool_iface == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "Not supported as ethtool interface disabled");
		return FALSE;
	}

	/* sanity check */
	if (address + bufsz > fu_device_get_firmware_size_max (FU_DEVICE (self))) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "tried to read outside of EEPROM size [0x%x]",
			     (guint) fu_device_get_firmware_size_max (FU_DEVICE (self)));
		return FALSE;
	}

	/* write EEPROM (NVRAM) data */
	eepromsz = sizeof(struct ethtool_eeprom) + bufsz;
	eeprom = (struct ethtool_eeprom *) g_malloc0 (eepromsz);
	eeprom->cmd = ETHTOOL_SEEPROM;
	eeprom->magic = BCM_NVRAM_MAGIC;
	eeprom->len = bufsz;
	eeprom->offset = address;
	memcpy (eeprom->data, buf, eeprom->len);
	strncpy (ifr.ifr_name, self->ethtool_iface, IFNAMSIZ - 1);
	ifr.ifr_data = (char *) eeprom;
#ifdef HAVE_IOCTL_H
	rc = ioctl (self->ethtool_fd, SIOCETHTOOL, &ifr);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <sys/ioctl.h> not found");
	return FALSE;
#endif
	if (rc < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "cannot write eeprom [%i]", rc);
		return FALSE;
	}

	/* success */
	return TRUE;
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <linux/ethtool.h> not found");
	return FALSE;
#endif
}

static gboolean
fu_bcm57xx_device_nvram_read (FuBcm57xxDevice *self,
			      guint32 address,
			      guint8 *buf,
			      gsize bufsz,
			      GError **error)
{
#ifdef HAVE_ETHTOOL_H
	gsize eepromsz;
	gint rc = -1;
	struct ifreq ifr = { 0 };
	g_autofree struct ethtool_eeprom *eeprom = NULL;

	/* failed to load tg3 */
	if (self->ethtool_iface == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "Not supported as ethtool interface disabled");
		return FALSE;
	}

	/* sanity check */
	if (address + bufsz > fu_device_get_firmware_size_max (FU_DEVICE (self))) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "tried to read outside of EEPROM size [0x%x]",
			     (guint) fu_device_get_firmware_size_max (FU_DEVICE (self)));
		return FALSE;
	}

	/* read EEPROM (NVRAM) data */
	eepromsz = sizeof(struct ethtool_eeprom) + bufsz;
	eeprom = (struct ethtool_eeprom *) g_malloc0 (eepromsz);
	eeprom->cmd = ETHTOOL_GEEPROM;
	eeprom->len = bufsz;
	eeprom->offset = address;
	strncpy (ifr.ifr_name, self->ethtool_iface, IFNAMSIZ - 1);
	ifr.ifr_data = (char *) eeprom;
#ifdef HAVE_IOCTL_H
	rc = ioctl (self->ethtool_fd, SIOCETHTOOL, &ifr);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <sys/ioctl.h> not found");
	return FALSE;
#endif
	if (rc < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "cannot read eeprom [%i]", rc);
		return FALSE;
	}

	/* copy back data */
	if (!fu_memcpy_safe (buf, bufsz, 0x0,		/* dst */
			     (guint8 *) eeprom, eepromsz,	/* src */
			     G_STRUCT_OFFSET(struct ethtool_eeprom, data),
			     bufsz, error))
		return FALSE;

	/* success */
	return TRUE;
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <linux/ethtool.h> not found");
	return FALSE;
#endif
}

static gboolean
fu_bcm57xx_device_nvram_check (FuBcm57xxDevice *self, GError **error)
{
#ifdef HAVE_ETHTOOL_H
	gint rc = -1;
	struct ethtool_drvinfo drvinfo = { 0 };
	struct ifreq ifr = { 0 };

	/* failed to load tg3 */
	if (self->ethtool_iface == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "Not supported as ethtool interface disabled");
		return FALSE;
	}

	/* get driver info */
	drvinfo.cmd = ETHTOOL_GDRVINFO;
	strncpy (ifr.ifr_name, self->ethtool_iface, IFNAMSIZ - 1);
	ifr.ifr_data = (char *) &drvinfo;
#ifdef HAVE_IOCTL_H
	rc = ioctl (self->ethtool_fd, SIOCETHTOOL, &ifr);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Not supported as <sys/ioctl.h> not found");
	return FALSE;
#endif
	if (rc < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "cannot get driver information [%i]", rc);
		return FALSE;
	}
	g_debug ("FW version %s", drvinfo.fw_version);

	/* sanity check */
	if (drvinfo.eedump_len != fu_device_get_firmware_size_max (FU_DEVICE (self))) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "EEPROM size invalid, got 0x%x, expected 0x%x",
			     drvinfo.eedump_len,
			     (guint) fu_device_get_firmware_size_max (FU_DEVICE (self)));
		return FALSE;
	}

	/* success */
	return TRUE;
#else
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "Not supported as <linux/ethtool.h> not found");
	return FALSE;
#endif
}

static gboolean
fu_bcm57xx_device_activate (FuDevice *device, GError **error)
{
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE (device);
	g_autoptr(FuDeviceLocker) locker1 = NULL;
	g_autoptr(FuDeviceLocker) locker2 = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new();

	/* the only way to do this is using the mmap method */
	locker2 = fu_device_locker_new_full (FU_DEVICE (self->recovery),
					     (FuDeviceLockerFunc) fu_device_detach,
					     (FuDeviceLockerFunc) fu_device_attach,
					     error);
	if (locker2 == NULL)
		return FALSE;

	/* open */
	locker1 = fu_device_locker_new (FU_DEVICE (self->recovery), error);
	if (locker1 == NULL)
		return FALSE;

	/* activate, causing APE reset, then close, then attach */
	if (!fu_device_activate (FU_DEVICE (self->recovery), error))
		return FALSE;

	/* ensure we attach before we close */
	if (!fu_device_locker_close (locker2, error))
		return FALSE;

	/* wait for the device to restart before calling reload() */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_BUSY);
	fu_progress_sleep(progress, 5); /* seconds */
	return TRUE;
}

static GBytes *
fu_bcm57xx_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE (device);
	const gsize bufsz = fu_device_get_firmware_size_max (FU_DEVICE (self));
	g_autofree guint8 *buf = g_malloc0 (bufsz);
	g_autoptr(GPtrArray) chunks = NULL;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_READ);
	chunks = fu_chunk_array_mutable_new (buf, bufsz, 0x0, 0x0, FU_BCM57XX_BLOCK_SZ);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_bcm57xx_device_nvram_read (self, fu_chunk_get_address (chk),
						   fu_chunk_get_data_out (chk),
						   fu_chunk_get_data_sz (chk),
						   error))
			return NULL;
		fu_progress_set_percentage_full(progress, i, chunks->len - 1);
	}

	/* read from hardware */
	return g_bytes_new_take (g_steal_pointer (&buf), bufsz);
}

static FuFirmware *
fu_bcm57xx_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_bcm57xx_firmware_new ();
	g_autoptr(GBytes) fw = NULL;

	/* read from hardware */
	fw = fu_bcm57xx_device_dump_firmware(device, progress, error);
	if (fw == NULL)
		return NULL;
	if (!fu_firmware_parse (firmware, fw, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;

	/* remove images that will contain user-data */
	if (!fu_firmware_remove_image_by_id (firmware, "info", error))
		return NULL;
	if (!fu_firmware_remove_image_by_id (firmware, "info2", error))
		return NULL;
	if (!fu_firmware_remove_image_by_id (firmware, "vpd", error))
		return NULL;
	return g_steal_pointer (&firmware);
}

static FuFirmware *
fu_bcm57xx_device_prepare_firmware (FuDevice *device,
				    GBytes *fw,
				    FwupdInstallFlags flags,
				    GError **error)
{
	guint dict_cnt = 0;
	g_autoptr(GBytes) fw_old = NULL;
	g_autoptr(FuFirmware) firmware = fu_bcm57xx_firmware_new ();
	g_autoptr(FuFirmware) firmware_tmp = fu_bcm57xx_firmware_new ();
	g_autoptr(FuFirmware) img_ape = NULL;
	g_autoptr(FuFirmware) img_stage1 = NULL;
	g_autoptr(FuFirmware) img_stage2 = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(); // FIXME
	g_autoptr(GPtrArray) images = NULL;

	/* try to parse NVRAM, stage1 or APE */
	if (!fu_firmware_parse (firmware_tmp, fw, flags, error)) {
		g_prefix_error (error, "failed to parse new firmware: ");
		return NULL;
	}

	/* for full NVRAM image, verify if correct device */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) == 0) {
		guint16 vid = fu_bcm57xx_firmware_get_vendor (FU_BCM57XX_FIRMWARE (firmware_tmp));
		guint16 did = fu_bcm57xx_firmware_get_model (FU_BCM57XX_FIRMWARE (firmware_tmp));
		if (vid != 0x0 && did != 0x0 &&
		    (fu_udev_device_get_vendor (FU_UDEV_DEVICE (device)) != vid ||
		     fu_udev_device_get_model (FU_UDEV_DEVICE (device)) != did)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "PCI vendor or model incorrect, "
				     "got: %04X:%04X expected %04X:%04X",
				     vid, did,
				     fu_udev_device_get_vendor (FU_UDEV_DEVICE (device)),
				     fu_udev_device_get_model (FU_UDEV_DEVICE (device)));
			return NULL;
		}
	}

	/* get the existing firmware from the device */
	fw_old = fu_bcm57xx_device_dump_firmware(device, progress, error);
	if (fw_old == NULL)
		return NULL;
	if (!fu_firmware_parse (firmware, fw_old, flags, error)) {
		g_prefix_error (error, "failed to parse existing firmware: ");
		return NULL;
	}
	if (g_getenv ("FWUPD_BCM57XX_VERBOSE") != NULL) {
		g_autofree gchar *str = fu_firmware_to_string (firmware);
		g_debug ("existing device firmware: %s", str);
	}

	/* merge in all the provided images into the existing firmware */
	img_stage1 = fu_firmware_get_image_by_id (firmware_tmp, "stage1", NULL);
	if (img_stage1 != NULL)
		fu_firmware_add_image (firmware, img_stage1);
	img_stage2 = fu_firmware_get_image_by_id (firmware_tmp, "stage2", NULL);
	if (img_stage2 != NULL)
		fu_firmware_add_image (firmware, img_stage2);
	img_ape = fu_firmware_get_image_by_id (firmware_tmp, "ape", NULL);
	if (img_ape != NULL)
		fu_firmware_add_image (firmware, img_ape);

	/* the src and dst dictionaries may be in different order */
	images = fu_firmware_get_images (firmware);
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index (images, i);
		if (FU_IS_BCM57XX_DICT_IMAGE (img)) {
			fu_firmware_set_idx (img, 0x80 + dict_cnt);
			dict_cnt++;
		}
	}
	if (g_getenv ("FWUPD_BCM57XX_VERBOSE") != NULL) {
		g_autofree gchar *str = fu_firmware_to_string (firmware);
		g_debug ("proposed device firmware: %s", str);
	}

	/* success */
	return g_steal_pointer (&firmware);
}

static gboolean
fu_bcm57xx_device_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE (device);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob_verify = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* build the images into one linear blob of the correct size */
	fu_device_set_status (device, FWUPD_STATUS_DECOMPRESSING);
	blob = fu_firmware_write (firmware, error);
	if (blob == NULL)
		return FALSE;

	/* hit hardware */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	chunks = fu_chunk_array_new_from_bytes (blob, 0x0, 0x0, FU_BCM57XX_BLOCK_SZ);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		if (!fu_bcm57xx_device_nvram_write (self, fu_chunk_get_address (chk),
						    fu_chunk_get_data (chk),
						    fu_chunk_get_data_sz (chk),
						    error))
			return FALSE;
		fu_progress_set_percentage_full(progress, i, chunks->len - 1);
	}

	/* verify */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_VERIFY);
	blob_verify = fu_bcm57xx_device_dump_firmware(device, progress, error);
	if (blob_verify == NULL)
		return FALSE;
	if (!fu_common_bytes_compare (blob, blob_verify, error))
		return FALSE;

	/* reset APE */
	return fu_device_activate (device, error);
}

static gboolean
fu_bcm57xx_device_setup (FuDevice *device, GError **error)
{
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE (device);
	guint32 fwversion = 0;

	/* device is in recovery mode */
	if (self->ethtool_iface == NULL) {
		g_autoptr(FuDeviceLocker) locker = NULL;
		g_debug ("device in recovery mode, use alternate device");
		locker = fu_device_locker_new (FU_DEVICE (self->recovery), error);
		if (locker == NULL)
			return FALSE;
		return fu_device_setup (FU_DEVICE (self->recovery), error);
	}

	/* check the EEPROM size */
	if (!fu_bcm57xx_device_nvram_check (self, error))
		return FALSE;

	/* get NVRAM version */
	if (!fu_bcm57xx_device_nvram_read (self, BCM_NVRAM_STAGE1_BASE + BCM_NVRAM_STAGE1_VERSION,
					   (guint8 *) &fwversion, sizeof(guint32), error))
		return FALSE;
	if (fwversion != 0x0) {
		g_autofree gchar *fwversion_str = NULL;

		/* this is only set on the OSS firmware */
		fwversion_str = fu_common_version_from_uint32 (GUINT32_FROM_BE(fwversion),
							       FWUPD_VERSION_FORMAT_TRIPLET);
		fu_device_set_version_format (device, FWUPD_VERSION_FORMAT_TRIPLET);
		fu_device_set_version (device, fwversion_str);
		fu_device_set_version_raw (device, fwversion);
		fu_device_set_branch (device, BCM_FW_BRANCH_OSS_FIRMWARE);
	} else {
		guint8 bufver[16] = { 0x0 };
		guint32 veraddr = 0;
		g_autoptr(Bcm57xxVeritem) veritem = NULL;

		/* fall back to the string, e.g. '5719-v1.43' */
		if (!fu_bcm57xx_device_nvram_read (self,
						   BCM_NVRAM_STAGE1_BASE + BCM_NVRAM_STAGE1_VERADDR,
						   (guint8 *) &veraddr, sizeof(guint32), error))
			return FALSE;
		veraddr = GUINT32_FROM_BE(veraddr);
		if (veraddr > BCM_PHYS_ADDR_DEFAULT)
			veraddr -= BCM_PHYS_ADDR_DEFAULT;
		if (!fu_bcm57xx_device_nvram_read (self,
						   BCM_NVRAM_STAGE1_BASE + veraddr,
						   bufver, sizeof(bufver), error))
			return FALSE;
		veritem = fu_bcm57xx_veritem_new (bufver, sizeof(bufver));
		if (veritem != NULL) {
			fu_device_set_version_format (device, veritem->verfmt);
			fu_device_set_version (device, veritem->version);
			fu_device_set_branch (device, veritem->branch);
		}
	}

	/* success */
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_BACKUP_BEFORE_INSTALL);
	return TRUE;
}

static gboolean
fu_bcm57xx_device_open (FuDevice *device, GError **error)
{
#ifdef HAVE_SOCKET_H
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE (device);
	self->ethtool_fd = socket (AF_INET, SOCK_DGRAM, 0);
	if (self->ethtool_fd < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "failed to open socket: %s",
#ifdef HAVE_ERRNO_H
			     strerror (errno));
#else
			     "unspecified error");
#endif
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "socket() not supported as sys/socket.h not available");
	return FALSE;
#endif
}

static gboolean
fu_bcm57xx_device_close (FuDevice *device, GError **error)
{
	FuBcm57xxDevice *self = FU_BCM57XX_DEVICE (device);
	return g_close (self->ethtool_fd, error);
}

static void
fu_bcm57xx_device_init (FuBcm57xxDevice *self)
{
	fu_device_add_protocol (FU_DEVICE (self), "com.broadcom.bcm57xx");
	fu_device_add_icon (FU_DEVICE (self), "network-wired");

	/* other values are set from a quirk */
	fu_device_set_firmware_size (FU_DEVICE (self), BCM_FIRMWARE_SIZE);

	/* used for recovery in case of ethtool failure and for APE reset */
	self->recovery = fu_bcm57xx_recovery_device_new ();
}

static void
fu_bcm57xx_device_finalize (GObject *object)
{
	FuBcm57xxDevice *self= FU_BCM57XX_DEVICE (object);
	g_free (self->ethtool_iface);
	G_OBJECT_CLASS (fu_bcm57xx_device_parent_class)->finalize (object);
}

static void
fu_bcm57xx_device_class_init (FuBcm57xxDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_bcm57xx_device_finalize;
	klass_device->prepare_firmware = fu_bcm57xx_device_prepare_firmware;
	klass_device->setup = fu_bcm57xx_device_setup;
	klass_device->reload = fu_bcm57xx_device_setup;
	klass_device->open = fu_bcm57xx_device_open;
	klass_device->close = fu_bcm57xx_device_close;
	klass_device->activate = fu_bcm57xx_device_activate;
	klass_device->write_firmware = fu_bcm57xx_device_write_firmware;
	klass_device->read_firmware = fu_bcm57xx_device_read_firmware;
	klass_device->dump_firmware = fu_bcm57xx_device_dump_firmware;
	klass_device->probe = fu_bcm57xx_device_probe;
	klass_device->to_string = fu_bcm57xx_device_to_string;
}
