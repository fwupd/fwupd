/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#ifdef HAVE_GNUTLS
#include <gnutls/abstract.h>
#endif

#include "fu-focal-moc-transport.h"

#if defined(HAVE_GNUTLS) && GNUTLS_VERSION_NUMBER >= 0x030802
#define FU_FOCAL_MOC_TEST_HAVE_ECDH
#endif

#ifdef FU_FOCAL_MOC_TEST_HAVE_ECDH
typedef enum {
	FU_FOCAL_MOC_TEST_FAULT_NONE,
	FU_FOCAL_MOC_TEST_FAULT_FIRST_INDEX,
	FU_FOCAL_MOC_TEST_FAULT_FIRST_TOTAL_ZERO,
	FU_FOCAL_MOC_TEST_FAULT_FIRST_MORE,
	FU_FOCAL_MOC_TEST_FAULT_FIRST_MESSAGE_ID,
	FU_FOCAL_MOC_TEST_FAULT_MESSAGE_ID,
	FU_FOCAL_MOC_TEST_FAULT_TOTAL,
	FU_FOCAL_MOC_TEST_FAULT_INDEX,
	FU_FOCAL_MOC_TEST_FAULT_MORE,
	FU_FOCAL_MOC_TEST_FAULT_STATUS,
	FU_FOCAL_MOC_TEST_FAULT_SHORT,
	FU_FOCAL_MOC_TEST_FAULT_TRUNCATED,
	FU_FOCAL_MOC_TEST_FAULT_CIPHER_BCC,
	FU_FOCAL_MOC_TEST_FAULT_SEND,
} FuFocalMocTestFault;

#define FU_TYPE_FOCAL_MOC_TEST_DEVICE (fu_focal_moc_test_device_get_type())
G_DECLARE_FINAL_TYPE(FuFocalMocTestDevice,
		     fu_focal_moc_test_device,
		     FU,
		     FOCAL_MOC_TEST_DEVICE,
		     GObject)

/* a software peer for the TransportSec handshake, with injectable faults */
struct _FuFocalMocTestDevice {
	GObject parent_instance;
	GPtrArray *rx_queue;
	GByteArray *last_request;
	guint8 pk_host[65];
	guint8 pk_device[65];
	guint8 key_aes_d2m[32];
	guint8 key_mac_d2m[32];
	guint8 key_iv_d2m[32];
	guint8 key_aes_m2d[32];
	guint8 key_mac_m2d[32];
	guint8 key_iv_m2d[32];
	guint32 d2m_sequence;
	guint32 m2d_sequence;
	guint8 tx_message_id;
	guint8 response_status;
	FuFocalMocTestFault fault;
	GByteArray *response_payload;
};
#endif
