/*
 * proto_manager.h
 *
 *  Created on: Jul 29, 2021
 *      Author: edwin
 */

#ifndef PROTO_MANAGER_H_
#define PROTO_MANAGER_H_

#include <glib.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "device_common.h"
#include "usb_msg.pb-c.h"

/*! Contains the packed data
         data - the packed data.  Caller should deallocate this after use
         len - length of data
 */
typedef struct _Message {
  gchar * data;
  unsigned long len;
} Message;


typedef union {
  gchar device_info[4200];
  gboolean hand_shake_event;
  gboolean transition_to_device_mode;
} Decoded_Data;

typedef enum {
  kProtoId_UnknownId,
  kProtoId_GetDeviceInfoResponse,
  kProtoId_TransitionToDeviceModeResponse,
  kProtoId_Ack,
  kProtoId_KongEvent,
  kProtoId_HandshakeEvent,
  kProtoId_CrashDumpAvailableEvent
} Proto_id;

/*!
* @brief Set header values
* @param header_msg pointer to Logi__Device__Proto__Header
*/
void proto_manager_set_header(Logi__Device__Proto__Header* header_msg) ;

/*!
 * @brief Generate a GetDeviceInfo request
 * @param message - struct that will contain the message data.  The caller have to free the data after use.
 * @return 0 if success, otherwise the error code
 */
int proto_manager_generate_get_device_info_request(Message* message);

/*!
 * @brief Generate a TransitionToDeviceMode request
 * @param message - struct that will contain the message data.  The caller have to free the data after use.
 * @return 0 if success, otherwise the error code
 */
int proto_manager_generate_transition_to_device_mode_request(Message* message);

/*!
* @brief decode the incoming message
* @param data - contains the incoming message to be parsed
* @param len - size of the array pointer message
* @param decoded_data - decoded data
* @return the proto id
*/
Proto_id proto_manager_decode_message(const gchar* data, guint32 len, Decoded_Data* decoded_data);

/*!
* @brief Parse the device info
* @param data - string containing the device info
* @param dev_info - contains the device information
* @return true if parsing is successful
*/
gboolean proto_manager_parse_device_info(const gchar* data, device_info* dev_info);


#endif /* PROTO_MANAGER_H_ */
