/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define FWUPD_TYPE_REQUEST (fwupd_request_get_type())
G_DECLARE_DERIVABLE_TYPE(FwupdRequest, fwupd_request, FWUPD, REQUEST, GObject)

struct _FwupdRequestClass {
	GObjectClass parent_class;
	/*< private >*/
	void (*_fwupd_reserved1)(void);
	void (*_fwupd_reserved2)(void);
	void (*_fwupd_reserved3)(void);
	void (*_fwupd_reserved4)(void);
	void (*_fwupd_reserved5)(void);
	void (*_fwupd_reserved6)(void);
	void (*_fwupd_reserved7)(void);
};

/**
 * FwupdRequestKind:
 * @FWUPD_REQUEST_KIND_UNKNOWN:		Unknown kind
 * @FWUPD_REQUEST_KIND_POST:		After the update
 * @FWUPD_REQUEST_KIND_IMMEDIATE:	Immediately
 *
 * The kind of request we are asking of the user.
 **/
typedef enum {
	FWUPD_REQUEST_KIND_UNKNOWN,   /* Since: 1.6.2 */
	FWUPD_REQUEST_KIND_POST,      /* Since: 1.6.2 */
	FWUPD_REQUEST_KIND_IMMEDIATE, /* Since: 1.6.2 */
	/*< private >*/
	FWUPD_REQUEST_KIND_LAST
} FwupdRequestKind;

/**
 * FWUPD_REQUEST_ID_REMOVE_REPLUG:
 *
 * The user needs to remove and reinsert the device to complete the update, e.g.
 * "The update will continue when the device USB cable has been unplugged and then re-inserted."
 *
 * Since 1.6.2
 */
#define FWUPD_REQUEST_ID_REMOVE_REPLUG "org.freedesktop.fwupd.request.remove-replug"

/**
 * FWUPD_REQUEST_ID_PRESS_UNLOCK:
 *
 * The user needs to press unlock on the device to continue, e.g.
 * "Press unlock on the device to continue the update process."
 *
 * Since 1.6.2
 */
#define FWUPD_REQUEST_ID_PRESS_UNLOCK "org.freedesktop.fwupd.request.press-unlock"

/**
 * FWUPD_REQUEST_ID_REMOVE_USB_CABLE:
 *
 * The user needs to remove the device to complete the update, e.g.
 * "The update will continue when the device USB cable has been unplugged."
 *
 * Since 1.8.6
 */
#define FWUPD_REQUEST_ID_REMOVE_USB_CABLE "org.freedesktop.fwupd.request.remove-usb-cable"

/**
 * FWUPD_REQUEST_ID_DO_NOT_POWER_OFF:
 *
 * Show the user a message not to unplug the machine from the AC power, e.g.
 * "Do not turn off your computer or remove the AC adaptor until you are sure the update has
 * completed."
 *
 * Since 1.8.6
 */
#define FWUPD_REQUEST_ID_DO_NOT_POWER_OFF "org.freedesktop.fwupd.request.do-not-power-off"

/**
 * FWUPD_REQUEST_FLAG_NONE:
 *
 * No flags are set.
 *
 * Since: 1.8.6
 */
#define FWUPD_REQUEST_FLAG_NONE (0u)

/**
 * FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE:
 *
 * Use a generic (translated) request message.
 *
 * Since: 1.8.6
 */
#define FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE (1u << 0)

/**
 * FWUPD_REQUEST_FLAG_ALLOW_GENERIC_IMAGE:
 *
 * Use a generic (translated) request image.
 *
 * Since: 1.8.6
 */
#define FWUPD_REQUEST_FLAG_ALLOW_GENERIC_IMAGE (1u << 1)

/**
 * FWUPD_REQUEST_FLAG_UNKNOWN:
 *
 * The request flag is unknown, typically caused by using mismatched client and daemon.
 *
 * Since: 1.8.6
 */
#define FWUPD_REQUEST_FLAG_UNKNOWN G_MAXUINT64

/**
 * FwupdRequestFlags:
 *
 * Flags used to represent request attributes
 */
typedef guint64 FwupdRequestFlags;

const gchar *
fwupd_request_kind_to_string(FwupdRequestKind kind);
FwupdRequestKind
fwupd_request_kind_from_string(const gchar *kind);

const gchar *
fwupd_request_flag_to_string(FwupdRequestFlags flag);
FwupdRequestFlags
fwupd_request_flag_from_string(const gchar *flag);

FwupdRequest *
fwupd_request_new(void);
gchar *
fwupd_request_to_string(FwupdRequest *self);

const gchar *
fwupd_request_get_id(FwupdRequest *self);
void
fwupd_request_set_id(FwupdRequest *self, const gchar *id);
guint64
fwupd_request_get_created(FwupdRequest *self);
void
fwupd_request_set_created(FwupdRequest *self, guint64 created);
const gchar *
fwupd_request_get_device_id(FwupdRequest *self);
void
fwupd_request_set_device_id(FwupdRequest *self, const gchar *device_id);
const gchar *
fwupd_request_get_message(FwupdRequest *self);
void
fwupd_request_set_message(FwupdRequest *self, const gchar *message);
const gchar *
fwupd_request_get_image(FwupdRequest *self);
void
fwupd_request_set_image(FwupdRequest *self, const gchar *image);
FwupdRequestKind
fwupd_request_get_kind(FwupdRequest *self);
void
fwupd_request_set_kind(FwupdRequest *self, FwupdRequestKind kind);

FwupdRequestFlags
fwupd_request_get_flags(FwupdRequest *self);
void
fwupd_request_set_flags(FwupdRequest *self, FwupdRequestFlags flags);
void
fwupd_request_add_flag(FwupdRequest *self, FwupdRequestFlags flag);
void
fwupd_request_remove_flag(FwupdRequest *self, FwupdRequestFlags flag);
gboolean
fwupd_request_has_flag(FwupdRequest *self, FwupdRequestFlags flag);

FwupdRequest *
fwupd_request_from_variant(GVariant *value);

G_END_DECLS
