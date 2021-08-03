#include "bulk_controller.h"
#include "bulk_util.h"

void*                   logibulkcontroller_thread_write_buffer          (void* thread_data);
void*                   logibulkcontroller_thread_send_data             (void *thread_data);
void*                   logibulkcontroller_thread_read_data             (void *thread_data);
void*                   logibulkcontroller_thread_send_upd_file         (void* thread_data);
void*                   logibulkcontroller_thread_read_sync_data        (void* data);
static gboolean         logibulkcontroller_is_sync_idle                 (void);
static gint             logibulkcontroller_init_transfer                (void);
static gint             logibulkcontroller_start_transfer               (GBytes* file_data);
static gint             logibulkcontroller_finish_transfer              (void);

typedef struct _LogiBulkControllerPrivate
{
        BulkControllerDevice                    ctrl_device;

        gint                                    packet_length;
        guchar                                  base64hash[BASE64_LENGTH];
        gboolean                                do_update;
        gboolean                                is_transfer_in_progress;
        gboolean                                is_upd_transfer_in_progress;
        guint32                                 transaction_id;
        guint                                   file_transfer_status;
        gint                                    file_size;
        gchar                                   *data_received_async;

        UsbPacket                               data_packet;
        UsbPacketSync                           data_packet_sync;
        UsbPacket                               recv_pckt;

        GThread                                 *send_file_upd_thread;
        GThread                                 *read_sync_data_thread;
        GThread                                 *read_buffer_thread;
        GThread                                 *send_data_thread;
        GThread                                 *write_buffer_thread;

        GQueue                                  *write_buffer_queue;
        GQueue                                  *read_data_queue;
        GQueue                                  *send_data_queue;

        Lock                                    lock[THREAD_COUNT];
        GBytes                                  *file_buffer;
        ReturnValue                             ret;
        ApiUserData                             *user_data;
        BulkControllerCallbacks                 callback;
}LogiBulkControllerPrivate;

LogiBulkControllerPrivate* priv;
static void logibulkcontroller_start_listening_sync (void);
static void logibulkcontroller_stop_listening_sync  (void);
static void logibulkcontroller_finalize             (GObject *obj);

G_DEFINE_TYPE_WITH_CODE (LogiBulkController, logibulkcontroller, G_TYPE_OBJECT, G_ADD_PRIVATE(LogiBulkController));

static void
logibulkcontroller_class_init (LogiBulkControllerClass* klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        g_debug ("Class constructor");
        object_class->finalize = logibulkcontroller_finalize;
}

static void
logibulkcontroller_init (LogiBulkController* self)
{
        g_debug ("Object constructor");
        priv = logibulkcontroller_get_instance_private (self);
        priv->read_data_queue = g_queue_new ();
        priv->send_data_queue = g_queue_new ();
        priv->write_buffer_queue = g_queue_new ();
        g_cond_init (&priv->lock[SEND_DATA_THREAD].cond);
        g_cond_init (&priv->lock[READ_DATA_THREAD].cond);
        g_cond_init (&priv->lock[WRITE_BUFFER_THREAD].cond);
        g_cond_init (&priv->lock[SEND_DATA_SYNC_THREAD].cond);
        priv->is_upd_transfer_in_progress = FALSE;
}

static void
logibulkcontroller_finalize (GObject *obj) {
        g_debug ("Finalize is called");
        g_queue_free (priv->read_data_queue);
        g_queue_free (priv->send_data_queue);
        g_queue_free (priv->write_buffer_queue);
        g_cond_clear (&priv->lock[SEND_DATA_THREAD].cond);
        g_cond_clear (&priv->lock[READ_DATA_THREAD].cond);
        g_cond_clear (&priv->lock[WRITE_BUFFER_THREAD].cond);
        g_cond_clear (&priv->lock[SEND_DATA_SYNC_THREAD].cond);
        G_OBJECT_CLASS (logibulkcontroller_parent_class) -> finalize (obj);
}

LogiBulkController*
logibulkcontroller_create_bulk_controller (gint vid, gint pid, BulkControllerCallbacks bulkcb, void *user_data)
{
        LogiBulkController* obj = g_object_new (TYPE_BULKCONTROLLER, NULL);
        g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
        priv = logibulkcontroller_get_instance_private (obj);
        priv->ctrl_device.vid = vid;
        priv->ctrl_device.pid = pid;
        priv->callback = bulkcb;
        priv->user_data = (ApiUserData*) user_data;
        return obj;
}

static gint
logibulkcontroller_get_logi_bulk_endpoint (void)
{
        GUsbEndpoint *ep;
        GError *error = NULL;
        g_autoptr(GPtrArray) intfs = NULL;
        g_autoptr(GPtrArray) endpoints = NULL;
        guint i;
        guint j;

        intfs = g_usb_device_get_interfaces (priv->ctrl_device.device, &error);
        if (NULL == intfs) {
                g_warning ("Interface is null for the device.");
                return ERRORCODE_INVALID_INTERFACE;
        }
        for (i = 0; i < intfs->len; i++) {
                GUsbInterface *intf = g_ptr_array_index (intfs, i);
                if (g_usb_interface_get_class (intf) == USB_INTERFACE_CLASS &&
                                          g_usb_interface_get_protocol (intf) == USB_INTERFACE_PROTOCOL) {
                        if (SYNC_INTERFACE_SUBPROTOCOL_ID == g_usb_interface_get_subclass (intf)) {
                                priv->ctrl_device.sync_interface_number = g_usb_interface_get_number (intf);
                                endpoints = g_usb_interface_get_endpoints (intf);
                                if (NULL == endpoints || 0 == endpoints->len) {
                                        continue;
                                }
                                for (j = 0; j < endpoints->len;j++) {
                                        ep = g_ptr_array_index (endpoints, j);
                                        (j != OUT_ENDPOINT) ?
                                                (priv->ctrl_device.sync_ep[IN_ENDPOINT] = g_usb_endpoint_get_address (ep)) :
                                                (priv->ctrl_device.sync_ep[OUT_ENDPOINT] = g_usb_endpoint_get_address (ep));
                                }
                                g_debug ("Interface = %u Address In = 0x%x Address Out = 0x%x",
                                                                        priv->ctrl_device.sync_interface_number,
                                                                        priv->ctrl_device.sync_ep[IN_ENDPOINT],
                                                                        priv->ctrl_device.sync_ep[OUT_ENDPOINT]);
                        } else if (UPD_INTERFACE_SUBPROTOCOL_ID == g_usb_interface_get_subclass (intf)){
                                priv->ctrl_device.sync_interface_number = g_usb_interface_get_number (intf);
                                endpoints = g_usb_interface_get_endpoints (intf);
                                if (NULL == endpoints || 0 == endpoints->len) {
                                        continue;
                                }
                                for (j = 0; j < endpoints->len;j++) {
                                        ep = g_ptr_array_index (endpoints, j);
                                        (j != OUT_ENDPOINT) ?
                                                (priv->ctrl_device.upd_ep[IN_ENDPOINT] = g_usb_endpoint_get_address (ep)) :
                                                (priv->ctrl_device.upd_ep[OUT_ENDPOINT] = g_usb_endpoint_get_address (ep));
                                }
                                g_debug ("Interface = %u Address In = 0x%x Address Out = 0x%x",
                                                                        priv->ctrl_device.sync_interface_number,
                                                                        priv->ctrl_device.upd_ep[IN_ENDPOINT],
                                                                        priv->ctrl_device.upd_ep[OUT_ENDPOINT]);
                        }
                }
        }
        return ERRORCODE_NO_ERROR;
}

static gint
logibulkcontroller_claim_interface (gboolean do_claim)
{
        gboolean ret;
        GError *error = NULL;

        if (do_claim) {
                ret = g_usb_device_claim_interface (priv->ctrl_device.device, priv->ctrl_device.upd_interface_number,
                                                            G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
                                                            &error);
                if (!ret) {
                        g_warning ("Failed to claim the interface %d", ret);
                        return ERRORCODE_CLAIM_INTERFACE_FAILED;
                }
                ret = g_usb_device_claim_interface (priv->ctrl_device.device, priv->ctrl_device.sync_interface_number,
                                                            G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
                                                            &error);
                if (!ret) {
                        g_warning ("Failed to claim the interface %d", ret);
                        return ERRORCODE_CLAIM_INTERFACE_FAILED;
                }
                g_debug ("Claim Interface Success");
        } else {
                ret = g_usb_device_release_interface (priv->ctrl_device.device, priv->ctrl_device.upd_interface_number,
                                                              G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
                                                              &error);
                if (!ret) {
                        g_warning ("Failed to release the interface %d", ret);
                        return ERRORCODE_RELEASE_INTERFACE_FAILED;
                }
                ret = g_usb_device_release_interface (priv->ctrl_device.device, priv->ctrl_device.sync_interface_number,
                                                              G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
                                                              &error);
                if (!ret) {
                        g_warning ("Failed to release the interface %d", ret);
                        return ERRORCODE_RELEASE_INTERFACE_FAILED;
                }
                g_debug ("Release interface Success");
        }
        return ERRORCODE_NO_ERROR;
}

gint
logibulkcontroller_open_device (LogiBulkController *obj)
{
        guint i;
        gint pid;
        gint vid;
        gint error_code = ERRORCODE_NO_ERROR;
        GPtrArray *array;
        GUsbContext *ctx;
        GUsbDevice *device;
        GError *error = NULL;

        LOGFN
        ctx = g_usb_context_new (&error);
        g_usb_context_set_debug (ctx, G_LOG_LEVEL_ERROR);
        array = g_usb_context_get_devices (ctx);
        for (i = 0; i < array->len; i++) {
                device = G_USB_DEVICE (g_ptr_array_index (array, i));
                pid = g_usb_device_get_pid (device);
                vid = g_usb_device_get_vid (device);
                if ((pid == priv->ctrl_device.pid) && (vid == priv->ctrl_device.vid)) {
                        break;
                }
        }
        if (i == array->len) {
                g_warning ("Did not find the device. Please connect the device %04x:%04x", (guint)priv->ctrl_device.vid,
                                                                                                (guint)priv->ctrl_device.pid);
                return ERRORCODE_UNKNOWN_DEVICE;
        }
        g_debug ("Found the device %04x:%04x", (guint)priv->ctrl_device.vid, (guint)priv->ctrl_device.pid);
        priv->ctrl_device.device = device;

        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_get_logi_bulk_endpoint ())) {
	              return error_code;
        }

        /* close not opened */
        error_code = g_usb_device_close (priv->ctrl_device.device, &error);
        g_assert_error (error, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_NOT_OPEN);
        g_assert (!error_code);
        g_clear_error (&error);

        /* open */
        error_code = g_usb_device_open (priv->ctrl_device.device, &error);
        g_assert_no_error (error);
        g_assert (error_code);
        g_debug ("Device open successful");
        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_claim_interface (TRUE))) {
                return error_code;
        }

        priv->is_transfer_in_progress = TRUE;
        logibulkcontroller_start_listening_sync ();
        return error_code;
}

static void
logibulkcontroller_transfer_cb_wrapper (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
        gboolean ret;
        GError *error = NULL;
        GUsbDeviceAsyncHelper *helper = (GUsbDeviceAsyncHelper *) user_data;
        UsbPacketSync *data = (UsbPacketSync*) helper->buffer;

        ret = g_usb_device_bulk_transfer_finish (G_USB_DEVICE (source_object), res, &error);
        if (!ret) {
                g_error ("%s", error->message);
                g_error_free (error);
                return;
        }
        if (data->cmd != 0) {
                if (data ->cmd == CMD_CHECK_BUFFERSIZE) {
                        g_debug ("Buffer size is 16k");
                } else if (data->cmd >= CMD_ACK) {
                        g_queue_push_head (priv->write_buffer_queue, data);
                        UNLOCK (&priv->lock[WRITE_BUFFER_THREAD].cond);
                } else if ((data->cmd == CMD_BUFFER_READ) || (data->cmd == CMD_UNINIT_BUFFER)) {
                        g_queue_push_head (priv->read_data_queue, data);
                        UNLOCK (&priv->lock[READ_DATA_THREAD].cond);
                }
        }
        g_main_loop_quit (helper->loop);
}

void*
logibulkcontroller_thread_read_sync_data (void* data)
{
        GCancellable *cancellable = NULL;
        GUsbDeviceAsyncHelper *helper;

        while (priv->is_transfer_in_progress) {
                helper = g_slice_new0 (GUsbDeviceAsyncHelper);
                helper->buffer_len = BUFFER_SIZE_16K;
                helper->buffer = g_new0 (gchar, helper->buffer_len);
                helper->loop = g_main_loop_new (NULL, FALSE);
                g_usb_device_bulk_transfer_async (priv->ctrl_device.device,
                                                      priv->ctrl_device.sync_ep[IN_ENDPOINT], (guint8 *)helper->buffer,
                                                      helper->buffer_len, READ_ASYNC_TIME_OUT, cancellable,
                                                      logibulkcontroller_transfer_cb_wrapper, helper);
                g_main_loop_run (helper->loop);
	        g_main_loop_unref (helper->loop);
        }
        return NULL;
}

static gint
logibulkcontroller_send_data (guchar *write_buffer, guint32 length, gint interface_id)
{
        gsize transferred = 0;
        gint end_point, error_code = ERRORCODE_NO_ERROR;
        GError *error = NULL;
        GCancellable *cancellable = NULL;

        if (NULL == priv->ctrl_device.device) {
                g_warning ("Device object is null. Check for connection");
                return ERRORCODE_NO_DEVICE;
        }
        if ((NULL == write_buffer)) {
                g_warning ("Send data is null.");
                return ERRORCODE_INVALID_SEND_DATA;
        }
        if (BULK_INTERFACE_SYNC == interface_id) {
                end_point = priv->ctrl_device.sync_ep[OUT_ENDPOINT];
        } else if (BULK_INTERFACE_UPD == interface_id){
                end_point = priv->ctrl_device.upd_ep[OUT_ENDPOINT];
        } else {
                g_warning ("Interface is invalid. Interface should be either UPD(%d) or Sync(%d)", BULK_INTERFACE_UPD,
                                                                                                  BULK_INTERFACE_SYNC);
                return ERRORCODE_INVALID_INTERFACE;
        }
        if ((ERRORCODE_NO_ERROR != (error_code = g_usb_device_bulk_transfer(priv->ctrl_device.device,
                                                end_point, (guchar *)write_buffer, (gint)length, &transferred,
                                                WRITE_TIME_OUT, cancellable, &error)))) {
                /* We have added this as a solution. Some time when transaction is completed in kernel context,
                 * it will return to libusb context where it encounters some error thats not known to libusb.
                 * So we ignored it as transactions as completed properly.
                */
                if (LIBUSB_ERROR_OTHER != error_code) {
                        g_warning ("Bulk transfer failed. ErrorCode: %d", error_code);
                }
                error_code = (LIBUSB_ERROR_OTHER == error_code) ? ERRORCODE_NO_ERROR :
                                                                        ERRORCODE_ENDPOINT_TRANSFER_FAILED;
        }
        return error_code;
}

static gint
logibulkcontroller_read_data (guchar *read_buffer, guint32 length, gint interface_id, guint timeout)
{
        gsize received_length = 0;
        gint end_point, error_code = ERRORCODE_NO_ERROR;
        GError *error = NULL;
        GCancellable *cancellable = NULL;

        if (NULL == priv->ctrl_device.device) {
                g_warning ("Device object is null. Check for connection");
                return ERRORCODE_NO_DEVICE;
        }
        if (NULL == read_buffer) {
                g_warning ("Read buffer is address is null. Cannot read on NULL address");
                return ERRORCODE_READ_BUFFER_INVALID;
        }
        if (0 == length) {
                g_warning ("Transfer length is zero. Cannot read data of length zero");
                return ERRORCODE_READ_ZERO_SIZE;
        }
        if (BULK_INTERFACE_SYNC == interface_id) {
                end_point = priv->ctrl_device.sync_ep[IN_ENDPOINT];
        } else if (BULK_INTERFACE_UPD == interface_id) {
                end_point = priv->ctrl_device.upd_ep[IN_ENDPOINT];
        } else {
                g_warning ("Interface is invalid. Interface should be either UPD(%d) or Sync(%d)", BULK_INTERFACE_UPD,
                                                                                                  BULK_INTERFACE_SYNC);
                return ERRORCODE_INVALID_INTERFACE;
        }
        if ((ERRORCODE_NO_ERROR != (error_code = g_usb_device_bulk_transfer (priv->ctrl_device.device,
                                                end_point, read_buffer, (gint)MAX_DATA_SIZE,
                                                &received_length, timeout, cancellable, &error)))) {
                /* We have added this as a solution. Some time when transaction is completed in kernel context,
                 * it will return to libusb context where it encounters some error thats not known to libusb.
                 * So we ignored it as transactions as completed properly.
                */
                if (error_code != LIBUSB_ERROR_OTHER) {
                        g_warning ("Bulk transfer failed. ErrorCode: %d", error_code);
                }
                error_code = (LIBUSB_ERROR_OTHER == error_code) ? ERRORCODE_NO_ERROR :
                                                                        ERRORCODE_ENDPOINT_RECEIVE_FAILED;
        }
  return error_code;
}

static gint
logibulkcontroller_check_ack (guint32 cmd, guint32 size)
{
        gint error_code = ERRORCODE_NO_ERROR;
        guint *received_cmd_data;

        memset (&priv->recv_pckt, 0, sizeof(priv->recv_pckt));
        if ( ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_read_data ((guchar*)&priv->recv_pckt,
                                                                                        size, BULK_INTERFACE_UPD,
                                                                                        (gint)READ_TIME_OUT))) {
                g_warning ("Error in reading the data %d", error_code);
                return ERRORCODE_ENDPOINT_RECEIVE_FAILED;
        }
        if (cmd == CMD_END_TRANSFER) {
                received_cmd_data = ((guint*)& (priv->recv_pckt.payload));
                priv->file_transfer_status = *(received_cmd_data + 1);
                g_debug ("Data = %X Status = %X", *received_cmd_data, priv->file_transfer_status);
        } else {
                received_cmd_data = (guint *)&(priv->recv_pckt.payload);
        }
        if ((CMD_ACK == priv->recv_pckt.cmd) &&  (*received_cmd_data == cmd)) {
                return error_code;
        }
        return ERRORCODE_INCORRECT_ORDER;
}

static gint
logibulkcontroller_create_upd_packet_and_send (guint32 cmd, guint32 length, void *data, gint *packet_len)
{
        gint error_code = ERRORCODE_NO_ERROR;
        EndTransferPkt end_pkt = {(guint)((priv->do_update) ? TRUE : FALSE), 0, MD5, {0}};

        memset(&(priv->data_packet), 0, sizeof(priv->data_packet));
        if (CMD_END_TRANSFER == cmd) {
                memcpy (end_pkt.base64hash, priv->base64hash, sizeof(priv->base64hash)-1);
                memcpy (&(priv->data_packet.payload), (guchar *)&end_pkt, length);
        }
        priv->data_packet.cmd = cmd;
        priv->data_packet.length = length;
        if (data) {
                memcpy (&(priv->data_packet.payload), (guchar *)data, length);
        }
        *packet_len = PACKET_HEADER_SIZE + length;

        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_send_data ((guchar*)&(priv->data_packet),
                                                                                *packet_len, BULK_INTERFACE_UPD))) {
                g_warning ("Error in writing the data to the endpoint. ErrorCode: %d", error_code);
        }
        return error_code;
}

static gint
logibulkcontroller_create_sync_packet_and_send (guint32 cmd, guint32 length, void *data, gint sequence_id)
{
        gint error_code = ERRORCODE_NO_ERROR;
        gint packet_length = 0;
        UsbPacketSync send_data_pckt;

        CREATE_PACKET(&send_data_pckt, cmd, length, sequence_id, data);
        packet_length = length + SYNC_PACKET_HEADER_SIZE;
        if ( ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_send_data ((guchar*)&send_data_pckt,
                                                                                packet_length, BULK_INTERFACE_SYNC))) {
          g_warning ("Error in writing the data to the endpoint. ErrorCode: %d", error_code);
        }
        return error_code;
}

static gint
logibulkcontroller_compute_hash (GBytes *data, guchar *base64hash)
{
        guchar md5buf[MD5_DIGEST_SIZE];
        gsize data_len = MD5_DIGEST_SIZE;

        GChecksum *checksum = g_checksum_new(G_CHECKSUM_MD5);
        const guchar *buf = (const guchar *)g_bytes_get_data(data, NULL);
	      if (NULL == buf) {
	                g_warning("File data is null. Cannot compute hash on null data");
	                return ERRORCODE_FILE_DATA_INVALID;
        }
        priv->file_size =  g_bytes_get_size(data);
        g_debug("Size of the file to be transferred = %d", priv->file_size);
        g_checksum_update(checksum, buf, g_bytes_get_size(data));
        g_checksum_get_digest (checksum, (guint8 *) &md5buf, &data_len);
        md5buf[16] = NULL_CHARACTER;
        g_stpcpy((gchar *)base64hash, (gchar *)g_base64_encode(md5buf, strlen((const gchar *)md5buf)));
        g_debug("Hash Value calculated : %s", priv->base64hash);
        return ERRORCODE_NO_ERROR;
}

static gint
logibulkcontroller_init_transfer (void)
{
        gint error_code = ERRORCODE_NO_ERROR;

        /* Sending INIT */
        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_create_upd_packet_and_send (CMD_INIT, 0, NULL,
                                                                                        &(priv->packet_length)))) {
                g_warning ("Error in writing init transfer packet %d", error_code);
                return error_code;
        }
        /* Receiving INIT ACK */
        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_check_ack (CMD_INIT, ACK_PKT_SIZE))) {
                g_warning ("Error in reading acknowledgement for init transfer packet %d", error_code);
                return error_code;
        }
        g_debug ("[SUCCESS] : CMD_INIT");
        return error_code;
}

static gint
logibulkcontroller_transfer_file_data (guchar *buf, guint size) {
        gint error_code = ERRORCODE_NO_ERROR;

        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_create_upd_packet_and_send (CMD_DATA_TRANSFER,
                                                                                size, buf, &priv->packet_length))) {
                g_warning ("Error in writing data transfer packet %d", error_code);
                return error_code;
        }
        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_check_ack (CMD_DATA_TRANSFER, ACK_PKT_SIZE))) {
                g_warning ("Error in reading acknowledgement for data transfer packet %d", error_code);
        }
        return error_code;
}

static gint
logibulkcontroller_start_transfer (GBytes* file_data) {
        gint error_code = ERRORCODE_NO_ERROR, packet_length = 0;
        guint packet_count = 1;
        guint cur_per = 0, last_per = 0, total_data_sent = 0;
        gfloat progress = 0.0f;
        guint64 trf_size = 0;
        gsize length;
        guchar *buf;

        trf_size = priv->file_size;
        g_debug ("Size of file to be transferred = %lu", trf_size);
        /* Transfer Sent*/
        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_create_upd_packet_and_send (CMD_START_TRANSFER,
                                                                                        sizeof(trf_size),
                                                                                        &trf_size,
                                                                                        &priv->packet_length))) {
                g_warning ("Error in writing start transfer packet %d", error_code);
                return error_code;
        }
        /* Receiving  ACK For Transfer */
        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_check_ack (CMD_START_TRANSFER, ACK_PKT_SIZE))) {
                g_warning ("Error in reading acknowledgement for start transfer packet %d", error_code);
                return error_code;
        }
        g_debug ("[SUCCESS] : CMD_START_TRANSFER");
        priv->callback.bulk_transfer_cb (TRANSFER_STARTED, cur_per, BULK_INTERFACE_UPD, priv->user_data);

        buf = (guchar *) g_bytes_get_data (file_data, NULL);
	      if (NULL == buf) {
	                g_warning ("File data is null. Please provide the correct file");
	                return ERRORCODE_FILE_DATA_INVALID;
	      }
        length = priv->file_size;
        while (total_data_sent != length) {
                if (length - total_data_sent > PAYLOAD_SIZE) {
                        packet_length = PAYLOAD_SIZE;
                } else {
                        packet_length = priv->file_size - total_data_sent;
                }
                if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_transfer_file_data ((buf + total_data_sent),
                                                                                  packet_length))) {
                        g_warning ("Failed to send data packet : PACKET = %u", packet_count);
                        priv->callback.bulk_transfer_cb (TRANSFER_FAILED, cur_per, BULK_INTERFACE_UPD, priv->user_data);
                        return error_code;
                }
                total_data_sent += packet_length;
                progress = (total_data_sent * 100.0f) /((gfloat)priv->file_size);
                cur_per = (gint)progress;
                if (cur_per != last_per) {
                        priv->callback.bulk_transfer_cb (TRANSFER_INPROGRESS, cur_per, BULK_INTERFACE_UPD, priv->user_data);
                }
                last_per = cur_per;
                packet_count++;
        }
        g_debug ("\n[SUCCESS]: CMD_DATA_TRANSFER");
        return error_code;
}

static gint
logibulkcontroller_finish_transfer (void)
{
        gint error_code = ERRORCODE_NO_ERROR;

        /* Sending End Transfer*/
        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_create_upd_packet_and_send (CMD_END_TRANSFER,
                                                                                        sizeof(EndTransferPkt),
                                                                                        NULL, &priv->packet_length))) {
                        g_warning ("Error in writing end transfer packet %d", error_code);
                        return error_code;
        }
        /*Waiting for End Transfer Ack*/
        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_check_ack (CMD_END_TRANSFER,
                                                                        ACK_PKT_SIZE + ACK_PACKET_FOR_HASH_VALUE))) {
                        g_warning ("Error in reading acknowledgement for end transfer packet %d", error_code);
                        return error_code;
        }
        g_debug ("[SUCCESS]: CMD_END_TRANSFER");
        /*Send Uninit*/
        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_create_upd_packet_and_send (CMD_UNINIT, 0, NULL,
                                                                                                &priv->packet_length))) {
                        g_warning ("Error in writing finsh transfer packet %d", error_code);
                        return error_code;
        }
        /*Waitng for UninitAck*/
        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_check_ack (CMD_UNINIT, ACK_PKT_SIZE))) {
                        g_warning ("Error in reading acknowledgement for finish transfer packet %d", error_code);
                        return error_code;
        }
        g_debug ("[SUCCESS] : CMD_UNINIT");
        return error_code;
}

void*
logibulkcontroller_thread_send_upd_file (void* data)
{
        gint error_code = ERRORCODE_NO_ERROR;
        gchar* err_str = g_malloc0 (ERROR_STRING_SIZE);

        priv->callback.bulk_transfer_cb (TRANSFER_HASH_STARTED, 0, BULK_INTERFACE_UPD, priv->user_data);
        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_compute_hash (priv->file_buffer,
                                                                                priv->base64hash))) {
                g_warning ("Failed to compute hash for the given file data");
                priv->is_upd_transfer_in_progress = FALSE;
                err_str = g_strdup ("Failed to compute hash for the given file data");
                priv->callback.bulk_error_cb(error_code, BULK_INTERFACE_UPD, err_str, (guint32)strlen(err_str),
                                                                                                priv->user_data);
                priv->callback.bulk_transfer_cb (TRANSFER_FAILED, 0, BULK_INTERFACE_UPD, priv->user_data);
                return NULL;
        }
        priv->callback.bulk_transfer_cb (TRANSFER_INIT_STARTED, 0, BULK_INTERFACE_UPD, priv->user_data);
        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_init_transfer ())) {
                g_warning("Error in init transfer");
                priv->is_upd_transfer_in_progress = FALSE;
                err_str = g_strdup ("Error in init transfer");
                priv->callback.bulk_error_cb(error_code, BULK_INTERFACE_UPD, err_str, (guint32)strlen(err_str),
                                                                                                priv->user_data);
                priv->callback.bulk_transfer_cb (TRANSFER_FAILED, 0, BULK_INTERFACE_UPD, priv->user_data);
                return NULL;
        }
        if (ERRORCODE_NO_ERROR != (error_code = logibulkcontroller_start_transfer (priv->file_buffer))) {
                g_warning("Error in start transfer");
                priv->is_upd_transfer_in_progress = FALSE;
                err_str = g_strdup ("Error in start transfer");
                priv->callback.bulk_error_cb(error_code, BULK_INTERFACE_UPD, err_str, (guint32)strlen(err_str),
                                                                                                priv->user_data);
                priv->callback.bulk_transfer_cb (TRANSFER_FAILED, 0, BULK_INTERFACE_UPD, priv->user_data);
                return NULL;
        }
        if (ERRORCODE_NO_ERROR != (!logibulkcontroller_finish_transfer ()) &&
                                                        (priv->file_transfer_status != TRANSFER_SUCCESS)) {
                g_warning("Error in finish transfer");
                priv->is_upd_transfer_in_progress = FALSE;
                err_str = g_strdup ("Error in finish transfer");
                priv->callback.bulk_error_cb(error_code, BULK_INTERFACE_UPD, err_str, (guint32)strlen(err_str),
                                                                                                priv->user_data);
                priv->callback.bulk_transfer_cb (TRANSFER_FAILED, 0, BULK_INTERFACE_UPD, priv->user_data);
                return NULL;
        }
        priv->is_upd_transfer_in_progress = FALSE;
        priv->callback.bulk_transfer_cb (TRANSFER_COMPLETED, 100, BULK_INTERFACE_UPD, priv->user_data);
        priv->is_transfer_in_progress = TRUE;
        logibulkcontroller_start_listening_sync ();
        g_debug ("Starting the threads for sync transfer");
        return NULL;
}

void*
logibulkcontroller_thread_read_data (void *thread_data)
{
        gint cmd_data;

        while(priv->is_transfer_in_progress) {
                if (!g_queue_is_empty(priv->read_data_queue)) {
                        UsbPacketSync * data = g_queue_pop_tail(priv->read_data_queue);
                        if (CMD_BUFFER_READ == data->cmd) {
                                        cmd_data = CMD_BUFFER_READ;
                                        priv->callback.bulk_read_cb_sync((const gchar *) data->payload, data->length,
                                                                                                      priv->user_data);
                        } else if (data->cmd == CMD_UNINIT_BUFFER) {
                                        cmd_data = CMD_UNINIT_BUFFER;
                        } else {
                                        continue;
                        }
                        logibulkcontroller_create_sync_packet_and_send (CMD_ACK, sizeof(cmd_data), &cmd_data, 0);
                } else {
                        g_mutex_lock (&priv->lock[READ_DATA_THREAD].mutex);
                        LOCK(&priv->lock[READ_DATA_THREAD].cond, &priv->lock[READ_DATA_THREAD].mutex);
                        g_mutex_unlock(&priv->lock[READ_DATA_THREAD].mutex);
                }
        }
        return NULL;
}

void*
logibulkcontroller_thread_send_data (void *thread_data)
{
        while (priv->is_transfer_in_progress) {
                if (!g_queue_is_empty(priv->send_data_queue)) {
                        UsbPacketSync *data = g_queue_pop_tail(priv->send_data_queue);
                        g_queue_push_head(priv->write_buffer_queue, data);
                        UNLOCK(&priv->lock[WRITE_BUFFER_THREAD].cond);
                        g_mutex_lock (&priv->lock[SEND_DATA_THREAD].mutex);
                        if (!LOCK(&priv->lock[SEND_DATA_THREAD].cond, &priv->lock[SEND_DATA_THREAD].mutex)){
                                gchar *err_str = (gchar*) "send data packet timed out";
                                priv->callback.bulk_error_cb(ERRORCODE_BUFFER_TRANSFER_FAILED, BULK_INTERFACE_SYNC, 
                                                                                         err_str, 
                                                                                         (guint32)strlen(err_str),
                                                                                         priv->user_data);
                                priv->callback.send_data_cb_sync(ERRORCODE_ENDPOINT_TRANSFER_FAILED, TRANSFER_FAILED,
                                                                                              priv->transaction_id,
                                                                                              priv->user_data);
                        }
                        g_mutex_unlock (&priv->lock[SEND_DATA_THREAD].mutex);
                } else {
                        g_mutex_lock (&priv->lock[SEND_DATA_SYNC_THREAD].mutex);
                        LOCK(&priv->lock[SEND_DATA_SYNC_THREAD].cond, &priv->lock[SEND_DATA_SYNC_THREAD].mutex);
                        g_mutex_unlock(&priv->lock[SEND_DATA_SYNC_THREAD].mutex);
                }
        }
        return NULL;
}

void*
logibulkcontroller_thread_write_buffer (void* thread_data)
{
        while (priv->is_transfer_in_progress) {
                if (!g_queue_is_empty(priv->write_buffer_queue)) {
                        UsbPacketSync * data = g_queue_pop_tail(priv->write_buffer_queue);
                        switch (data->cmd) {
                                case CMD_BUFFER_WRITE:
                                        priv->transaction_id = data->sequence_id;
                                        logibulkcontroller_create_sync_packet_and_send (CMD_BUFFER_WRITE,
                                                                                                data->length,
                                                                                                data->payload,
                                                                                                data->sequence_id);
                                        break;
                                case CMD_ACK:
                                        if (CMD_BUFFER_WRITE == atoi((const gchar*) data->payload)) {
                                          logibulkcontroller_create_sync_packet_and_send (CMD_UNINIT_BUFFER, 0, NULL,
                                                                                                                   0);
                                        } else if (CMD_UNINIT_BUFFER == atoi((const gchar *)data->payload)) {
                                          priv->callback.send_data_cb_sync(ERRORCODE_NO_ERROR, TRANSFER_SUCCESS,
                                                                                                priv->transaction_id,
                                                                                                priv->user_data);
                                          UNLOCK(&priv->lock[SEND_DATA_THREAD].cond);
                                        }
                                        break;
                                case CMD_NACK:
                                        //Failure callback
                                        g_warning("Nack packet received for the request");
                                        UNLOCK(&priv->lock[SEND_DATA_THREAD].cond);
                                        priv->callback.send_data_cb_sync(ERRORCODE_ENDPOINT_TRANSFER_FAILED,
                                                                                                TRANSFER_FAILURE,
                                                                                                priv->transaction_id,
                                                                                                priv->user_data);
                                default:
                                        break;
                        }
                } else {
                        g_mutex_lock (&priv->lock[WRITE_BUFFER_THREAD].mutex);
                        LOCK(&priv->lock[WRITE_BUFFER_THREAD].cond, &priv->lock[WRITE_BUFFER_THREAD].mutex);
                        g_mutex_unlock(&priv->lock[WRITE_BUFFER_THREAD].mutex);
                }
        }
        return NULL;
}

static gboolean
logibulkcontroller_is_sync_idle (void) {
        if (g_queue_is_empty(priv->write_buffer_queue) || !g_queue_is_empty(priv->send_data_queue)
                || !g_queue_is_empty(priv->read_data_queue)) {
                        return TRUE;
        }
        return FALSE;
}

gint
logibulkcontroller_send_file_upd (LogiBulkController *obj, GBytes* file_data, gsize size,
                                                                                        gboolean start_update)
{
        LOGFN
        priv->do_update = start_update;
        priv->file_buffer = file_data;
        if (priv->is_upd_transfer_in_progress) {
                g_warning("File transfer already in progress");
                return ERRORCODE_TRANSFER_INPROGRESS;
        }
        while (!logibulkcontroller_is_sync_idle()) {
                g_warning ("Transfers on the sync endpoint are in progress");
        }
        priv->is_transfer_in_progress = FALSE;
        logibulkcontroller_stop_listening_sync();
        g_debug ("Stopping the sync transfer threads");

        if (0 > (gint)size) {
                 g_warning("File name length invalid");
                 return ERRORCODE_INVALID_FILE_SIZE;
        }
        if (NULL == file_data) {
                g_warning("File data is null. Please provide the correct file");
                return ERRORCODE_FILE_DATA_INVALID;
        }
        priv->is_upd_transfer_in_progress = TRUE;
        priv->send_file_upd_thread = g_thread_new ("SendUPDThread", &logibulkcontroller_thread_send_upd_file, NULL);
        return ERRORCODE_NO_ERROR;
}

ReturnValue*
logibulkcontroller_send_data_sync (LogiBulkController *obj, const gchar *data, guint32 size)
{
        UsbPacketSync send_data_pckt;
        guint transaction_id = 0;
        priv->ret.transaction_id = 0;

        LOGFN
        if (priv->is_upd_transfer_in_progress) {
                g_warning("File transfer already is in progress. Cannot transfer the data.");
                priv->ret.error_code = ERRORCODE_TRANSFER_INPROGRESS;
                return &priv->ret;
        }
        if (NULL == data) {
                g_warning ("Data is null. Please provide the correct data to transfer.");
                priv->ret.error_code = ERRORCODE_INVALID_SEND_DATA;
                return &priv->ret;
        }
        if (0 >= size) {
                g_warning ("Size of the data is either 0 or less than 0.");
                priv->ret.error_code = ERRORCODE_SEND_DATA_SIZE_ZERO;
                return &priv->ret;
        }
        transaction_id = g_random_int_range(UINT16_MIN, __UINT16_MAX__);
        priv->ret.transaction_id = transaction_id;
        CREATE_PACKET (&send_data_pckt, CMD_BUFFER_WRITE, size, transaction_id, data);
        g_queue_push_head (priv->send_data_queue, &send_data_pckt);
        UNLOCK (&priv->lock[SEND_DATA_SYNC_THREAD].cond);
        g_debug ("Send data request pushed to queue. Request ID: %u", transaction_id);
        priv->ret.error_code = ERRORCODE_SEND_DATA_REQUEST_PUSHED_TO_QUEUE;
        return &priv->ret;
}

static void
logibulkcontroller_start_listening_sync (void)
{
        logibulkcontroller_create_sync_packet_and_send (CMD_CHECK_BUFFERSIZE, 0, 0, 0);
        priv->read_sync_data_thread = g_thread_new ("ReadSyncDataThread", &logibulkcontroller_thread_read_sync_data,
                                                                                                                 NULL);
        priv->read_buffer_thread = g_thread_new ("ReadDataThread", &logibulkcontroller_thread_read_data, NULL);
        priv->send_data_thread = g_thread_new ("SendDataThread", &logibulkcontroller_thread_send_data, NULL);
        priv->write_buffer_thread = g_thread_new ("WriteBufferThread", &logibulkcontroller_thread_write_buffer, NULL);
}

static void
logibulkcontroller_stop_listening_sync (void)
{
        g_thread_join (priv->read_sync_data_thread);
        g_thread_join (priv->read_buffer_thread);
        g_thread_join (priv->send_data_thread);
        g_thread_join (priv->write_buffer_thread);
}

gint
logibulkcontroller_close_device (LogiBulkController *obj)
{
        GError *error = NULL;

        LOGFN
        priv->is_transfer_in_progress = FALSE;
        if (priv->send_file_upd_thread) {
                g_thread_join (priv->send_file_upd_thread);
        }
        logibulkcontroller_claim_interface (FALSE);
        logibulkcontroller_stop_listening_sync();
        g_usb_device_close (priv->ctrl_device.device, &error);
        g_object_unref (priv->ctrl_device.device);
        if (NULL != obj) {
                g_object_unref (obj);
        }
        return ERRORCODE_NO_ERROR;
}