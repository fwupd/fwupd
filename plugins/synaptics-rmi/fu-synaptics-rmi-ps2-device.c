/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (c) 2020 Synaptics Incorporated.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaptics-rmi-ps2-device.h"
#include "fu-io-channel.h"

struct _FuSynapticsRmiPs2Device {
	FuUdevDevice		 parent_instance;
	FuIOChannel		*io_channel;
	gboolean		 inRMIBackdoor;
};

G_DEFINE_TYPE (FuSynapticsRmiPs2Device, fu_synaptics_rmi_ps2_device, FU_TYPE_UDEV_DEVICE)

static gboolean
ReadACK (FuSynapticsRmiPs2Device *self, guint8 *pbuf, GError **error) //FIXME: rename to fu_synaptics_rmi_ps2_device_read_ack
{
	for(guint i = 0 ; i < 60; i++) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_io_channel_read_raw (self->io_channel, pbuf, 0x1,
					     NULL, 60,
					     FU_IO_CHANNEL_FLAG_NONE,
					     &error_local)) {
			if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
				g_debug ("read timed out: %u", i);
				g_usleep (30);
				continue;
			}
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
		return TRUE;
	}
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "failed");
	return FALSE;
}

/* read a single byte from the touchpad */
static gboolean
ReadByte (FuSynapticsRmiPs2Device *self, guint8 *pbuf, guint timeout, GError **error) //FIXME: rename to fu_synaptics_rmi_ps2_device_read_byte
{
	return fu_io_channel_read_raw (self->io_channel, pbuf, 0x1,
				       NULL, timeout,
				       FU_IO_CHANNEL_FLAG_NONE,
				       error);
}

/* write a single byte to the touchpad and the read the acknowledge */
static gboolean
WriteByte (FuSynapticsRmiPs2Device *self, guint8 buf, guint timeout, GError **error) //FIXME: rename to fu_synaptics_rmi_ps2_device_write_byte
{
	gboolean do_write = TRUE;

	for (guint i = 0; i < 3; i++) {
		guint8 res = 0;
		g_autoptr(GError) error_local = NULL;

		if (do_write) {
			if (!fu_io_channel_write_raw (self->io_channel, &buf, 0x1, timeout,
						      FU_IO_CHANNEL_FLAG_FLUSH_INPUT, error))
				return FALSE;
		}
		do_write = FALSE;
		g_debug ("wrote byte: 0x%x, attempt to read acknowledge...", buf);

		if(!ReadACK (self, &res, &error_local)) {
			g_debug ("read Failed: %s", error_local->message);
			continue;
		}
		if (res == edpsAcknowledge) {
			g_debug ("write acknowledged");
			return TRUE;
		}
		if (res == edpsResend) {
			g_debug ("resend");
			do_write = TRUE;
			g_debug ("resend, sleep 1 sec");
			g_usleep (1000*1000);
			continue;
		}
		if (res == edpsError) {
			g_debug ("WriteByte fail received error from touchpad");
			do_write = TRUE;
			g_usleep (1000 * 10);
			continue;
		}
		g_debug ("other response : 0x%x, sleep 1 sec", res);
		g_usleep (1000 * 10);
	}
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "failed");
	return FALSE;
}

static gboolean
WriteRMIRegister (FuSynapticsRmiPs2Device *self, 
				 guint8 addr, 
				 const guint8 *buf, 
				 guint8 len, 
				 guint timeout, 
				 GError **error)
{

	if (!self->inRMIBackdoor && !fu_synaptics_rmi_ps2_device_enable_RMI_backdoor(self, error)) {
		g_prefix_error (error, "failed to enable RMI backdoor: ");
		return FALSE;
	}
		

	if(!WriteByte(self, edpAuxSetScaling2To1, timeout, error)) {
		g_prefix_error (error, "failed to WriteByte: ");
		return FALSE;
	}

	if (!WriteByte(self, edpAuxSetSampleRate, timeout, error)) {
		g_prefix_error (error, "failed to WriteByte: ");
		return FALSE;
	}

	if (!WriteByte(self, addr, timeout, error)) {
		g_prefix_error (error, "failed to WriteByte: ");
		return FALSE;
	}

	for(guint8 numBytes = 0 ; numBytes < len ; numBytes++){
		if (!WriteByte(self, edpAuxSetSampleRate, timeout, error)) {
			g_prefix_error (error, "failed to WriteByte: ");
			return FALSE;
		}

		if (!WriteByte(self, *(buf + numBytes), timeout, error)) {
			g_prefix_error (error, "failed to WriteByte: ");
			return FALSE;
		}
	}
	

	g_usleep (1000*20);

	return TRUE;
}

static gboolean
ReadRMIRegister (FuSynapticsRmiPs2Device *self, 
				guint8 addr, 
				guint8 * buf,
				GError **error)
{
	if (!buf) {
		return FALSE;
	}

	g_debug ("ReadRMIRegister: register address = 0x%x", addr);
	if (!self->inRMIBackdoor && !fu_synaptics_rmi_ps2_device_enable_RMI_backdoor(self, error)) {
		g_prefix_error (error, "failed to enable RMI backdoor: ");
		return FALSE;
	}

	g_autoptr(GError) error_local = NULL;
	if (!(WriteByte(self, edpAuxSetScaling2To1, 0, &error_local) && WriteByte(self, edpAuxSetSampleRate, 0, &error_local)
		&& WriteByte(self, addr, 0, &error_local) && WriteByte(self, edpAuxStatusRequest, 0, &error_local)))
	{
		g_warning ("failed to write command in Read RMI Register: %s", error_local->message);
		return FALSE;
	}

	guint32 response = 0;
	for (gint i = 0; i < 3; ++i) {
		guint8 tmp = 0;
		if(!ReadByte(self, &tmp, 0, error)) {
			g_prefix_error (error, "failed to Read byte: ");
			return FALSE;
		}
		response = response | (tmp << (8 * i));
	}

	// We only care about the least significant byte since that is what contains the value
	// of the register at the address addr
	*buf = (guint8)response;
	g_debug ("RMI value == 0x%x", *buf);

	g_usleep (1000*20);

	g_debug ("Finished Read RMI Register");

	return TRUE;
}

static gboolean
ReadRMIPacketRegister(FuSynapticsRmiPs2Device *self,
					  guint8 addr, 
					  guint8 * buf, 
					  gint len, 
					  GError **error)
{
	if (!buf) {
		return FALSE;
	}

	g_debug ("ReadRMIPacketRegister: register address = 0x%x", addr);
	if (!self->inRMIBackdoor && !fu_synaptics_rmi_ps2_device_enable_RMI_backdoor(self, error)) {
		g_prefix_error (error, "failed to enable RMI backdoor: ");
		return FALSE;
	}

	g_autoptr(GError) error_local = NULL;
	if (!(WriteByte(self, edpAuxSetScaling2To1, 0, &error_local) && WriteByte(self, edpAuxSetSampleRate, 0, &error_local)
		&& WriteByte(self, addr, 0, &error_local) && WriteByte(self, edpAuxStatusRequest, 0, &error_local)))
	{
		g_warning ("failed to write command in Read RMI Packet Register: %s", error_local->message);
		return FALSE;
	}

	for (gint i = 0; i < len; ++i) {
		guint8 tmp = 0;
		if(!ReadByte(self, &tmp, 0, error)) {
			return FALSE;
		}
		*(buf+i) = tmp;
	}

	g_usleep (1000*20);

	g_debug ("Finished Read RMI Packet Register");

	return TRUE;
}

static void
fu_synaptics_rmi_ps2_device_to_string (FuUdevDevice *device, guint idt, GString *str)
{
	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (device);
	fu_common_string_append_kb (str, idt, "InRmiBackdoor", self->inRMIBackdoor);
}

static gboolean
fu_synaptics_rmi_ps2_device_probe (FuUdevDevice *device, GError **error)
{
	/* psmouse is the usual mode, but serio is needed for update */
	if (g_strcmp0 (fu_udev_device_get_driver (device), "serio_raw") == 0) {
		fu_device_add_flag (FU_DEVICE (device),
				    FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag (FU_DEVICE (device),
				       FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* set the physical ID */
	return fu_udev_device_set_physical_id (device, "platform", error);
}

static gboolean
fu_synaptics_rmi_ps2_device_setup (FuDevice *device, GError **error)
{
//	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (device);
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_open (FuUdevDevice *device, GError **error)
{
	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (device);
	guint8 buf[2] = { 0x0 };

	/* create channel */
	self->io_channel = fu_io_channel_unix_new (fu_udev_device_get_fd (device));

	/* in serio_raw mode */
	if (fu_device_has_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {

		/* clear out any data in the serio_raw queue */
		for(guint i = 0; i < 0xffff; i++) {
			guint8 tmp = 0;
			if (!ReadByte (self, &tmp, 20, NULL))
				break;
		}

		/* send reset -- may take 300-500ms */
		if (!WriteByte (self, edpAuxReset, 600, error)) {
			g_prefix_error (error, "failed to reset: ");
			return FALSE;
		}

		/* read the 0xAA 0x00 announcing the touchpad is ready */
		if (!ReadByte(self, &buf[0], 500, error) ||
		    !ReadByte(self, &buf[1], 500, error)) {
			g_prefix_error (error, "failed to read 0xAA00: ");
			return FALSE;
		}
		if (buf[0] != 0xAA || buf[1] != 0x00) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     "failed to read 0xAA00, got 0x%02X%02X: ",
				     buf[0], buf[1]);
			return FALSE;
		}

		/* disable the device so that it stops reporting finger data */
		if (!WriteByte (self, edpAuxDisable, 50, error)) {
			g_prefix_error (error, "failed to disable stream mode: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_close (FuUdevDevice *device, GError **error)
{
	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (device);
	fu_udev_device_set_fd (device, -1);
	g_clear_object (&self->io_channel);
	return TRUE;
}

static FuFirmware *
fu_synaptics_rmi_ps2_device_prepare_firmware (FuDevice *device,
					      GBytes *fw,
					      FwupdInstallFlags flags,
					      GError **error)
{
//	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (device);
	g_autoptr(FuFirmware) firmware = fu_firmware_new ();

	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;

	/* success */
	return g_steal_pointer (&firmware);
}

static gboolean
fu_synaptics_rmi_ps2_device_write_firmware (FuDevice *device,
					    FuFirmware *firmware,
					    FwupdInstallFlags flags,
					    GError **error)
{
//	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (device);
	fu_device_sleep_with_progress (device, 5);
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_set_resolution_sequence (FuSynapticsRmiPs2Device *self,
						     guint8 arg,
						     gboolean send_e6s,
						     GError **error)
{
	g_debug ("Set Resolution Sequence: arg = 0x%x", arg);

	/* send set scaling twice if send_e6s */
	for (gint i = send_e6s ? 2 : 1; i > 0; --i) {
		if (!WriteByte (self, edpAuxSetScaling1To1, 50, error))
			return FALSE;
	}

	for (gint i = 3; i >= 0; --i) {
		guint8 ucTwoBitArg = (arg >> (i * 2)) & 0x3;
		if (!WriteByte (self, edpAuxSetResolution, 50, error)) {
			return FALSE;
		}
		g_debug ("Send ucTwoBitArg = 0x%x", ucTwoBitArg);
		if (!WriteByte (self, ucTwoBitArg, 50, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_set_sample_rate_sequence (FuSynapticsRmiPs2Device *self,
						      guint8 param,
						      guint8 arg,
						      gboolean send_e6s,
						      GError **error)
{
	/* allow 3 retries */
	for (guint i = 0; i < 3; i++) {
		g_autoptr(GError) error_local = NULL;
		if (i > 0) {
			/* always send two E6s when retrying */
			send_e6s = TRUE;
		}
		if (!fu_synaptics_rmi_ps2_device_set_resolution_sequence (self, arg, send_e6s, &error_local) ||
		    !WriteByte (self, edpAuxSetSampleRate, 50, &error_local) ||
		    !WriteByte (self, param, 50, &error_local)) {
			g_warning ("failed, will retry: %s", error_local->message);
			continue;
		}
		return TRUE;
	}

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "too many tries");
	return FALSE;
}

gboolean
fu_synaptics_rmi_ps2_device_enable_RMI_backdoor (FuSynapticsRmiPs2Device *self,
						      GError **error)
{
	g_debug ("Enable RMI backdoor");

	/* disable stream */
	if (!WriteByte (self, edpAuxDisable, 50, error)) {
		g_prefix_error (error, "failed to disable stream mode: ");
		return FALSE;
	}

	/* enable RMI mode */
	if (!fu_synaptics_rmi_ps2_device_set_sample_rate_sequence (self, essrSetModeByte2, edpAuxFullRMIBackDoor, FALSE, error)) {
		g_prefix_error (error, "failed to enter RMI mode: ");
		return FALSE;
	}

	return TRUE;
}


static gboolean
fu_synaptics_rmi_ps2_device_detach (FuDevice *device, GError **error)
{
	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (device);

	/* sanity check */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("already in bootloader mode, skipping");
		return TRUE;
	}

	/* put in serio_raw mode so that we can do register writes */
	if (!fu_udev_device_write_sysfs (FU_UDEV_DEVICE (device),
					 "drvctl", "serio_raw", error)) {
		g_prefix_error (error, "failed to write to drvctl: ");
		return FALSE;
	}

	/* rescan device */
	if (!fu_device_close (device, error))
		return FALSE;
	if (!fu_device_rescan (device, error))
		return FALSE;
	if (!fu_device_open (device, error))
		return FALSE;

	if (!fu_synaptics_rmi_ps2_device_enable_RMI_backdoor(self, error)){
		g_prefix_error (error, "failed to enable RMI backdoor: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_attach (FuDevice *device, GError **error)
{
	/* sanity check */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("already in runtime mode, skipping");
		return TRUE;
	}

	/* back to psmouse */
	if (!fu_udev_device_write_sysfs (FU_UDEV_DEVICE (device),
					 "drvctl", "psmouse", error)) {
		g_prefix_error (error, "failed to write to drvctl: ");
		return FALSE;
	}

	/* rescan device */
	return fu_device_rescan (device, error);
}

static void
fu_synaptics_rmi_ps2_device_init (FuSynapticsRmiPs2Device *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_protocol (FU_DEVICE (self), "com.synaptics.rmi");
	fu_device_set_name (FU_DEVICE (self), "TouchStyk");
	fu_device_set_vendor (FU_DEVICE (self), "Synaptics");
	fu_device_set_vendor_id (FU_DEVICE (self), "HIDRAW:0x06CB");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_HEX); //FIXME?
	fu_udev_device_set_flags (FU_UDEV_DEVICE (self),
				  FU_UDEV_DEVICE_FLAG_OPEN_READ |
				  FU_UDEV_DEVICE_FLAG_OPEN_WRITE);
}

static void
fu_synaptics_rmi_ps2_device_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_synaptics_rmi_ps2_device_parent_class)->finalize (object);
}

static void
fu_synaptics_rmi_ps2_device_class_init (FuSynapticsRmiPs2DeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUdevDeviceClass *klass_udev_device = FU_UDEV_DEVICE_CLASS (klass);
	object_class->finalize = fu_synaptics_rmi_ps2_device_finalize;
	klass_device->attach = fu_synaptics_rmi_ps2_device_attach;
	klass_device->detach = fu_synaptics_rmi_ps2_device_detach;
	klass_device->setup = fu_synaptics_rmi_ps2_device_setup;
	klass_device->reload = fu_synaptics_rmi_ps2_device_setup;
	klass_device->write_firmware = fu_synaptics_rmi_ps2_device_write_firmware;
	klass_device->prepare_firmware = fu_synaptics_rmi_ps2_device_prepare_firmware;
	klass_udev_device->to_string = fu_synaptics_rmi_ps2_device_to_string;
	klass_udev_device->probe = fu_synaptics_rmi_ps2_device_probe;
	klass_udev_device->open = fu_synaptics_rmi_ps2_device_open;
	klass_udev_device->close = fu_synaptics_rmi_ps2_device_close;
}
