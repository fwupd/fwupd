/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-mtd-recovery-device.h"

struct _FuMtdRecoveryDevice {
	FuDevice parent_instance;
	guint gpio_number;
	gchar *kernel_driver;
	gchar *bind_id;
};

G_DEFINE_TYPE(FuMtdRecoveryDevice, fu_mtd_recovery_device, FU_TYPE_DEVICE)

#define MTD_PROXY_TIMEOUT 500000 /* ms */

#define MTD_GPIO_NUMBER_UNSET G_MAXUINT

static void
fu_mtd_recovery_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuMtdRecoveryDevice *self = FU_MTD_RECOVERY_DEVICE(device);

	if (self->gpio_number != MTD_GPIO_NUMBER_UNSET)
		fu_string_append_kx(str, idt, "GpioNumber", self->gpio_number);
	if (self->bind_id != NULL)
		fu_string_append(str, idt, "BindId", self->bind_id);
	if (self->kernel_driver != NULL)
		fu_string_append(str, idt, "KernelDriver", self->kernel_driver);
}

static gboolean
fu_mtd_recovery_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuDevice *proxy;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* get the whole image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* process by the proxy */
	proxy = fu_device_get_proxy(device);
	if (proxy == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "no proxy device assigned");
		return FALSE;
	}
	locker = fu_device_locker_new(proxy, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_write_firmware(proxy, fw, progress, flags, error);
}

static gboolean
fu_mtd_recovery_device_set_gpio_direction(FuMtdRecoveryDevice *self,
					  const gchar *direction,
					  GError **error)
{
	g_autofree gchar *fn = NULL;
	g_autofree gchar *devname = g_strdup_printf("gpio%u", self->gpio_number);
	g_autoptr(FuIOChannel) io_channel = NULL;

	g_return_val_if_fail(direction != NULL, FALSE);

	fn = g_build_filename("/sys/class/gpio", devname, "direction", NULL);
	io_channel = fu_io_channel_new_file(fn, error);
	if (io_channel == NULL)
		return FALSE;
	return fu_io_channel_write_raw(io_channel,
				       (const guint8 *)direction,
				       strlen(direction),
				       1500,
				       FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
				       error);
}

static gboolean
fu_mtd_recovery_device_create_gpio(FuMtdRecoveryDevice *self, GError **error)
{
	g_autofree gchar *devname = g_strdup_printf("gpio%u", self->gpio_number);
	g_autofree gchar *fn = g_build_filename("/sys/class/gpio", devname, NULL);
	g_autofree gchar *gpio_number_str = NULL;
	g_autoptr(FuIOChannel) io_channel = NULL;

	/* already exists, so NOP */
	if (g_file_test(fn, G_FILE_TEST_EXISTS))
		return TRUE;

	/* echo the device number as a string */
	io_channel = fu_io_channel_new_file("/sys/class/gpio/export", error);
	if (io_channel == NULL)
		return FALSE;
	gpio_number_str = g_strdup_printf("%u", self->gpio_number);
	return fu_io_channel_write_raw(io_channel,
				       (const guint8 *)gpio_number_str,
				       strlen(gpio_number_str),
				       1500,
				       FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
				       error);
}

static void
fu_mtd_recovery_device_proxy_changed_cb(FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
}

static gboolean
fu_mtd_recovery_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMtdRecoveryDevice *self = FU_MTD_RECOVERY_DEVICE(device);
	g_autoptr(FuUdevDevice) device_tmp = NULL;

	/* already available */
	if (fu_device_get_proxy(device) != NULL)
		return TRUE;

	/* create GPIO device and disable write lock */
	if (self->gpio_number != MTD_GPIO_NUMBER_UNSET) {
		if (!fu_mtd_recovery_device_create_gpio(self, error))
			return FALSE;
		if (!fu_mtd_recovery_device_set_gpio_direction(self, "high", error))
			return FALSE;
	}

	/* bind driver */
	device_tmp = g_object_new(FU_TYPE_UDEV_DEVICE,
				  "context",
				  fu_device_get_context(device),
				  "subsystem",
				  "mtd",
				  NULL);
	if (self->bind_id != NULL)
		fu_udev_device_set_bind_id(device_tmp, self->bind_id);
	if (!fu_device_bind_driver(FU_DEVICE(device_tmp), "mtd", self->kernel_driver, error))
		return FALSE;

	/* wait for the MTD device to show up */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_mtd_recovery_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMtdRecoveryDevice *self = FU_MTD_RECOVERY_DEVICE(device);

	/* reset the MUX' select, so the host can read the spi flash to boot */
	if (self->gpio_number != MTD_GPIO_NUMBER_UNSET) {
		if (!fu_mtd_recovery_device_set_gpio_direction(self, "low", error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mtd_recovery_device_setup(FuDevice *device, GError **error)
{
	FuMtdRecoveryDevice *self = FU_MTD_RECOVERY_DEVICE(device);

	/* sanity check */
	if (self->kernel_driver == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "no MtdRecoveryKernelDriver assigned");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mtd_recovery_device_set_quirk_kv(FuDevice *device,
				    const gchar *key,
				    const gchar *value,
				    GError **error)
{
	FuMtdRecoveryDevice *self = FU_MTD_RECOVERY_DEVICE(device);
	if (g_strcmp0(key, "MtdRecoveryGpioNumber") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT, error))
			return FALSE;
		self->gpio_number = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "MtdRecoveryKernelDriver") == 0) {
		self->kernel_driver = g_strdup(value);
		return TRUE;
	}
	if (g_strcmp0(key, "MtdRecoveryBindId") == 0) {
		self->bind_id = g_strdup(value);
		return TRUE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_mtd_recovery_device_init(FuMtdRecoveryDevice *self)
{
	self->gpio_number = MTD_GPIO_NUMBER_UNSET;
	fu_device_set_id(FU_DEVICE(self), "mtd-recovery");
	fu_device_set_name(FU_DEVICE(self), "MTD Recovery");
	fu_device_set_summary(FU_DEVICE(self), "Offline Memory Technology Device");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(FU_DEVICE(self), "0.0.0");
	fu_device_set_remove_delay(FU_DEVICE(self), MTD_PROXY_TIMEOUT);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_icon(FU_DEVICE(self), "drive-harddisk-solidstate");
	fu_device_add_protocol(FU_DEVICE(self), "org.infradead.mtd");

	/* the MTD to write with is set as the proxy */
	g_signal_connect(self,
			 "notify::proxy",
			 G_CALLBACK(fu_mtd_recovery_device_proxy_changed_cb),
			 NULL);
}

static void
fu_mtd_recovery_device_finalize(GObject *obj)
{
	FuMtdRecoveryDevice *self = FU_MTD_RECOVERY_DEVICE(obj);
	g_free(self->kernel_driver);
	g_free(self->bind_id);
	G_OBJECT_CLASS(fu_mtd_recovery_device_parent_class)->finalize(obj);
}

static void
fu_mtd_recovery_device_class_init(FuMtdRecoveryDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_mtd_recovery_device_finalize;
	klass_device->setup = fu_mtd_recovery_device_setup;
	klass_device->to_string = fu_mtd_recovery_device_to_string;
	klass_device->write_firmware = fu_mtd_recovery_device_write_firmware;
	klass_device->attach = fu_mtd_recovery_device_attach;
	klass_device->detach = fu_mtd_recovery_device_detach;
	klass_device->set_quirk_kv = fu_mtd_recovery_device_set_quirk_kv;
}
