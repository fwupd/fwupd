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

#define FU_FOCAL_MOC_TEST_PUBLIC_KEY_SIZE 65

void
fu_focal_moc_test_device_set_response_status(FuFocalMocTestDevice *self, guint8 status);
void
fu_focal_moc_test_device_set_fault(FuFocalMocTestDevice *self, FuFocalMocTestFault fault);
GByteArray *
fu_focal_moc_test_device_get_response_payload(FuFocalMocTestDevice *self);
GByteArray *
fu_focal_moc_test_device_get_last_request(FuFocalMocTestDevice *self);
guint32
fu_focal_moc_test_device_get_d2m_sequence(FuFocalMocTestDevice *self);
guint32
fu_focal_moc_test_device_get_m2d_sequence(FuFocalMocTestDevice *self);
guint8
fu_focal_moc_test_device_get_tx_message_id(FuFocalMocTestDevice *self);
const guint8 *
fu_focal_moc_test_device_get_pk_host(FuFocalMocTestDevice *self);
#endif
