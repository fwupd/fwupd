/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define FWUPD_TYPE_REQUEST (fwupd_request_get_type())
G_DECLARE_DERIVABLE_TYPE(FwupdRequest, fwupd_request, FWUPD, REQUEST, GObject)

struct _FwupdRequestClass {
	GObjectClass parent_class;
	void (*invalidate)(FwupdRequest *client);
	/*< private >*/
	void (*_fwupd_reserved2)(void);
	void (*_fwupd_reserved3)(void);
	void (*_fwupd_reserved4)(void);
	void (*_fwupd_reserved5)(void);
	void (*_fwupd_reserved6)(void);
	void (*_fwupd_reserved7)(void);
};

/**
 * FwupdRequestKind:
 *
 * The kind of request we are asking of the user.
 **/
typedef enum {
	/**
	 * FWUPD_REQUEST_KIND_UNKNOWN:
	 *
	 * Unknown kind.
	 *
	 * Since: 1.6.2
	 */
	FWUPD_REQUEST_KIND_UNKNOWN,
	/**
	 * FWUPD_REQUEST_KIND_POST:
	 *
	 * After the update.
	 *
	 * Since: 1.6.2
	 */
	FWUPD_REQUEST_KIND_POST,
	/**
	 * FWUPD_REQUEST_KIND_IMMEDIATE:
	 *
	 * Immediately.
	 *
	 * Since: 1.6.2
	 */
	FWUPD_REQUEST_KIND_IMMEDIATE,
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
 * FWUPD_REQUEST_ID_INSERT_USB_CABLE:
 *
 * The user needs to insert the cable to complete the update, e.g.
 * "The update will continue when the device USB cable has been re-inserted."
 *
 * Since 1.8.9
 */
#define FWUPD_REQUEST_ID_INSERT_USB_CABLE "org.freedesktop.fwupd.request.insert-usb-cable"

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
 * FWUPD_REQUEST_ID_REPLUG_INSTALL:
 *
 * Show the user a message to replug the device and then install the firmware, e.g.
 * "Unplug and replug the device, to continue the update process."
 *
 * Since 1.8.11
 */
#define FWUPD_REQUEST_ID_REPLUG_INSTALL "org.freedesktop.fwupd.replug-install"

/**
 * FWUPD_REQUEST_ID_REPLUG_POWER:
 *
 * Show the user a message to replug the power connector, e.g.
 * "The update will continue when the device power cable has been unplugged and then re-inserted."
 *
 * Since 1.9.9
 */
#define FWUPD_REQUEST_ID_REPLUG_POWER "org.freedesktop.fwupd.replug-power"

/**
 * FwupdRequestFlags:
 *
 * Flags used to represent request attributes
 */
typedef enum {
	/**
	 * FWUPD_REQUEST_FLAG_NONE:
	 *
	 * No flags are set.
	 *
	 * Since: 1.8.6
	 */
	FWUPD_REQUEST_FLAG_NONE = 0u,
	/**
	 * FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE:
	 *
	 * Use a generic (translated) request message.
	 *
	 * Since: 1.8.6
	 */
	FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE = 1u << 0,
	/**
	 * FWUPD_REQUEST_FLAG_ALLOW_GENERIC_IMAGE:
	 *
	 * Use a generic (translated) request image.
	 *
	 * Since: 1.8.6
	 */
	FWUPD_REQUEST_FLAG_ALLOW_GENERIC_IMAGE = 1u << 1,
	/**
	 * FWUPD_REQUEST_FLAG_NON_GENERIC_MESSAGE:
	 *
	 * Device requires a non-generic interaction with custom non-translatable text.
	 *
	 * Since: 1.9.10
	 */
	FWUPD_REQUEST_FLAG_NON_GENERIC_MESSAGE = 1ull << 2,
	/**
	 * FWUPD_REQUEST_FLAG_NON_GENERIC_IMAGE:
	 *
	 * Device requires to show the user a custom image for the action to make sense.
	 *
	 * Since: 1.9.10
	 */
	FWUPD_REQUEST_FLAG_NON_GENERIC_IMAGE = 1ull << 3,
	/**
	 * FWUPD_REQUEST_FLAG_UNKNOWN:
	 *
	 * The request flag is unknown, typically caused by using mismatched client and daemon.
	 *
	 * Since: 1.8.6
	 */
	FWUPD_REQUEST_FLAG_UNKNOWN = G_MAXUINT64,
} FwupdRequestFlags;

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

const gchar *
fwupd_request_get_id(FwupdRequest *self) G_GNUC_NON_NULL(1);
void
fwupd_request_set_id(FwupdRequest *self, const gchar *id) G_GNUC_NON_NULL(1);
guint64
fwupd_request_get_created(FwupdRequest *self) G_GNUC_NON_NULL(1);
void
fwupd_request_set_created(FwupdRequest *self, guint64 created) G_GNUC_NON_NULL(1);
const gchar *
fwupd_request_get_device_id(FwupdRequest *self) G_GNUC_NON_NULL(1);
void
fwupd_request_set_device_id(FwupdRequest *self, const gchar *device_id) G_GNUC_NON_NULL(1);
const gchar *
fwupd_request_get_message(FwupdRequest *self) G_GNUC_NON_NULL(1);
void
fwupd_request_set_message(FwupdRequest *self, const gchar *message) G_GNUC_NON_NULL(1);
const gchar *
fwupd_request_get_image(FwupdRequest *self) G_GNUC_NON_NULL(1);
void
fwupd_request_set_image(FwupdRequest *self, const gchar *image) G_GNUC_NON_NULL(1);
FwupdRequestKind
fwupd_request_get_kind(FwupdRequest *self) G_GNUC_NON_NULL(1);
void
fwupd_request_set_kind(FwupdRequest *self, FwupdRequestKind kind) G_GNUC_NON_NULL(1);

FwupdRequestFlags
fwupd_request_get_flags(FwupdRequest *self) G_GNUC_NON_NULL(1);
void
fwupd_request_set_flags(FwupdRequest *self, FwupdRequestFlags flags) G_GNUC_NON_NULL(1);
void
fwupd_request_add_flag(FwupdRequest *self, FwupdRequestFlags flag) G_GNUC_NON_NULL(1);
void
fwupd_request_remove_flag(FwupdRequest *self, FwupdRequestFlags flag) G_GNUC_NON_NULL(1);
gboolean
fwupd_request_has_flag(FwupdRequest *self, FwupdRequestFlags flag) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);

G_END_DECLS
