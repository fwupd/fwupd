/*
 * Copyright (c) 1999-2021 Logitech, Inc.
 * All Rights Reserved
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef BULK_UTIL_H
#define BULK_UTIL_H

#define MAX_EP_COUNT		  2
#define DESCRIPTOR_SIZE		  64
#define MAX_DATA_SIZE		  16384
#define MAX_DATA_SIZE_UPD	  8192
#define ACK_PKT_SIZE		  12
#define PACKET_HEADER_SIZE	  2 * sizeof(int)
#define SYNC_PACKET_HEADER_SIZE	  12
#define BASE64_LENGTH		  25
#define PAYLOAD_SIZE		  MAX_DATA_SIZE_UPD - PACKET_HEADER_SIZE
#define TRANSFER_SUCCESS	  1
#define TRANSFER_FAILURE	  0
#define LIBUSB_ERROR_OTHER	  1
#define ACK_PACKET_FOR_HASH_VALUE 4
#define SEM_NO_TIMEOUT		  0x0
#define SLEEP_3000_MS		  3000
#define SEND_DATA_TIMEOUT_SEC	  1
#define MAX_RETRIES		  10
#define TIME_OUT_RETRY		  2
#define FAIL_RETRIES		  3
#define INCORRECT_PACKET_RETRIES  5
#define HASH_TIMEOUT		  60
#define MD5_DIGEST_SIZE		  17
#define FILE_OPEN_ERROR		  -1
#define MAX_QUEUE_SIZE		  100
#define ONE_SEC			  1000
#define INFINITE_WAIT		  0
#define USB_INTERFACE_CLASS	  255
#define USB_INTERFACE_PROTOCOL	  1
#define HANDLER_TIMEOUT		  500 * 1000;
#define TIME_OUT_5S		  5
#define TWO_MILISECONDS		  2000
#define UINT16_MIN		  0
#define THREAD_COUNT		  4
#define ERROR_STRING_SIZE	  100
#define NULL_CHARACTER		  '\0'

#define VARIABLE_BUFFER_SIZE(size) size

#define PAYLOAD_SIZE_SYNC(size) size - PACKET_HEADER_SIZE - sizeof(gint)

#define LOCK(cond, mutex)                                                                          \
	g_cond_wait_until(cond, mutex, g_get_monotonic_time() + TIME_OUT_5S * G_TIME_SPAN_SECOND)

#define UNLOCK(cond) g_cond_signal(cond)

/* Structure used to store lock data */
typedef struct {
	GCond cond;
	GMutex mutex;
} Lock;
#endif
