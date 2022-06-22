/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <fcntl.h>
#include <glib/gstdio.h>
#include <linux/ipmi.h>
#include <linux/ipmi_msgdefs.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "fu-ipmi-device.h"

#define FU_IPMI_DEVICE_TIMEOUT 1500 /* ms */

#define FU_IPMI_TRANSACTION_RETRY_COUNT 5
#define FU_IPMI_TRANSACTION_RETRY_DELAY 200 /* ms */

/* not defined in linux/ipmi_msgdefs.h */
#define IPMI_SET_USER_ACCESS   0x43
#define IPMI_SET_USER_NAME     0x45
#define IPMI_GET_USER_NAME     0x46
#define IPMI_SET_USER_PASSWORD 0x47

#define IPMI_PASSWORD_DISABLE_USER  0x00
#define IPMI_PASSWORD_ENABLE_USER   0x01
#define IPMI_PASSWORD_SET_PASSWORD  0x02
#define IPMI_PASSWORD_TEST_PASSWORD 0x03

/* these are not provided in ipmi_msgdefs.h */
#define IPMI_INVALID_COMMAND_ON_LUN_ERR	 0xC2
#define IPMI_OUT_OF_SPACE_ERR		 0xC4
#define IPMI_CANCELLED_OR_INVALID_ERR	 0xC5
#define IPMI_OUT_OF_RANGE_ERR		 0xC9
#define IPMI_CANNOT_RETURN_DATA_ERR	 0xCA
#define IPMI_NOT_FOUND_ERR		 0xCB
#define IPMI_INVALID_DATA_FIELD_ERR	 0xCC
#define IPMI_COMMAND_ILLEGAL_ERR	 0xCD
#define IPMI_RESPONSE_NOT_PROVIDED_ERR	 0xCE
#define IPMI_DUPLICATED_REQUEST_ERR	 0xCF
#define IPMI_SDR_IN_UPDATE_MODE_ERR	 0xD0
#define IPMI_DESTINATION_UNAVAILABLE_ERR 0xD3
#define IPMI_INSUFFICIENT_PRIVILEGE_ERR	 0xD4
#define IPMI_COMMAND_DISABLED_ERR	 0xD6

#ifndef IPMI_DEVICE_IN_UPDATE_MODE_ERR
#define IPMI_DEVICE_IN_UPDATE_MODE_ERR 0xD1
#endif
#ifndef IPMI_DEVICE_IN_INIT_ERR
#define IPMI_DEVICE_IN_INIT_ERR 0xD2
#endif

struct _FuIpmiDevice {
	FuUdevDevice parent_instance;
	glong seq;
	guint8 device_id;
	guint8 device_rev;
	guint8 version_ipmi;
};

G_DEFINE_TYPE(FuIpmiDevice, fu_ipmi_device, FU_TYPE_UDEV_DEVICE)

#define FU_IPMI_DEVICE_IOCTL_TIMEOUT 5000 /* ms */

static void
fu_ipmi_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuIpmiDevice *self = FU_IPMI_DEVICE(device);
	fu_string_append_kx(str, idt, "DeviceId", self->device_id);
	fu_string_append_kx(str, idt, "DeviceRev", self->device_rev);
	fu_string_append_kx(str, idt, "VersionIpmi", self->version_ipmi);
}

static gboolean
fu_ipmi_device_send(FuIpmiDevice *self,
		    guint8 netfn,
		    guint8 cmd,
		    const guint8 *buf,
		    gsize bufsz,
		    GError **error)
{
	g_autofree guint8 *buf2 = fu_memdup_safe(buf, bufsz, NULL);
	struct ipmi_system_interface_addr addr = {.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE,
						  .channel = IPMI_BMC_CHANNEL};
	struct ipmi_req req = {
	    .addr = (guint8 *)&addr,
	    .addr_len = sizeof(addr),
	    .msgid = self->seq++,
	    .msg.data = buf2,
	    .msg.data_len = (guint16)bufsz,
	    .msg.netfn = netfn,
	    .msg.cmd = cmd,
	};
	if (g_getenv("FWUPD_REDFISH_VERBOSE") != NULL && buf2 != NULL)
		fu_dump_raw(G_LOG_DOMAIN, "ipmi-send", buf2, bufsz);
	return fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				    IPMICTL_SEND_COMMAND,
				    (guint8 *)&req,
				    NULL,
				    FU_IPMI_DEVICE_IOCTL_TIMEOUT,
				    error);
}

static gboolean
fu_ipmi_device_recv(FuIpmiDevice *self,
		    guint8 *netfn,
		    guint8 *cmd,
		    glong *seq,
		    guint8 *buf,
		    gsize bufsz,
		    gsize *len, /* optional, out */
		    GError **error)
{
	struct ipmi_addr addr = {0};
	struct ipmi_recv recv = {
	    .addr = (guint8 *)&addr,
	    .addr_len = sizeof(addr),
	    .msg.data = buf,
	    .msg.data_len = bufsz,
	};
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  IPMICTL_RECEIVE_MSG_TRUNC,
				  (guint8 *)&recv,
				  NULL,
				  FU_IPMI_DEVICE_IOCTL_TIMEOUT,
				  error))
		return FALSE;
	if (g_getenv("FWUPD_REDFISH_VERBOSE") != NULL && buf != NULL)
		fu_dump_raw(G_LOG_DOMAIN, "ipmi-recv", buf, bufsz);
	if (netfn != NULL)
		*netfn = recv.msg.netfn;
	if (cmd != NULL)
		*cmd = recv.msg.cmd;
	if (seq != NULL)
		*seq = recv.msgid;
	if (len != NULL)
		*len = (gsize)recv.msg.data_len;
	return TRUE;
}

static gboolean
fu_ipmi_device_lock(GObject *device, GError **error)
{
	FuIpmiDevice *self = FU_IPMI_DEVICE(device);
	struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
	if (fcntl(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)), F_SETLKW, &lock) == -1) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "error locking IPMI device: %m");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ipmi_device_unlock(GObject *device, GError **error)
{
	FuIpmiDevice *self = FU_IPMI_DEVICE(device);
	struct flock lock = {.l_type = F_UNLCK};
	if (fcntl(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)), F_SETLKW, &lock) == -1) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "error unlocking IPMI device: %m");
		return FALSE;
	}
	return TRUE;
}

static const gchar *
fu_ipmi_device_errcode_to_string(guint8 errcode)
{
	if (errcode == IPMI_CC_NO_ERROR)
		return "no-error";
	if (errcode == IPMI_NODE_BUSY_ERR)
		return "node-busy";
	if (errcode == IPMI_INVALID_COMMAND_ERR)
		return "invalid-command";
	if (errcode == IPMI_TIMEOUT_ERR)
		return "timeout";
	if (errcode == IPMI_ERR_MSG_TRUNCATED)
		return "msg-truncated";
	if (errcode == IPMI_REQ_LEN_INVALID_ERR)
		return "req-len-invalid";
	if (errcode == IPMI_REQ_LEN_EXCEEDED_ERR)
		return "req-len-exceeded";
	if (errcode == IPMI_DEVICE_IN_UPDATE_MODE_ERR)
		return "device-in-update-mode";
	if (errcode == IPMI_DEVICE_IN_INIT_ERR)
		return "device-in-init";
	if (errcode == IPMI_NOT_IN_MY_STATE_ERR)
		return "not-in-my-state";
	if (errcode == IPMI_LOST_ARBITRATION_ERR)
		return "lost-arbitration";
	if (errcode == IPMI_BUS_ERR)
		return "bus-error";
	if (errcode == IPMI_NAK_ON_WRITE_ERR)
		return "nak-on-write";
	if (errcode == IPMI_ERR_UNSPECIFIED)
		return "unspecified";
	/* these are not defined in ipmi_msgdefs.h but used in reality */
	if (errcode == IPMI_INVALID_COMMAND_ON_LUN_ERR)
		return "invalid-command-on-lun";
	if (errcode == IPMI_OUT_OF_SPACE_ERR)
		return "out-of-space";
	if (errcode == IPMI_CANCELLED_OR_INVALID_ERR)
		return "cancelled-or-invalid";
	if (errcode == IPMI_OUT_OF_RANGE_ERR)
		return "out-of-range";
	if (errcode == IPMI_CANNOT_RETURN_DATA_ERR)
		return "cannot-return-data";
	if (errcode == IPMI_NOT_FOUND_ERR)
		return "not-found";
	if (errcode == IPMI_INVALID_DATA_FIELD_ERR)
		return "invalid-data-field";
	if (errcode == IPMI_COMMAND_ILLEGAL_ERR)
		return "command-illegal";
	if (errcode == IPMI_RESPONSE_NOT_PROVIDED_ERR)
		return "response-not-provided";
	if (errcode == IPMI_DUPLICATED_REQUEST_ERR)
		return "duplicated-request";
	if (errcode == IPMI_SDR_IN_UPDATE_MODE_ERR)
		return "sdr-in-update-mode";
	if (errcode == IPMI_DESTINATION_UNAVAILABLE_ERR)
		return "destination-unavailable";
	if (errcode == IPMI_INSUFFICIENT_PRIVILEGE_ERR)
		return "insufficient-privilege";
	if (errcode == IPMI_COMMAND_DISABLED_ERR)
		return "command-disabled";
	return "unknown";
}

static gboolean
fu_ipmi_device_errcode_to_error(guint8 errcode, GError **error)
{
	/* success */
	if (errcode == IPMI_CC_NO_ERROR)
		return TRUE;

	/* data not found, seemingly Lenovo specific */
	if (errcode == IPMI_INVALID_DATA_FIELD_ERR || errcode == IPMI_NOT_FOUND_ERR) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_FOUND,
			    "CC error: %s [0x%02X]",
			    fu_ipmi_device_errcode_to_string(errcode),
			    errcode);
		return FALSE;
	}

	/* fallback */
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_FAILED,
		    "CC error: %s [0x%02X]",
		    fu_ipmi_device_errcode_to_string(errcode),
		    errcode);
	return FALSE;
}

typedef struct {
	guint8 netfn;
	guint8 cmd;
	const guint8 *req_buf;
	gsize req_bufsz;
	guint8 *resp_buf;
	gsize resp_bufsz;
	gsize *resp_len;
	gint timeout_ms;
} FuIpmiDeviceTransactionHelper;

static gboolean
fu_ipmi_device_transaction_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuIpmiDevice *self = FU_IPMI_DEVICE(device);
	FuIpmiDeviceTransactionHelper *helper = (FuIpmiDeviceTransactionHelper *)user_data;
	GPollFD pollfds[1];
	gsize resp_buf2sz = helper->resp_bufsz + 1;
	gsize resp_len2 = 0;
	g_autoptr(GTimer) timer = g_timer_new();
	g_autoptr(FuDeviceLocker) lock = NULL;
	g_autofree guint8 *resp_buf2 = g_malloc0(resp_buf2sz);

	lock = fu_device_locker_new_full(self, fu_ipmi_device_lock, fu_ipmi_device_unlock, error);
	if (lock == NULL)
		return FALSE;

	if (!fu_ipmi_device_send(self,
				 helper->netfn,
				 helper->cmd,
				 helper->req_buf,
				 helper->req_bufsz,
				 error))
		return FALSE;

	pollfds[0].fd = fu_udev_device_get_fd(FU_UDEV_DEVICE(self));
	pollfds[0].events = POLLIN;

	for (;;) {
		guint8 resp_netfn = 0;
		guint8 resp_cmd = 0;
		glong seq = 0;
		gint rc;

		rc = g_poll(pollfds,
			    1,
			    helper->timeout_ms - (g_timer_elapsed(timer, NULL) * 1000.f));
		if (rc < 0) {
			g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "poll() error %m");
			return FALSE;
		}
		if (rc == 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "timeout waiting for response "
				    "(netfn %d, cmd %d)",
				    helper->netfn,
				    helper->cmd);
			return FALSE;
		}

		if (!(pollfds[0].revents & POLLIN)) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_FAILED,
					    "unexpected status");
			return FALSE;
		}

		if (!fu_ipmi_device_recv(self,
					 &resp_netfn,
					 &resp_cmd,
					 &seq,
					 resp_buf2,
					 resp_buf2sz,
					 &resp_len2,
					 error))
			return FALSE;

		if (seq != self->seq - 1) {
			g_debug("out-of-sequence reply: "
				"expected %ld, got %ld",
				self->seq,
				seq);
			if (g_timer_elapsed(timer, NULL) * 1000.f >= helper->timeout_ms) {
				g_set_error_literal(error,
						    G_IO_ERROR,
						    G_IO_ERROR_FAILED,
						    "timed out");
				return FALSE;
			}
		} else {
			if (!fu_ipmi_device_errcode_to_error(resp_buf2[0], error))
				return FALSE;
			if (helper->resp_buf != NULL) {
				if (!fu_memcpy_safe(helper->resp_buf,
						    helper->resp_bufsz,
						    0x0, /* dst */
						    resp_buf2,
						    resp_buf2sz,
						    0x01, /* src */
						    helper->resp_bufsz,
						    error))
					return FALSE;
			}
			if (helper->resp_len != NULL)
				*helper->resp_len = resp_len2 - 1;
			if (g_getenv("FWUPD_REDFISH_VERBOSE") != NULL) {
				g_debug("IPMI netfn: %02x->%02x, cmd: %02x->%02x",
					helper->netfn,
					resp_netfn,
					helper->cmd,
					resp_cmd);
			}
			break;
		}
	}
	return TRUE;
}

static gboolean
fu_ipmi_device_transaction(FuIpmiDevice *self,
			   guint8 netfn,
			   guint8 cmd,
			   const guint8 *req_buf,
			   gsize req_bufsz,
			   guint8 *resp_buf, /* optional */
			   gsize resp_bufsz,
			   gsize *resp_len, /* optional, out */
			   gint timeout_ms,
			   GError **error)
{
	FuIpmiDeviceTransactionHelper helper = {
	    .netfn = netfn,
	    .cmd = cmd,
	    .req_buf = req_buf,
	    .req_bufsz = req_bufsz,
	    .resp_buf = resp_buf,
	    .resp_bufsz = resp_bufsz,
	    .resp_len = resp_len,
	    .timeout_ms = timeout_ms,
	};
	fu_device_retry_add_recovery(FU_DEVICE(self), G_IO_ERROR, G_IO_ERROR_NOT_FOUND, NULL);
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_ipmi_device_transaction_cb,
				    FU_IPMI_TRANSACTION_RETRY_COUNT,
				    FU_IPMI_TRANSACTION_RETRY_DELAY,
				    &helper,
				    error);
}

static gboolean
fu_ipmi_device_probe(FuDevice *device, GError **error)
{
	FuIpmiDevice *self = FU_IPMI_DEVICE(device);
	const gchar *physical_ids[] = {"/dev/ipmi0", "/dev/ipmi/0", "/dev/ipmidev/0", NULL};

	/* look for the IPMI device */
	for (guint i = 0; physical_ids[i] != NULL; i++) {
		if (g_file_test(physical_ids[i], G_FILE_TEST_EXISTS)) {
			fu_device_set_physical_id(FU_DEVICE(self), physical_ids[i]);
			return TRUE;
		}
	}

	/* cannot continue */
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no BMC device found");
	return FALSE;
}

static gboolean
fu_ipmi_device_setup(FuDevice *device, GError **error)
{
	FuIpmiDevice *self = FU_IPMI_DEVICE(device);
	gsize resp_len = 0;
	guint8 resp[16] = {0};

	/* get IPMI versions */
	if (!fu_ipmi_device_transaction(self,
					IPMI_NETFN_APP_REQUEST,
					IPMI_GET_DEVICE_ID_CMD,
					NULL,
					0,
					resp,
					sizeof(resp),
					&resp_len,
					FU_IPMI_DEVICE_TIMEOUT,
					error))
		return FALSE;
	if (resp_len == 11 || resp_len == 15) {
		guint8 bcd;
		g_autoptr(GString) str = g_string_new(NULL);

		self->device_id = resp[0];
		self->device_rev = resp[1];
		bcd = resp[3] & 0x0f;
		bcd += 10 * (resp[4] >> 3);
		/* rev1.rev2.aux_revision */
		g_string_append_printf(str, "%u.%02u", resp[2], bcd);
		if (resp_len == 15) {
			g_string_append_printf(str,
					       ".%02x%02x%02x%02x",
					       resp[11],
					       resp[12],
					       resp[13],
					       resp[14]);
		}
		fu_device_set_version(device, str->str);
		bcd = resp[4] & 0x0f;
		bcd += 10 * (resp[4] >> 4);
		self->version_ipmi = bcd;
	} else {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to parse DEVICE_ID_CMD response (sz: %" G_GSIZE_FORMAT ")",
			    resp_len);
		return FALSE;
	}

	/* success */
	return TRUE;
}

gchar *
fu_ipmi_device_get_user_password(FuIpmiDevice *self, guint8 user_id, GError **error)
{
	const guint8 req[1] = {user_id};
	guint8 resp[0x10] = {0};
	gsize resp_len = 0;

	g_return_val_if_fail(FU_IS_IPMI_DEVICE(self), NULL);
	g_return_val_if_fail(user_id != 0x0, NULL);

	/* run transaction */
	if (!fu_ipmi_device_transaction(self,
					IPMI_NETFN_APP_REQUEST,
					IPMI_GET_USER_NAME,
					req,
					sizeof(req),
					resp,
					sizeof(resp),
					&resp_len,
					FU_IPMI_DEVICE_TIMEOUT,
					error)) {
		g_prefix_error(error, "failed to get username: ");
		return NULL;
	}
	if (resp_len != sizeof(resp)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to retrieve username from IPMI, got 0x%x bytes",
			    (guint)resp_len);
		return NULL;
	}

	/* success */
	return fu_strsafe((const gchar *)resp, resp_len);
}

gboolean
fu_ipmi_device_set_user_name(FuIpmiDevice *self,
			     guint8 user_id,
			     const gchar *username,
			     GError **error)
{
	guint8 req[0x11] = {user_id};
	gsize username_sz;

	g_return_val_if_fail(FU_IS_IPMI_DEVICE(self), FALSE);
	g_return_val_if_fail(user_id != 0x0, FALSE);
	g_return_val_if_fail(username != NULL, FALSE);

	/* copy into buffer */
	username_sz = strlen(username);
	if (!fu_memcpy_safe(req,
			    sizeof(req),
			    0x1, /* dst */
			    (guint8 *)username,
			    username_sz,
			    0x0, /* src */
			    username_sz,
			    error)) {
		g_prefix_error(error, "username invalid: ");
		return FALSE;
	}

	/* run transaction */
	if (!fu_ipmi_device_transaction(self,
					IPMI_NETFN_APP_REQUEST,
					IPMI_SET_USER_NAME,
					req,
					sizeof(req),
					NULL, /* resp */
					0,
					NULL,
					FU_IPMI_DEVICE_TIMEOUT,
					error)) {
		g_prefix_error(error, "failed to set user %02x name: ", user_id);
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_ipmi_device_set_user_enable(FuIpmiDevice *self, guint8 user_id, gboolean value, GError **error)
{
	guint8 op = value ? IPMI_PASSWORD_ENABLE_USER : IPMI_PASSWORD_DISABLE_USER;
	const guint8 req[] = {user_id, op};

	g_return_val_if_fail(FU_IS_IPMI_DEVICE(self), FALSE);
	g_return_val_if_fail(user_id != 0x0, FALSE);

	/* run transaction */
	if (!fu_ipmi_device_transaction(self,
					IPMI_NETFN_APP_REQUEST,
					IPMI_SET_USER_PASSWORD,
					req,
					sizeof(req),
					NULL, /* resp */
					0,
					NULL,
					FU_IPMI_DEVICE_TIMEOUT,
					error)) {
		g_prefix_error(error, "failed to set user %02x enable: ", user_id);
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_ipmi_device_set_user_password(FuIpmiDevice *self,
				 guint8 user_id,
				 const gchar *password,
				 GError **error)
{
	guint8 req[0x12] = {user_id, IPMI_PASSWORD_SET_PASSWORD};
	gsize password_sz;

	g_return_val_if_fail(FU_IS_IPMI_DEVICE(self), FALSE);
	g_return_val_if_fail(user_id != 0x0, FALSE);
	g_return_val_if_fail(password != NULL, FALSE);

	/* copy into buffer */
	password_sz = strlen(password);
	if (!fu_memcpy_safe(req,
			    sizeof(req),
			    0x2, /* dst */
			    (guint8 *)password,
			    password_sz,
			    0x0, /* src */
			    password_sz,
			    error)) {
		g_prefix_error(error, "password invalid: ");
		return FALSE;
	}

	/* run transaction */
	if (!fu_ipmi_device_transaction(self,
					IPMI_NETFN_APP_REQUEST,
					IPMI_SET_USER_PASSWORD,
					req,
					sizeof(req),
					NULL, /* resp */
					0,
					NULL,
					FU_IPMI_DEVICE_TIMEOUT,
					error)) {
		g_prefix_error(error, "failed to set user %02x password: ", user_id);
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_ipmi_device_set_user_priv(FuIpmiDevice *self,
			     guint8 user_id,
			     guint8 priv_limit,
			     guint8 channel,
			     GError **error)
{
	const guint8 req[] = {channel, user_id, priv_limit, 0x0};

	g_return_val_if_fail(FU_IS_IPMI_DEVICE(self), FALSE);
	g_return_val_if_fail(user_id != 0x0, FALSE);
	g_return_val_if_fail(channel <= 0x0F, FALSE);
	g_return_val_if_fail(priv_limit <= 0x0F, FALSE);

	/* run transaction */
	if (!fu_ipmi_device_transaction(self,
					IPMI_NETFN_APP_REQUEST,
					IPMI_SET_USER_ACCESS,
					req,
					sizeof(req),
					NULL, /* resp */
					0,
					NULL,
					FU_IPMI_DEVICE_TIMEOUT,
					error)) {
		g_prefix_error(error,
			       "failed to set user %02x privs of 0x%02x, 0x%02x: ",
			       user_id,
			       priv_limit,
			       channel);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_ipmi_device_init(FuIpmiDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "IPMI");
	fu_device_set_summary(FU_DEVICE(self), "Intelligent Platform Management Interface");
	fu_device_add_icon(FU_DEVICE(self), "computer");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
}

static void
fu_ipmi_device_class_init(FuIpmiDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->probe = fu_ipmi_device_probe;
	klass_device->setup = fu_ipmi_device_setup;
	klass_device->to_string = fu_ipmi_device_to_string;
}

FuIpmiDevice *
fu_ipmi_device_new(FuContext *ctx)
{
	FuIpmiDevice *self;
	self = g_object_new(FU_TYPE_IPMI_DEVICE, "context", ctx, "device-file", "/dev/ipmi0", NULL);
	return self;
}
