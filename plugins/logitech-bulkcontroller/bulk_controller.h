/*
 * Copyright (c) 1999-2021 Logitech, Inc.
 * All Rights Reserved
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef BULK_CONTROLLER_H_
#define BULK_CONTROLLER_H_

#include <glib-object.h>
#include <glib.h>
#include <gusb.h>
#include <gusb/gusb-device.h>
#include <locale.h>
#include <stdio.h>

#include "bulk_util.h"
#define TYPE_BULKCONTROLLER	      (logibulkcontroller_get_type())
#define MAX_EP_COUNT		      2
#define UPD_INTERFACE_SUBPROTOCOL_ID  117
#define SYNC_INTERFACE_SUBPROTOCOL_ID 118

typedef struct _LogiBulkControllerClass {
	GObjectClass parent_class;
} LogiBulkControllerClass;

typedef struct _LogiBulkController {
	GObject parent_instance;
} LogiBulkController;

/**
 * @brief Enum for bulk error codes.
 */
typedef enum {
	ERRORCODE_NO_ERROR = 0,
	ERRORCODE_UNKNOWN_DEVICE = 1,
	ERRORCODE_INVALID_VID = 2,
	ERRORCODE_INVALID_PID = 3,
	ERRORCODE_OPEN_DEVICE_FAILED = 4,
	ERRORCODE_DEVICE_NOT_OPEN = 5,
	ERRORCODE_NO_DEVICE = 6,
	ERRORCODE_IO_CONTROL_OPERATION_FAILED = 7,
	ERRORCODE_BULK_USB_INTERFACE_NOT_INITIALIZED = 8,
	ERRORCODE_BULK_USB_FAILED_INITIALIZE = 9,
	ERRORCODE_OPEN_DEV_HANDLE_FAILED = 10,
	ERRORCODE_OPEN_DEV_DESC_FAILED = 11,
	ERRORCODE_INVALID_SEND_DATA = 12,
	ERRORCODE_ENDPOINT_CLAIM_FAILED = 13,
	ERRORCODE_ENDPOINT_RELEASE_FAILED = 14,
	ERRORCODE_ENDPOINT_TRANSFER_FAILED = 15,
	ERRORCODE_ENDPOINT_RECEIVE_FAILED = 16,
	ERRORCODE_SEND_DATA_SIZE_ZERO = 17,
	ERRORCODE_API_NOT_IMPLEMENTED = 18,
	ERRORCODE_ERROR_CONDIG = 19,
	ERRORCODE_INCORRECT_ORDER = 20,
	ERRORCODE_FILE_OPEN_FAILED = 21,
	ERRORCODE_INVALID_INTERFACE = 22,
	ERRORCODE_READ_ZERO_SIZE = 23,
	ERRORCODE_INVALID_READ_DATA = 24,
	ERRORCODE_TRANSFER_FILE_DATA_FAILED = 25,
	ERRORCODE_FILE_DATA_INVALID = 26,
	ERRORCODE_FINISH_TRANSFER_FAILED = 27,
	ERRORCODE_USERFILE_NOT_EXIST = 28,
	ERRORCODE_INVALID_FILE_SIZE = 29,
	ERRORCODE_EMPTY_FILE = 30,
	ERRORCODE_READ_BUFFER_INVALID = 31,
	ERRORCODE_TRANSFER_INPROGRESS = 32,
	ERRORCODE_FILE_TRANSFER_INITIATED = 33,
	ERRORCODE_MUTEX_LOCK_TIMEOUT = 34,
	ERRORCODE_BUFFER_TRANSFER_TIMEOUT = 35,
	ERRORCODE_INVALID_CONTROLLER_OBJECT = 36,
	ERRORCODE_QUEUE_IS_FULL = 37,
	ERRORCODE_SEND_DATA_REQUEST_PUSHED_TO_QUEUE = 38,
	ERRORCODE_HAS_VALUES_MISMATCH = 39,
	ERRORCODE_CLAIM_INTERFACE_FAILED = 40,
	ERRORCODE_RELEASE_INTERFACE_FAILED = 41,
	ERRORCODE_BUFFER_TRANSFER_FAILED = 42,
	ERRORCODE_SYNC_TRANSFER_INPROGRESS = 43,
	ERRORCODE_INVALID_PACKET = 44,
	ERRORCODE_PACKET_CREATION_FAILED = 45
} BulkControllerErrorCode;

typedef enum {
	WRITE_TIME_OUT = 100,
	READ_ASYNC_TIME_OUT = 3000,
	READ_TIME_OUT = 30000 /* Android to calculate Hash and return */
} TimeOut;

typedef enum { SHA_256, SHA_512, MD5 } HashType;

typedef enum { BULK_INTERFACE_UPD, BULK_INTERFACE_SYNC } BulkInterface;

typedef enum {
	TRANSFER_HASH_STARTED,
	TRANSFER_INIT_STARTED,
	TRANSFER_STARTED,
	TRANSFER_FAILED,
	TRANSFER_INPROGRESS,
	TRANSFER_COMPLETED
} FileTransferState;

typedef enum {
	CMD_CHECK_BUFFERSIZE = 0xCC00,
	CMD_INIT = 0xCC01,
	CMD_START_TRANSFER = 0xCC02,
	CMD_DATA_TRANSFER = 0xCC03,
	CMD_END_TRANSFER = 0xCC04,
	CMD_UNINIT = 0xCC05,
	CMD_BUFFER_READ = 0xCC06,
	CMD_BUFFER_WRITE = 0xCC07,
	CMD_UNINIT_BUFFER = 0xCC08,
	CMD_ACK = 0xFF01,
	CMD_TIMEOUT = 0xFF02,
	CMD_NACK = 0xFF03
} UsbCommands;

typedef enum { OUT_ENDPOINT, IN_ENDPOINT } InterfaceEndPoints;

typedef enum { BUFFER_SIZE_8K = 8192, BUFFER_SIZE_16K = 16384 } BufferSize;

typedef enum {
	SEND_DATA_THREAD,
	READ_DATA_THREAD,
	WRITE_BUFFER_THREAD,
	SEND_DATA_SYNC_THREAD
} ThreadName;

/* Callbacks */

/**
 * @brief Defines the callback signature for error callback
 * @param error_code - the error code.
 * @param bulk_int - the interface where the error is coming from.
 * @param data - byte array containing the error data.
 * @param size - size of byte array data.
 */
typedef void (*BulkErrorCallback)(gint error_code,
				  BulkInterface bulk_int,
				  const gchar *data,
				  guint32 size,
				  void *user_data);

/**
 * @brief Defines the callback signature when data becomes available
 * @param data - byte array containing the read data.
 * @param size - size of byte array data.
 */
typedef void (*BulkReadCallback)(const gchar *data, guint32 size, void *user_data);

/**
* @brief Defines the callback signature when file transfer becomes available.  This
*        callback should be called when file transfer starts and when it is completed.
* @param file_path - byte array containing the path to the file.  The filename should
*        be the same file name as from the originating location.
*        The folder location of file should be the same folder set indicated in
SetFileTransferLocation
* @param size - size of byte array data.
* @param state - the state of file transfer- either one of the following
						      transfer_started|transfer_faield|transfer_complete
*/
typedef void (*BulkFileTransferCallback)(FileTransferState state,
					 gint progress,
					 BulkInterface bulk_intf,
					 void *user_data);

/**
 * @brief Defines the callback signature to send data over sync
 */
typedef void (*SendDataSyncCallback)(gint error_code,
				     gint status,
				     gint transaction_id,
				     void *user_data);

typedef struct {
	BulkErrorCallback bulk_error_cb;
	BulkFileTransferCallback bulk_transfer_cb;
	BulkReadCallback bulk_read_cb_upd;
	BulkReadCallback bulk_read_cb_sync;
	SendDataSyncCallback send_data_cb_sync;
} BulkControllerCallbacks;

typedef struct {
	guint cmd;
	guint length;
	guchar payload[PAYLOAD_SIZE];
} UsbPacket;

typedef struct {
	guint update;
	guint force;
	HashType hashType;
	guchar base64hash[BASE64_LENGTH - 1];
} EndTransferPkt;

typedef struct {
	guint cmd;
	guint length;
	gint sequence_id;
	guchar payload[BUFFER_SIZE_16K];
} UsbPacketSync;

typedef struct {
	gint vid;
	gint pid;
	guint sync_ep[MAX_EP_COUNT];
	guint upd_ep[MAX_EP_COUNT];
	guint sync_interface_number;
	guint upd_interface_number;
	GUsbContext *ctx;
	GUsbDevice *device;
} BulkControllerDevice;

typedef struct {
	gchar *buffer;
	guint buffer_len;
	GMainLoop *loop;
} GUsbDeviceAsyncHelper;

typedef struct {
	guint32 error_code;
	guint16 transaction_id;
} ReturnValue;

typedef struct {
	gchar *prog_name;
	GCond test_upd_cond;
	GMutex test_upd_mutex;
	void *device_ptr;
} ApiUserData;

/* Public Methods */
GType
logibulkcontroller_get_type(void);

/**
 * @brief Creates a bulk controller object
 * @param vid - the vendor id of device
 * @param pid - the product id of device
 * @param callbacks - struct pointer to all callbacks
 */
LogiBulkController *
logibulkcontroller_create_bulk_controller(gint vid,
					  gint pid,
					  BulkControllerCallbacks bulkcb,
					  void *user_data);

/**
 * @brief Opens the bulk interfaces for specific vid,pid for later access
 * @return
 * status of device
 */
gint
logibulkcontroller_open_device(LogiBulkController *logibulkcontroller);

/**
 * @brief Closes the bulk interfaces for the device
 * @return
 * Should also be called at destructor
 */
gint
logibulkcontroller_close_device(LogiBulkController *logibulkcontroller);

/**
 * @brief Method for sending file using UPD bulk interface
 * @param file_data - file data to send
 * @param size - size of byte array data.
 * @param start_update - to enable/disable firmware update upon file transfer. By default set to
 * false
 */
gint
logibulkcontroller_send_file_upd(LogiBulkController *logibulkcontroller,
				 GBytes *file_data,
				 gsize size,
				 gboolean start_update);

/**
 * @brief Method for sending data using Sync Agent bulk interface
 * @param data - the byte array to send.
 * @param size - size of byte array data.
 *@return : retunns a pair of values. 1st value is transaction ID of the request and 2nd value is
 *the error code.
 */
ReturnValue *
logibulkcontroller_send_data_sync(LogiBulkController *logibulkcontroller,
				  const gchar *data,
				  guint32 size);
#endif
