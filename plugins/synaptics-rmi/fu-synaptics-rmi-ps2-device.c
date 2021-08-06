/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (c) 2020 Synaptics Incorporated.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-synaptics-rmi-ps2-device.h"
#include "fu-synaptics-rmi-v5-device.h"
#include "fu-synaptics-rmi-v7-device.h"

struct _FuSynapticsRmiPs2Device {
	FuSynapticsRmiDevice	 parent_instance;
	FuIOChannel		*io_channel;
};

G_DEFINE_TYPE (FuSynapticsRmiPs2Device, fu_synaptics_rmi_ps2_device, FU_TYPE_SYNAPTICS_RMI_DEVICE)

enum EPS2DataPortCommand {
	edpAuxFullRMIBackDoor		= 0x7F,
	edpAuxAccessModeByte1		= 0xE0,
	edpAuxAccessModeByte2		= 0xE1,
	edpAuxIBMReadSecondaryID	= 0xE1,
	edpAuxSetScaling1To1		= 0xE6,
	edpAuxSetScaling2To1		= 0xE7,
	edpAuxSetResolution		= 0xE8,
	edpAuxStatusRequest		= 0xE9,
	edpAuxSetStreamMode		= 0xEA,
	edpAuxReadData			= 0xEB,
	edpAuxResetWrapMode		= 0xEC,
	edpAuxSetWrapMode		= 0xEE,
	edpAuxSetRemoteMode		= 0xF0,
	edpAuxReadDeviceType		= 0xF2,
	edpAuxSetSampleRate		= 0xF3,
	edpAuxEnable			= 0xF4,
	edpAuxDisable			= 0xF5,
	edpAuxSetDefault		= 0xF6,
	edpAuxResend			= 0xFE,
	edpAuxReset			= 0xFF,
};

typedef enum {
	esdrTouchPad			= 0x47,
	esdrStyk			= 0x46,
	esdrControlBar			= 0x44,
	esdrRGBControlBar		= 0x43,
} ESynapticsDeviceResponse;

enum EStatusRequestSequence {
	esrIdentifySynaptics		= 0x00,
	esrReadTouchPadModes		= 0x01,
	esrReadModeByte			= 0x01,
	esrReadEdgeMargins		= 0x02,
	esrReadCapabilities		= 0x02,
	esrReadModelID			= 0x03,
	esrReadCompilationDate		= 0x04,
	esrReadSerialNumberPrefix	= 0x06,
	esrReadSerialNumberSuffix	= 0x07,
	esrReadResolutions		= 0x08,
	esrReadExtraCapabilities1	= 0x09,
	esrReadExtraCapabilities2	= 0x0A,
	esrReadExtraCapabilities3	= 0x0B,
	esrReadExtraCapabilities4	= 0x0C,
	esrReadExtraCapabilities5	= 0x0D,
	esrReadCoordinates		= 0x0D,
	esrReadExtraCapabilities6	= 0x0E,
	esrReadExtraCapabilities7	= 0x0F,
};

enum EPS2DataPortStatus {
	edpsAcknowledge			= 0xFA,
	edpsError			= 0xFC,
	edpsResend			= 0xFE,
	edpsTimeOut			= 0x100
};

enum ESetSampleRateSequence {
	essrSetModeByte1		= 0x0A,
	essrSetModeByte2		= 0x14,
	essrSetModeByte3		= 0x28,
	essrSetModeByte4		= 0x3C,
	essrSetDeluxeModeByte1		= 0x0A,
	essrSetDeluxeModeByte2		= 0x3C,
	essrSetDeluxeModeByte3		= 0xC8,
	essrFastRecalibrate		= 0x50,
	essrPassThroughCommandTunnel	= 0x28
};

enum EDeviceType {
	edtUnknown,
	edtTouchPad,
};

enum EStickDeviceType {
	esdtNone			= 0,
	esdtIBM,
	esdtJYTSyna			= 5,
	esdtSynaptics			= 6,
	esdtUnknown			= 0xFFFFFFFF
};

static gboolean
fu_synaptics_rmi_ps2_device_read_ack (FuSynapticsRmiPs2Device *self,
				      guint8 *pbuf,
				      GError **error)
{
	for(guint i = 0 ; i < 60; i++) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_io_channel_read_raw (self->io_channel, pbuf, 0x1,
					     NULL, 10,
					     FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
					     &error_local)) {
			if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
				g_warning ("read timed out: %u", i);
				g_usleep (30);
				continue;
			}
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
		return TRUE;
	}
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "read timed out");
	return FALSE;
}

/* read a single byte from the touchpad */
static gboolean
fu_synaptics_rmi_ps2_device_read_byte (FuSynapticsRmiPs2Device *self,
				       guint8 *pbuf,
				       guint timeout,
				       GError **error)
{
	g_return_val_if_fail (timeout > 0, FALSE);
	return fu_io_channel_read_raw (self->io_channel, pbuf, 0x1,
				       NULL, timeout,
				       FU_IO_CHANNEL_FLAG_NONE,
				       error);
}

/* write a single byte to the touchpad and the read the acknowledge */
static gboolean
fu_synaptics_rmi_ps2_device_write_byte (FuSynapticsRmiPs2Device *self,
					guint8 buf,
					guint timeout,
					FuSynapticsRmiDeviceFlags flags,
					GError **error)
{
	gboolean do_write = TRUE;
	g_return_val_if_fail (timeout > 0, FALSE);
	for (guint i = 0; ; i++) {
		guint8 res = 0;
		g_autoptr(GError) error_local = NULL;
		if (do_write) {
			if (!fu_io_channel_write_raw (self->io_channel,
						      &buf, sizeof(buf),
						      timeout,
						      FU_IO_CHANNEL_FLAG_FLUSH_INPUT |
						      FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO,
						      error))
				return FALSE;
		}
		do_write = FALSE;

		for (;;) {
			/* attempt to read acknowledge... */
			if (!fu_synaptics_rmi_ps2_device_read_ack (self, &res, &error_local)) {
				if (i > 3) {
					g_propagate_prefixed_error (error,
							    g_steal_pointer (&error_local),
							    "read ack failed: ");
					return FALSE;
				}
				g_warning ("read ack failed: %s, retrying", error_local->message);
				break;
			}
			if (res == edpsAcknowledge)
				return TRUE;
			if (res == edpsResend) {
				do_write = TRUE;
				g_debug ("resend");
				g_usleep (G_USEC_PER_SEC);
				break;
			}
			if (res == edpsError) {
				do_write = TRUE;
				g_debug ("error");
				g_usleep (1000 * 10);
				break;
			}
			g_debug ("other response: 0x%x", res);
			g_usleep (1000 * 10);
		}
		if (i >= 3) {
			if (flags & FU_SYNAPTICS_RMI_DEVICE_FLAG_ALLOW_FAILURE) {
				/* just break without error return because FW
				 * will not return ACK for commands like RESET */
				break;
			}
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "cannot write byte after retries");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_set_resolution_sequence (FuSynapticsRmiPs2Device *self,
						     guint8 arg,
						     gboolean send_e6s,
						     GError **error)
{
	/* send set scaling twice if send_e6s */
	for (gint i = send_e6s ? 2 : 1; i > 0; --i) {
		if (!fu_synaptics_rmi_ps2_device_write_byte (self, edpAuxSetScaling1To1, 50,
							     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
							     error))
			return FALSE;
	}
	for (gint i = 3; i >= 0; --i) {
		guint8 ucTwoBitArg = (arg >> (i * 2)) & 0x3;
		if (!fu_synaptics_rmi_ps2_device_write_byte (self, edpAuxSetResolution, 50,
							     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
							     error)) {
			return FALSE;
		}
		if (!fu_synaptics_rmi_ps2_device_write_byte (self, ucTwoBitArg, 50,
							     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
							     error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_status_request_sequence (FuSynapticsRmiPs2Device *self,
						     guint8 ucArgument,
						     guint32 *buf,
						     GError **error)
{
	gboolean success = FALSE;

	/* allow 3 retries */
	for (guint i = 0; i < 3; ++i) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_synaptics_rmi_ps2_device_set_resolution_sequence (self,
									  ucArgument,
									  FALSE,
									  &error_local)) {
			g_debug ("failed set try #%u: %s",
				 i, error_local->message);
			continue;
		}
		if (!fu_synaptics_rmi_ps2_device_write_byte (self,
							     edpAuxStatusRequest,
							     10,
							     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
							     &error_local)) {
			g_debug ("failed write try #%u: %s",
				 i, error_local->message);
			continue;
		}
		success = TRUE;
		break;
	}
	if (success == FALSE) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "failed");
		return FALSE;
	}

	/* read the response from the status request */
	for (gint i = 0; i < 3; ++i) {
		guint8 tmp = 0x0;
		if (!fu_synaptics_rmi_ps2_device_read_byte (self, &tmp, 10, error)) {
			g_prefix_error (error, "failed to read byte: ");
			return FALSE;
		}
		*buf = ((*buf) << 8) | tmp;
	}

	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_sample_rate_sequence (FuSynapticsRmiPs2Device *self,
						  guint8 param,
						  guint8 arg,
						  gboolean send_e6s,
						  GError **error)
{
	/* allow 3 retries */
	for (guint i = 0; ; i++) {
		g_autoptr(GError) error_local = NULL;
		if (i > 0) {
			/* always send two E6s when retrying */
			send_e6s = TRUE;
		}
		if (!fu_synaptics_rmi_ps2_device_set_resolution_sequence (self, arg, send_e6s, &error_local) ||
		    !fu_synaptics_rmi_ps2_device_write_byte (self, edpAuxSetSampleRate, 50,
							     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
							     &error_local) ||
		    !fu_synaptics_rmi_ps2_device_write_byte (self, param, 50,
							     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
							     &error_local)) {
			if (i > 3) {
				g_propagate_error (error, g_steal_pointer (&error_local));
				return FALSE;
			}
			g_warning ("failed, will retry: %s", error_local->message);
			continue;
		}
		break;
	}
	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_detect_synaptics_styk (FuSynapticsRmiPs2Device *self,
						   gboolean *result,
						   GError **error)
{
	guint8 buf;
	if (!fu_synaptics_rmi_ps2_device_write_byte (self, edpAuxIBMReadSecondaryID, 10,
						     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						     error)) {
		g_prefix_error (error, "failed to write IBMReadSecondaryID(0xE1): ");
		return FALSE;
	}
	if (!fu_synaptics_rmi_ps2_device_read_byte (self, &buf, 10, error)) {
		g_prefix_error (error, "failed to receive IBMReadSecondaryID: ");
		return FALSE;
	}
	if (buf == esdtJYTSyna || buf == esdtSynaptics)
		*result = TRUE;
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_query_build_id (FuSynapticsRmiDevice *rmi_device,
					    guint32 *build_id,
					    GError **error)
{
	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (rmi_device);
	guint32 buf = 0;
	gboolean is_synaptics_styk = FALSE;
	ESynapticsDeviceResponse esdr;

	if (!fu_synaptics_rmi_ps2_device_status_request_sequence (self,
								  esrIdentifySynaptics,
								  &buf,
								  error)) {
		g_prefix_error (error, "failed to request IdentifySynaptics: ");
		return FALSE;
	}
	g_debug ("identify Synaptics response = 0x%x", buf);

	esdr = (buf & 0xFF00) >> 8;
	if (!fu_synaptics_rmi_ps2_device_detect_synaptics_styk (self,
								&is_synaptics_styk,
								error)) {
		g_prefix_error (error, "failed to detect Synaptics styk: ");
		return FALSE;
	}
	fu_synaptics_rmi_device_set_iepmode (rmi_device, FALSE);
	if (esdr == esdrTouchPad || is_synaptics_styk) {
		/* Get the firmware id from the Extra Capabilities 2 Byte
		 * The firmware id is located in bits 0 - 23 */
		if (!fu_synaptics_rmi_ps2_device_status_request_sequence (self,
									  esrReadExtraCapabilities2,
									  build_id,
									  error)) {
			g_prefix_error (error, "failed to read extraCapabilities2: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_query_product_sub_id (FuSynapticsRmiDevice *rmi_device,
						  guint8 *sub_id,
						  GError **error)
{
	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (rmi_device);
	guint32 buf = 0;
	if (!fu_synaptics_rmi_ps2_device_status_request_sequence (self, esrReadCapabilities, &buf, error)) {
		g_prefix_error (error, "failed to status_request_sequence read esrReadCapabilities: ");
		return FALSE;
	}
	*sub_id = (buf >> 8) & 0xFF;
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_enter_iep_mode (FuSynapticsRmiDevice *rmi_device,
					    GError **error)
{
	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (rmi_device);

	/* disable stream */
	if (!fu_synaptics_rmi_ps2_device_write_byte (self, edpAuxDisable, 50,
						     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						     error)) {
		g_prefix_error (error, "failed to disable stream mode: ");
		return FALSE;
	}

	/* enable RMI mode */
	if (!fu_synaptics_rmi_ps2_device_sample_rate_sequence (self,
							       essrSetModeByte2,
							       edpAuxFullRMIBackDoor,
							       FALSE,
							       error)) {
		g_prefix_error (error, "failed to enter RMI mode: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_write_rmi_register (FuSynapticsRmiPs2Device *self,
						guint8 addr,
						const guint8 *buf,
						guint8 buflen,
						guint timeout,
						FuSynapticsRmiDeviceFlags flags,
						GError **error)
{
	g_return_val_if_fail (timeout > 0, FALSE);

	if (!fu_synaptics_rmi_device_enter_iep_mode (FU_SYNAPTICS_RMI_DEVICE (self),
						     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						     error))
		return FALSE;
	if (!fu_synaptics_rmi_ps2_device_write_byte (self,
						     edpAuxSetScaling2To1,
						     timeout,
						     flags,
						     error)) {
		g_prefix_error (error, "failed to edpAuxSetScaling2To1: ");
		return FALSE;
	}
	if (!fu_synaptics_rmi_ps2_device_write_byte (self,
						     edpAuxSetSampleRate,
						     timeout,
						     flags,
						     error)) {
		g_prefix_error (error, "failed to edpAuxSetSampleRate: ");
		return FALSE;
	}
	if (!fu_synaptics_rmi_ps2_device_write_byte (self,
						     addr,
						     timeout,
						     flags,
						     error)) {
		g_prefix_error (error, "failed to write address: ");
		return FALSE;
	}
	for (guint8 i = 0; i < buflen; i++) {
		if (!fu_synaptics_rmi_ps2_device_write_byte (self,
							     edpAuxSetSampleRate,
							     timeout,
							     flags,
							     error)) {
			g_prefix_error (error, "failed to set byte %u: ", i);
			return FALSE;
		}
		if (!fu_synaptics_rmi_ps2_device_write_byte (self,
							     buf[i],
							     timeout,
							     flags,
							     error)) {
			g_prefix_error (error, "failed to write byte %u: ", i);
			return FALSE;
		}
	}

	/* success */
	g_usleep (1000 * 20);
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_read_rmi_register (FuSynapticsRmiPs2Device *self,
					       guint8 addr,
					       guint8 *buf,
					       GError **error)
{
	g_return_val_if_fail (buf != NULL, FALSE);

	if (!fu_synaptics_rmi_device_enter_iep_mode (FU_SYNAPTICS_RMI_DEVICE (self),
						     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						     error))
		return FALSE;
	for (guint retries = 0; ; retries++) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_synaptics_rmi_ps2_device_write_byte (self, edpAuxSetScaling2To1, 50,
							     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
							     error) ||
		    !fu_synaptics_rmi_ps2_device_write_byte (self, edpAuxSetSampleRate, 50,
							     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
							     error) ||
		    !fu_synaptics_rmi_ps2_device_write_byte (self, addr, 50,
							     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
							     error) ||
		    !fu_synaptics_rmi_ps2_device_write_byte (self, edpAuxStatusRequest, 50,
							     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
							     error)) {
			g_prefix_error (error, "failed to write command in Read RMI register: ");
			return FALSE;
		}
		if (!fu_synaptics_rmi_ps2_device_read_byte (self, buf, 10, &error_local)) {
			if (retries++ > 2) {
				g_propagate_prefixed_error (error,
							    g_steal_pointer (&error_local),
							    "failed to read byte @0x%x after %u retries: ",
							    addr, retries);
				return FALSE;
			}
			g_debug ("failed to read byte @0x%x: %s",
				 addr,
				 error_local->message);
			continue;
		}

		/* success */
		break;
	}

	/* success */
	g_usleep (1000 * 20);
	return TRUE;
}

static GByteArray *
fu_synaptics_rmi_ps2_device_read_rmi_packet_register (FuSynapticsRmiPs2Device *self,
						      guint8 addr,
						      guint req_sz,
						      GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new ();

	if (!fu_synaptics_rmi_device_enter_iep_mode (FU_SYNAPTICS_RMI_DEVICE (self),
						     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						     error))
		return NULL;
	if (!fu_synaptics_rmi_ps2_device_write_byte (self, edpAuxSetScaling2To1, 50,
						     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						     error) ||
	    !fu_synaptics_rmi_ps2_device_write_byte (self, edpAuxSetSampleRate, 50,
						     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						     error) ||
	    !fu_synaptics_rmi_ps2_device_write_byte (self, addr, 50,
						     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						     error) ||
	    !fu_synaptics_rmi_ps2_device_write_byte (self, edpAuxStatusRequest, 50,
						     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						     error)) {
		g_prefix_error (error, "failed to write command in Read RMI Packet Register: ");
		return NULL;
	}
	for (guint i = 0; i < req_sz; ++i) {
		guint8 tmp = 0;
		if (!fu_synaptics_rmi_ps2_device_read_byte (self, &tmp, 10, error)) {
			g_prefix_error (error, "failed to read byte %u: ", i);
			return NULL;
		}
		fu_byte_array_append_uint8 (buf, tmp);
	}

	g_usleep (1000 * 20);
	return g_steal_pointer (&buf);
}

static gboolean
fu_synaptics_rmi_ps2_device_query_status (FuSynapticsRmiDevice *rmi_device,
					  GError **error)
{
	FuSynapticsRmiFunction *f34;
	g_debug ("ps2 query status");
	f34 = fu_synaptics_rmi_device_get_function (rmi_device, 0x34, error);
	if (f34 == NULL)
		return FALSE;
	if (f34->function_version == 0x0 ||
	    f34->function_version == 0x1) {
		return fu_synaptics_rmi_v5_device_query_status (rmi_device, error);
	}
	if (f34->function_version == 0x2) {
		return fu_synaptics_rmi_v7_device_query_status (rmi_device, error);
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "f34 function version 0x%02x unsupported",
		     f34->function_version);
	return FALSE;
}

static gboolean
fu_synaptics_rmi_ps2_device_set_page (FuSynapticsRmiDevice *rmi_device,
				      guint8 page,
				      GError **error)
{
	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (rmi_device);
	if (!fu_synaptics_rmi_ps2_device_write_rmi_register (self,
							     RMI_DEVICE_PAGE_SELECT_REGISTER,
							     &page,
							     1,
							     20,
							     FALSE,
							     error)) {
		g_prefix_error (error, "failed to write page %u: ", page);
		return FALSE;
	}
	return TRUE;
}

static GByteArray *
fu_synaptics_rmi_ps2_device_read (FuSynapticsRmiDevice *rmi_device,
				  guint16 addr,
				  gsize req_sz,
				  GError **error)
{
	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (rmi_device);
	g_autoptr(GByteArray) buf = NULL;
	g_autofree gchar *dump = NULL;

	if (!fu_synaptics_rmi_device_set_page (rmi_device,
					       addr >> 8,
					       error)) {
		g_prefix_error (error, "failed to set RMI page:");
		return NULL;
	}

	for (guint retries = 0; ; retries++) {
		buf = g_byte_array_new ();
		for (guint i = 0; i < req_sz; i++) {
			guint8 tmp = 0x0;
			if (!fu_synaptics_rmi_ps2_device_read_rmi_register (self,
									    (guint8) ((addr & 0x00FF) + i),
									    &tmp,
									    error)) {
				g_prefix_error (error,
						"failed register read 0x%x: ",
						addr + i);
				return NULL;
			}
			fu_byte_array_append_uint8 (buf, tmp);
		}
		if (buf->len != req_sz) {
			g_debug ("buf->len(%u) != req_sz(%u)", buf->len, (guint) req_sz);
			if (retries++ > 2) {
				g_set_error (error,
					     G_IO_ERROR, G_IO_ERROR_FAILED,
					     "buffer length did not match: %u vs %u",
					     buf->len, (guint) req_sz);
				return NULL;
			}
			continue;
		}
		break;
	}
	dump = g_strdup_printf ("R %x", addr);
	if (g_getenv ("FWUPD_SYNAPTICS_RMI_VERBOSE") != NULL) {
		fu_common_dump_full (G_LOG_DOMAIN, dump,
				     buf->data, buf->len,
				     80, FU_DUMP_FLAGS_NONE);
	}
	return g_steal_pointer (&buf);
}

static GByteArray *
fu_synaptics_rmi_ps2_device_read_packet_register (FuSynapticsRmiDevice *rmi_device,
						  guint16 addr,
						  gsize req_sz,
						  GError **error)
{
	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (rmi_device);
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_synaptics_rmi_device_set_page (rmi_device,
					       addr >> 8,
					       error)) {
		g_prefix_error (error, "failed to set RMI page:");
		return NULL;
	}

	buf = fu_synaptics_rmi_ps2_device_read_rmi_packet_register (self,
								    addr,
								    req_sz,
								    error);
	if (buf == NULL) {
		g_prefix_error (error,
				"failed packet register read %x: ",
				addr);
		return NULL;
	}

	if (g_getenv ("FWUPD_SYNAPTICS_RMI_VERBOSE") != NULL) {
		g_autofree gchar *dump = g_strdup_printf ("R %x", addr);
		fu_common_dump_full (G_LOG_DOMAIN, dump,
				     buf->data, buf->len,
				     80, FU_DUMP_FLAGS_NONE);
	}
	return g_steal_pointer (&buf);
}

static gboolean
fu_synaptics_rmi_ps2_device_write (FuSynapticsRmiDevice *rmi_device,
				   guint16 addr,
				   GByteArray *req,
				   FuSynapticsRmiDeviceFlags flags,
				   GError **error)
{
	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (rmi_device);
	if (!fu_synaptics_rmi_device_set_page (rmi_device,
					       addr >> 8,
					       error)) {
		g_prefix_error (error, "failed to set RMI page: ");
		return FALSE;
	}
	if (!fu_synaptics_rmi_ps2_device_write_rmi_register (self,
							     addr & 0x00FF,
							     req->data,
							     req->len,
							     1000, /* timeout */
							     flags,
							     error)) {
		g_prefix_error (error,
				"failed to write register %x: ",
				addr);
		return FALSE;
	}
	if (g_getenv ("FWUPD_SYNAPTICS_RMI_VERBOSE") != NULL) {
		g_autofree gchar *str = g_strdup_printf ("W %x", addr);
		fu_common_dump_full (G_LOG_DOMAIN, str,
				     req->data, req->len,
				     80, FU_DUMP_FLAGS_NONE);
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_write_bus_select (FuSynapticsRmiDevice *rmi_device,
					      guint8 bus,
					      GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new ();
	fu_byte_array_append_uint8 (req, bus);
	if (!fu_synaptics_rmi_ps2_device_write (rmi_device,
						RMI_DEVICE_BUS_SELECT_REGISTER,
						req,
						FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						error)) {
		g_prefix_error (error, "failed to write rmi register %u: ", bus);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_probe (FuDevice *device, GError **error)
{
	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS (fu_synaptics_rmi_ps2_device_parent_class)->probe (device, error))
		return FALSE;

	/* psmouse is the usual mode, but serio is needed for update */
	if (g_strcmp0 (fu_udev_device_get_driver (FU_UDEV_DEVICE (device)), "serio_raw") == 0) {
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* set the physical ID */
	return fu_udev_device_set_physical_id (FU_UDEV_DEVICE (device), "platform", error);
}

static gboolean
fu_synaptics_rmi_ps2_device_open (FuDevice *device, GError **error)
{
	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (device);
	guint8 buf[2] = { 0x0 };

	/* FuUdevDevice->open */
	if (!FU_DEVICE_CLASS (fu_synaptics_rmi_ps2_device_parent_class)->open (device, error))
		return FALSE;

	/* create channel */
	self->io_channel = fu_io_channel_unix_new (fu_udev_device_get_fd (FU_UDEV_DEVICE (device)));

	/* in serio_raw mode */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {

		/* clear out any data in the serio_raw queue */
		for(guint i = 0; i < 0xffff; i++) {
			guint8 tmp = 0;
			if (!fu_synaptics_rmi_ps2_device_read_byte (self, &tmp, 20, NULL))
				break;
		}

		/* send reset -- may take 300-500ms */
		if (!fu_synaptics_rmi_ps2_device_write_byte (self, edpAuxReset, 600,
							     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
							     error)) {
			g_prefix_error (error, "failed to reset: ");
			return FALSE;
		}

		/* read the 0xAA 0x00 announcing the touchpad is ready */
		if (!fu_synaptics_rmi_ps2_device_read_byte(self, &buf[0], 500, error) ||
		    !fu_synaptics_rmi_ps2_device_read_byte(self, &buf[1], 500, error)) {
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
		if (!fu_synaptics_rmi_ps2_device_write_byte (self, edpAuxDisable, 50,
							     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
							     error)) {
			g_prefix_error (error, "failed to disable stream mode: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_close (FuDevice *device, GError **error)
{
	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (device);
	fu_udev_device_set_fd (FU_UDEV_DEVICE (device), -1);
	g_clear_object (&self->io_channel);

	/* FuUdevDevice->close */
	return FU_DEVICE_CLASS (fu_synaptics_rmi_ps2_device_parent_class)->close (device, error);}

static gboolean
fu_synaptics_rmi_ps2_device_detach (FuDevice *device, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiFunction *f34;

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

	f34 = fu_synaptics_rmi_device_get_function (self, 0x34, error);
	if (f34 == NULL)
		return FALSE;
	if (f34->function_version == 0x0 ||
	    f34->function_version == 0x1) {
		if (!fu_synaptics_rmi_v5_device_detach (device, error))
			return FALSE;
	} else if (f34->function_version == 0x2) {
		if (!fu_synaptics_rmi_v7_device_detach (device, error))
			return FALSE;
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "f34 function version 0x%02x unsupported",
			     f34->function_version);
		return FALSE;
	}

	/* set iepmode before querying device forcibly because of FW requirement */
	if (!fu_synaptics_rmi_device_enter_iep_mode (self,
						     FU_SYNAPTICS_RMI_DEVICE_FLAG_FORCE,
						     error))
		return FALSE;

	if (!fu_synaptics_rmi_ps2_device_query_status (self, error)) {
		g_prefix_error (error, "failed to query status after detach: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_setup (FuDevice *device, GError **error)
{
	/* we can only scan the PDT in serio_raw mode */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;
	return FU_DEVICE_CLASS (fu_synaptics_rmi_ps2_device_parent_class)->setup (device, error);
}

static gboolean
fu_synaptics_rmi_ps2_device_attach (FuDevice *device, GError **error)
{
	FuSynapticsRmiDevice *rmi_device = FU_SYNAPTICS_RMI_DEVICE (device);
	g_autoptr(FuProgress) progress = fu_progress_new();

	/* sanity check */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("already in runtime mode, skipping");
		return TRUE;
	}

	/* set iepmode before reset device forcibly because of FW requirement */
	fu_synaptics_rmi_device_set_iepmode (rmi_device, FALSE);

	/* delay after writing */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_progress_sleep(progress, 2);

	/* reset device */
	if (!fu_synaptics_rmi_device_enter_iep_mode (rmi_device,
						     FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						     error))
		return FALSE;
	if (!fu_synaptics_rmi_device_reset (rmi_device, error)) {
		g_prefix_error (error, "failed to reset device: ");
		return FALSE;
	}

	/* delay after reset */
	fu_progress_sleep(progress, 5);

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
	fu_device_set_name (FU_DEVICE (self), "TouchStyk");
	fu_device_set_vendor (FU_DEVICE (self), "Synaptics");
	fu_device_add_vendor_id (FU_DEVICE (self), "HIDRAW:0x06CB");
	fu_synaptics_rmi_device_set_max_page (FU_SYNAPTICS_RMI_DEVICE (self), 0x1);
	fu_udev_device_set_flags (FU_UDEV_DEVICE (self),
				  FU_UDEV_DEVICE_FLAG_OPEN_READ |
				  FU_UDEV_DEVICE_FLAG_OPEN_WRITE);
}

static gboolean
fu_synaptics_rmi_ps2_device_wait_for_attr (FuSynapticsRmiDevice *rmi_device,
					   guint8 source_mask,
					   guint timeout_ms,
					   GError **error)
{
	g_usleep (1000 * timeout_ms);
	return TRUE;
}

static void
fu_synaptics_rmi_ps2_device_class_init (FuSynapticsRmiPs2DeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuSynapticsRmiDeviceClass *klass_rmi = FU_SYNAPTICS_RMI_DEVICE_CLASS (klass);
	klass_device->attach = fu_synaptics_rmi_ps2_device_attach;
	klass_device->detach = fu_synaptics_rmi_ps2_device_detach;
	klass_device->setup = fu_synaptics_rmi_ps2_device_setup;
	klass_device->probe = fu_synaptics_rmi_ps2_device_probe;
	klass_device->open = fu_synaptics_rmi_ps2_device_open;
	klass_device->close = fu_synaptics_rmi_ps2_device_close;
	klass_rmi->read = fu_synaptics_rmi_ps2_device_read;
	klass_rmi->write = fu_synaptics_rmi_ps2_device_write;
	klass_rmi->set_page = fu_synaptics_rmi_ps2_device_set_page;
	klass_rmi->query_status = fu_synaptics_rmi_ps2_device_query_status;
	klass_rmi->query_build_id = fu_synaptics_rmi_ps2_device_query_build_id;
	klass_rmi->query_product_sub_id = fu_synaptics_rmi_ps2_device_query_product_sub_id;
	klass_rmi->wait_for_attr = fu_synaptics_rmi_ps2_device_wait_for_attr;
	klass_rmi->enter_iep_mode = fu_synaptics_rmi_ps2_device_enter_iep_mode;
	klass_rmi->write_bus_select = fu_synaptics_rmi_ps2_device_write_bus_select;
	klass_rmi->read_packet_register = fu_synaptics_rmi_ps2_device_read_packet_register;
}
