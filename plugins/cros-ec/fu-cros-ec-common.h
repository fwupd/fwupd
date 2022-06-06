/*
 * Copyright (C) 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define UPDATE_PROTOCOL_VERSION 6
#define FU_CROS_EC_STRLEN	32

/*
 * This is the format of the update PDU header.
 *
 * block digest: the first four bytes of the sha1 digest of the rest of the
 *               structure (can be 0 on boards where digest is ignored).
 * block_base:   offset of this PDU into the flash SPI.
 */
typedef struct __attribute__((packed)) {
	guint32 block_digest;
	guint32 block_base;
	/* The actual payload goes here. */
} update_command;

/*
 * This is the frame format the host uses when sending update PDUs over USB.
 *
 * The PDUs are up to 1K bytes in size, they are fragmented into USB chunks of
 * 64 bytes each and reassembled on the receive side before being passed to
 * the flash update function.
 *
 * The flash update function receives the unframed PDU body (starting at the
 * cmd field below), and puts its reply into the same buffer the PDU was in.
 */
struct update_frame_header {
	guint32 block_size; /* Total frame size, including this field. */
	update_command cmd;
};

/*
 * A convenience structure which allows to group together various revision
 * fields of the header created by the signer (cr50-specific).
 *
 * These fields are compared when deciding if versions of two images are the
 * same or when deciding which one of the available images to run.
 */
struct signed_header_version {
	guint32 minor;
	guint32 major;
	guint32 epoch;
};

/*
 * Response to the connection establishment request.
 *
 * When responding to the very first packet of the update sequence, the
 * original USB update implementation was responding with a four byte value,
 * just as to any other block of the transfer sequence.
 *
 * It became clear that there is a need to be able to enhance the update
 * protocol, while staying backwards compatible.
 *
 * All newer protocol versions (starting with version 2) respond to the very
 * first packet with an 8 byte or larger response, where the first 4 bytes are
 * a version specific data, and the second 4 bytes - the protocol version
 * number.
 *
 * This way the host receiving of a four byte value in response to the first
 * packet is considered an indication of the target running the 'legacy'
 * protocol, version 1. Receiving of an 8 byte or longer response would
 * communicates the protocol version in the second 4 bytes.
 */
struct first_response_pdu {
	guint32 return_value;

	/* The below fields are present in versions 2 and up. */

	/* Type of header following (one of first_response_pdu_header_type) */
	guint16 header_type;

	/* Must be UPDATE_PROTOCOL_VERSION */
	guint16 protocol_version;

	/* In version 6 and up, a board-specific header follows. */
	union {
		/* cr50 (header_type = UPDATE_HEADER_TYPE_CR50) */
		struct {
			/* The below fields are present in versions 3 and up. */
			guint32 backup_ro_offset;
			guint32 backup_rw_offset;

			/* The below fields are present in versions 4 and up. */
			/*
			 * Versions of the currently active RO and RW sections.
			 */
			struct signed_header_version shv[2];

			/* The below fields are present in versions 5 and up */
			/* keyids of the currently active RO and RW sections. */
			guint32 keyid[2];
		} cr50;
		/* Common code (header_type = UPDATE_HEADER_TYPE_COMMON) */
		struct {
			/* Maximum PDU size */
			guint32 maximum_pdu_size;

			/* Flash protection status */
			guint32 flash_protection;

			/* Offset of the other region */
			guint32 offset;

			/* Version string of the other region */
			gchar version[FU_CROS_EC_STRLEN];

			/* Minimum rollback version that RO will accept */
			gint32 min_rollback;

			/* RO public key version */
			guint32 key_version;
		} common;
	};
};

enum first_response_pdu_header_type {
	UPDATE_HEADER_TYPE_CR50 = 0, /* Must be 0 for backwards compatibility */
	UPDATE_HEADER_TYPE_COMMON = 1,
};

struct cros_ec_version {
	gchar boardname[FU_CROS_EC_STRLEN];
	gchar triplet[FU_CROS_EC_STRLEN];
	gchar sha1[FU_CROS_EC_STRLEN];
	gboolean dirty;
};

gboolean
fu_cros_ec_parse_version(const gchar *version_raw, struct cros_ec_version *version, GError **error);
