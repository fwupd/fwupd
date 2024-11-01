/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuIoctl"

#include "config.h"

#include "fu-ioctl-private.h"
#include "fu-udev-device-private.h"

struct _FuIoctl {
	GObject parent_instance;
	FuUdevDevice *udev_device; /* ref */
	GString *event_id;
	GPtrArray *fixups; /* of FuIoctlFixup */
};

G_DEFINE_TYPE(FuIoctl, fu_ioctl, G_TYPE_OBJECT)

typedef struct {
	gchar *key;
	gboolean is_mutable;
	guint8 *buf;
	gsize bufsz;
	FuIoctlFixupFunc fixup_cb;
} FuIoctlFixup;

static void
fu_ioctl_fixup_free(FuIoctlFixup *fixup)
{
	g_free(fixup->key);
	g_free(fixup);
}

static gchar *
fu_ioctl_fixup_build_key(FuIoctlFixup *fixup, const gchar *suffix)
{
	return g_strdup_printf("%s%s", fixup->key != NULL ? fixup->key : "", suffix);
}

/**
 * fu_ioctl_set_name:
 * @self: a #FuIoctl
 * @name: (nullable): a string, e.g. `Nvme`
 *
 * Adds a name for the ioctl, preserving compatibility with existing emulation data.
 *
 * NOTE: For new devices this is not required.
 *
 * Since: 2.0.2
 **/
void
fu_ioctl_set_name(FuIoctl *self, const gchar *name)
{
	g_return_if_fail(FU_IS_IOCTL(self));
	g_string_truncate(self->event_id, 0);
	g_string_append_printf(self->event_id, "%sIoctl:", name != NULL ? name : "");
}

/* private */
FuIoctl *
fu_ioctl_new(FuUdevDevice *udev_device)
{
	g_autoptr(FuIoctl) self = g_object_new(FU_TYPE_IOCTL, NULL);

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(udev_device), NULL);

	self->udev_device = g_object_ref(udev_device);
	return g_steal_pointer(&self);
}

static void
fu_ioctl_append_key(GString *event_id, const gchar *key, const gchar *value)
{
	if (event_id->len > 0 && !g_str_has_suffix(event_id->str, ":"))
		g_string_append_c(event_id, ',');
	g_string_append_printf(event_id, "%s=%s", key, value);
}

static void
fu_ioctl_append_key_as_u8(GString *event_id, const gchar *key, gsize value)
{
	g_autofree gchar *value2 = g_strdup_printf("0x%02x", (guint)value);
	fu_ioctl_append_key(event_id, key, value2);
}

static void
fu_ioctl_append_key_as_u16(GString *event_id, const gchar *key, gsize value)
{
	g_autofree gchar *value2 = g_strdup_printf("0x%04x", (guint)value);
	fu_ioctl_append_key(event_id, key, value2);
}

static void
fu_ioctl_append_key_from_buf(GString *event_id, const gchar *key, const guint8 *buf, gsize bufsz)
{
	g_autofree gchar *key_data = g_strdup_printf("%sData", key != NULL ? key : "");
	g_autofree gchar *value_data = g_base64_encode(buf, bufsz);
	g_autofree gchar *key_length = g_strdup_printf("%sLength", key != NULL ? key : "");
	g_autofree gchar *value_length = g_strdup_printf("0x%x", (guint)bufsz);

	fu_ioctl_append_key(event_id, key_data, value_data);
	fu_ioctl_append_key(event_id, key_length, value_length);
}

/**
 * fu_ioctl_add_key_as_u8:
 * @self: a #FuIoctl
 * @key: a string, e.g. `Opcode`
 * @value: a integer value
 *
 * Adds a key for the emulation, formatting it as `0x%02x`.
 *
 * Since: 2.0.2
 **/
void
fu_ioctl_add_key_as_u8(FuIoctl *self, const gchar *key, gsize value)
{
	g_return_if_fail(FU_IS_IOCTL(self));
	g_return_if_fail(key != NULL);
	fu_ioctl_append_key_as_u8(self->event_id, key, value);
}

/**
 * fu_ioctl_add_key_as_u16:
 * @self: a #FuIoctl
 * @key: a string, e.g. `Opcode`
 * @value: a integer value
 *
 * Adds a key for the emulation, formatting it as `0x%04x`.
 *
 * Since: 2.0.2
 **/
void
fu_ioctl_add_key_as_u16(FuIoctl *self, const gchar *key, gsize value)
{
	g_return_if_fail(FU_IS_IOCTL(self));
	g_return_if_fail(key != NULL);
	fu_ioctl_append_key_as_u16(self->event_id, key, value);
}

static void
fu_ioctl_add_buffer(FuIoctl *self,
		    const gchar *key,
		    guint8 *buf,
		    gsize bufsz,
		    gboolean is_mutable,
		    FuIoctlFixupFunc fixup_cb)
{
	fu_ioctl_append_key_from_buf(self->event_id, key, buf, bufsz);
	if (fixup_cb != NULL) {
		FuIoctlFixup *fixup = g_new0(FuIoctlFixup, 1);
		fixup->key = g_strdup(key);
		fixup->is_mutable = is_mutable;
		fixup->buf = buf;
		fixup->bufsz = bufsz;
		fixup->fixup_cb = fixup_cb;
		g_ptr_array_add(self->fixups, fixup);
	}
}

/**
 * fu_ioctl_add_mutable_buffer:
 * @self: a #FuIoctl
 * @key: a string, e.g. `Cdb`
 * @buf: (nullable): an optional buffer
 * @bufsz: Size of @buf
 * @fixup_cb: (scope forever): a function to call on the structure
 *
 * Adds a mutable buffer that can be used to fix up the ioctl-defined structure with the buffer and
 * size, and adds a key for the emulation.
 *
 * Since: 2.0.2
 **/
void
fu_ioctl_add_mutable_buffer(FuIoctl *self,
			    const gchar *key,
			    guint8 *buf,
			    gsize bufsz,
			    FuIoctlFixupFunc fixup_cb)
{
	fu_ioctl_add_buffer(self, key, buf, bufsz, TRUE, fixup_cb);
}

/**
 * fu_ioctl_add_const_buffer:
 * @self: a #FuIoctl
 * @key: a string, e.g. `Cdb`
 * @buf: (nullable): an optional buffer
 * @bufsz: Size of @buf
 * @fixup_cb: (scope forever): a function to call on the structure
 *
 * Adds a constant buffer that can be used to fix up the ioctl-defined structure with the buffer
 * and size, and adds a key for the emulation.
 *
 * Since: 2.0.2
 **/
void
fu_ioctl_add_const_buffer(FuIoctl *self,
			  const gchar *key,
			  const guint8 *buf,
			  gsize bufsz,
			  FuIoctlFixupFunc fixup_cb)
{
	fu_ioctl_add_buffer(self, key, (guint8 *)buf, bufsz, FALSE, fixup_cb);
}

/**
 * fu_ioctl_execute:
 * @self: a #FuIoctl
 * @request: request number
 * @buf: a buffer to use, which *must* be large enough for the request
 * @bufsz: the size of @buf
 * @rc: (out) (nullable): the raw return value from the ioctl
 * @timeout: timeout in ms for the retry action, see %FU_IOCTL_FLAG_RETRY
 * @flags: some #FuIoctlFlags, e.g. %FU_IOCTL_FLAG_RETRY
 * @error: (nullable): optional return location for an error
 *
 * Executes the ioctl, emulating as required. Each fixup defined using fu_ioctl_add_mutable_buffer()
 * of fu_ioctl_add_const_buffer() is run before the ioctl is executed.
 *
 * If there are no fixups defined, the @buf is emulated, and so you must ensure that there are no
 * ioctl wrapper structures that use indirect pointer values.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.2
 **/
gboolean
fu_ioctl_execute(FuIoctl *self,
		 gulong request,
		 gpointer buf,
		 gsize bufsz,
		 gint *rc,
		 guint timeout,
		 FuIoctlFlags flags,
		 GError **error)
{
	FuDeviceEvent *event = NULL;
	g_autoptr(GString) event_id = NULL;

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self->udev_device), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self->udev_device)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_string_new(self->event_id->str);
		if (g_strcmp0(event_id->str, "Ioctl:") == 0) {
			fu_ioctl_append_key_as_u16(event_id, "Request", request);
			fu_ioctl_append_key_from_buf(event_id, NULL, buf, bufsz);
		}
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self->udev_device), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self->udev_device), event_id->str, error);
		if (event == NULL)
			return FALSE;
		if (self->fixups->len == 0) {
			if (!fu_device_event_copy_data(event, "DataOut", buf, bufsz, NULL, error))
				return FALSE;
		}
		for (guint i = 0; i < self->fixups->len; i++) {
			FuIoctlFixup *fixup = g_ptr_array_index(self->fixups, i);
			g_autofree gchar *key = fu_ioctl_fixup_build_key(fixup, "DataOut");
			if (!fixup->is_mutable)
				continue;
			if (!fu_device_event_copy_data(event,
						       key,
						       fixup->buf,
						       fixup->bufsz,
						       NULL,
						       error))
				return FALSE;
		}
		if (rc != NULL) {
			gint64 rc_tmp = fu_device_event_get_i64(event, "Rc", NULL);
			if (rc_tmp != G_MAXINT64)
				*rc = (gint)rc_tmp;
		}
		return TRUE;
	}

	/* save */
	if (fu_context_has_flag(fu_device_get_context(FU_DEVICE(self->udev_device)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event = fu_device_save_event(FU_DEVICE(self->udev_device), event_id->str);
	}

	/* the buffer might be specified indirectly */
	if (buf != NULL) {
		for (guint i = 0; i < self->fixups->len; i++) {
			FuIoctlFixup *fixup = g_ptr_array_index(self->fixups, i);
			if (!fixup->fixup_cb(self, buf, fixup->buf, fixup->bufsz, error))
				return FALSE;
		}
	}
	if (!fu_udev_device_ioctl(self->udev_device,
				  request,
				  buf,
				  bufsz,
				  rc,
				  timeout,
				  flags,
				  error))
		return FALSE;

	/* save response */
	if (event != NULL) {
		if (rc != NULL && *rc != 0)
			fu_device_event_set_i64(event, "Rc", *rc);
		if (self->fixups->len == 0)
			fu_device_event_set_data(event, "DataOut", buf, bufsz);
		for (guint i = 0; i < self->fixups->len; i++) {
			FuIoctlFixup *fixup = g_ptr_array_index(self->fixups, i);
			g_autofree gchar *key = fu_ioctl_fixup_build_key(fixup, "DataOut");
			if (!fixup->is_mutable)
				continue;
			fu_device_event_set_data(event, key, fixup->buf, fixup->bufsz);
		}
	}

	/* success */
	return TRUE;
}

static void
fu_ioctl_init(FuIoctl *self)
{
	self->event_id = g_string_new("Ioctl:");
	self->fixups = g_ptr_array_new_with_free_func((GDestroyNotify)fu_ioctl_fixup_free);
}

static void
fu_ioctl_finalize(GObject *object)
{
	FuIoctl *self = FU_IOCTL(object);

	g_string_free(self->event_id, TRUE);
	g_ptr_array_unref(self->fixups);
	if (self->udev_device != NULL)
		g_object_unref(self->udev_device);

	G_OBJECT_CLASS(fu_ioctl_parent_class)->finalize(object);
}

static void
fu_ioctl_class_init(FuIoctlClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_ioctl_finalize;
}
