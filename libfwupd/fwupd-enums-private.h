/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

G_BEGIN_DECLS

#define FWUPD_RESULT_KEY_APPSTREAM_ID		"AppstreamId"	/* s */
#define FWUPD_RESULT_KEY_CHECKSUM		"Checksum"	/* as */
#define FWUPD_RESULT_KEY_CREATED		"Created"	/* t */
#define FWUPD_RESULT_KEY_DESCRIPTION		"Description"	/* s */
#define FWUPD_RESULT_KEY_DETACH_CAPTION		"DetachCaption"	/* s */
#define FWUPD_RESULT_KEY_DETACH_IMAGE		"DetachImage"	/* s */
#define FWUPD_RESULT_KEY_DEVICE_ID		"DeviceId"	/* s */
#define FWUPD_RESULT_KEY_PARENT_DEVICE_ID	"ParentDeviceId"/* s */
#define FWUPD_RESULT_KEY_FILENAME		"Filename"	/* s */
#define FWUPD_RESULT_KEY_PROTOCOL		"Protocol"	/* s */
#define FWUPD_RESULT_KEY_CATEGORIES		"Categories"	/* as */
#define FWUPD_RESULT_KEY_ISSUES			"Issues"	/* as */
#define FWUPD_RESULT_KEY_FLAGS			"Flags"		/* t */
#define FWUPD_RESULT_KEY_FLASHES_LEFT		"FlashesLeft"	/* u */
#define FWUPD_RESULT_KEY_INSTALL_DURATION	"InstallDuration"	/* u */
#define FWUPD_RESULT_KEY_GUID			"Guid"		/* as */
#define FWUPD_RESULT_KEY_INSTANCE_IDS		"InstanceIds"	/* as */
#define FWUPD_RESULT_KEY_HOMEPAGE		"Homepage"	/* s */
#define FWUPD_RESULT_KEY_DETAILS_URL		"DetailsUrl"	/* s */
#define FWUPD_RESULT_KEY_SOURCE_URL		"SourceUrl"	/* s */
#define FWUPD_RESULT_KEY_ICON			"Icon"		/* as */
#define FWUPD_RESULT_KEY_LICENSE		"License"	/* s */
#define FWUPD_RESULT_KEY_MODIFIED		"Modified"	/* t */
#define FWUPD_RESULT_KEY_METADATA		"Metadata"	/* a{ss} */
#define FWUPD_RESULT_KEY_NAME			"Name"		/* s */
#define FWUPD_RESULT_KEY_NAME_VARIANT_SUFFIX	"NameVariantSuffix"	/* s */
#define FWUPD_RESULT_KEY_PLUGIN			"Plugin"	/* s */
#define FWUPD_RESULT_KEY_PROTOCOL		"Protocol"	/* s */
#define FWUPD_RESULT_KEY_RELEASE		"Release"	/* a{sv} */
#define FWUPD_RESULT_KEY_REMOTE_ID		"RemoteId"	/* s */
#define FWUPD_RESULT_KEY_SERIAL			"Serial"	/* s */
#define FWUPD_RESULT_KEY_SIZE			"Size"		/* t */
#define FWUPD_RESULT_KEY_SUMMARY		"Summary"	/* s */
#define FWUPD_RESULT_KEY_TRUST_FLAGS		"TrustFlags"	/* t */
#define FWUPD_RESULT_KEY_UPDATE_MESSAGE		"UpdateMessage"	/* s */
#define FWUPD_RESULT_KEY_UPDATE_ERROR		"UpdateError"	/* s */
#define FWUPD_RESULT_KEY_UPDATE_STATE		"UpdateState"	/* u */
#define FWUPD_RESULT_KEY_URI			"Uri"		/* s */
#define FWUPD_RESULT_KEY_VENDOR_ID		"VendorId"	/* s */
#define FWUPD_RESULT_KEY_VENDOR			"Vendor"	/* s */
#define FWUPD_RESULT_KEY_VENDOR			"Vendor"	/* s */
#define FWUPD_RESULT_KEY_VERSION_BOOTLOADER	"VersionBootloader"	/* s */
#define FWUPD_RESULT_KEY_VERSION_FORMAT		"VersionFormat"	/* u */
#define FWUPD_RESULT_KEY_VERSION_RAW		"VersionRaw"	/* t */
#define FWUPD_RESULT_KEY_VERSION_LOWEST		"VersionLowest"	/* s */
#define FWUPD_RESULT_KEY_VERSION		"Version"	/* s */

G_END_DECLS
