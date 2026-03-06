#include "dock_command_declaration.h"
#include "flash_id_usage_Information.h"
#include "libusb.h"
#include "usage_information_table.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

libusb_context *ctx = NULL;
volatile int device_connected = 1; // 1表示设备连接，0表示设备已断开
libusb_device_handle *devh = NULL;
struct timespec req = {0, waitMcuBusyTime};
int myclaim(int interface) {
  // int release_interface = libusb_release_interface(devh,interface);
  // printf("release_interface%d :
  // %s\n",interface,libusb_strerror(release_interface));
  int r = libusb_claim_interface(devh, interface);
  if (r != 0)
    printf("interface%d error: %s\n", interface, libusb_strerror(r));

  return r;
}
int init() {
  libusb_close(devh);
  libusb_exit(ctx);
  // int r = libusb_init_context(/*ctx=*/NULL, /*options=*/NULL,
  // /*num_options=*/0);
  int r = libusb_init(&ctx);
  if (r < 0) {
    // fprintf(stderr, "failed to initialise libusb %d - %s\n", r,
    // libusb_strerror(r));
    exit(1);
  }
  devh = libusb_open_device_with_vid_pid(NULL, 0x17ef, 0x111e);
  if (!devh) {
    errno = ENODEV;
    // printf("open device failed\n");
    return errno;
  }
  // 如果內核驅動佔用，先 detach
  if (libusb_kernel_driver_active(devh, 1) == 1) {
    r = libusb_detach_kernel_driver(devh, 1);
    if (r < 0) {
      // fprintf(stderr, "Detach kernel driver failed on iface %d:
      // %s\n",1,libusb_error_name(r));
      return r;
    }
  }
  if (libusb_kernel_driver_active(devh, 2) == 1) {
    r = libusb_detach_kernel_driver(devh, 2);
    if (r < 0) {
      // fprintf(stderr, "Detach kernel driver failed on iface %d:
      // %s\n",2,libusb_error_name(r));
      return r;
    }
  }
  int interface1 = myclaim(1);
  int interface2 = myclaim(2);

  return 0;
}
// 回调函数：处理设备的插入和断开事件
int LIBUSB_CALL hotplug_callback(struct libusb_context *ctx,
                                 struct libusb_device *dev,
                                 libusb_hotplug_event event, void *user_data) {
  struct libusb_device_descriptor desc;
  libusb_get_device_descriptor(dev, &desc);

  if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
    printf("Device disconnected: VID=0x%04x, PID=0x%04x\n", desc.idVendor,
           desc.idProduct);

    // 检查是否是目标设备
    if (desc.idVendor == DockVid && desc.idProduct == DockPid) {
      printf("Target device disconnected! Terminating program...\n");
      exit(-1);
    }
  }

  return 0; // 返回 0 表示回调未被注销
}
void *usb_event_thread(void *arg) {
  while (device_connected) {
    libusb_handle_events_completed(ctx, NULL); // 检测并触发 USB 热插拔回调事件
  }
  return NULL;
}

void SetFeature(uint8_t *cmd, int interface) {
  int res;
  switch (interface) {
  case 1:
    res = libusb_control_transfer(devh, 0b00100001, 0x09, 0x0300 + cmd[0],
                                  interface, cmd, Interface1Length, 0);
    // printf("set: %d\n",res);
    break;
  case 2:
    res = libusb_control_transfer(devh, 0b00100001, 0x09, 0x0300 + cmd[0],
                                  interface, cmd, Interface2Length, 0);
    // printf("set: %d\n",res);
    break;
  default:
    break;
  }

  // if(res < 0)
  //     printf("SetFeature failed : %d\n", res);
}

void GetFeature(uint8_t *cmd, int interface) {
  int res;
  switch (interface) {
  case 1:
    res = libusb_control_transfer(devh, 0b10100001, 0x01, 0x0300 + cmd[0],
                                  interface, cmd, Interface1Length, 0);
    // printf("get: %d\n",res);
    break;
  case 2:
    res = libusb_control_transfer(devh, 0b10100001, 0x01, 0x0300 + cmd[0],
                                  interface, cmd, Interface2Length, 0);
    // printf("get: %d\n",res);
    break;
  default:
    break;
  }

  // if(res < 0)
  //     printf("GetFeature failed : %d\n", res);
}
int Function1(uint8_t CmdClass, uint8_t CmdId, uint8_t FlashId, uint8_t *data,
              int dataSize, uint8_t *output) {
  int interface = 1;
  int PacketSize = Interface1Length;

  uint8_t *cmd = (uint8_t *)calloc(PacketSize, sizeof(uint8_t));
  uint8_t *cmd2 = (uint8_t *)calloc(PacketSize, sizeof(uint8_t));

  cmd2[0] = 0x00;

  // Header
  cmd[0] = Target_Status_Defult;
  cmd[1] = dataSize;
  cmd[2] = CmdClass;
  cmd[3] = CmdId;
  cmd[4] = FlashId;
  cmd[5] = 0x00; // reseved

  // Body

  if (dataSize < 0 || dataSize > PacketSize - 6) {
    free(cmd);
    free(cmd2);
    return PARAM_ERROR;
  }

  if (dataSize > 0 && data == NULL) {
    free(cmd);
    free(cmd2);
    return PARAM_ERROR;
  }

  for (int i = 0; i < dataSize; i++)
    cmd[6 + i] = data[i];

  SetFeature(cmd, interface);
  for (int i = 0; i < PacketSize; i++)
    printf("%02X ", cmd[i]);
  printf("\n");
  printf("******************SetFeature for interface 1 PackagetSize = "
         "%d******************",
         PacketSize);
  printf("\n");
  free(cmd);

  int count = 0;

  do {
    GetFeature(cmd2, interface);
    for (int i = 0; i < PacketSize; i++)
      printf("%02X ", cmd2[i]);
    printf("\n");

    printf("******************GetFeature for interface 1 PackagetSize "
           "%d******************",
           PacketSize);
    printf("\n");
    switch (cmd2[0]) {
    case CommandDefault:
      // printf("Function error: %d",REPORT_DATA_FAILED);
      free(cmd2);
      return REPORT_DATA_FAILED;
    case CommandBusy:
      count++;
      nanosleep(&req, NULL);
      break;
    case CommandSuccess:
      if (cmd2[4] != FlashId) {
        free(cmd2);
        // printf("Function error: %d",REPORT_DATA_FAILED);
        return REPORT_DATA_FAILED;
      } else {
        for (int i = 0; i < PacketSize; i++) {
          output[i] = cmd2[i];
        }
        free(cmd2);
        return SUCCESS;
      }

    case CommandFaliure:
      // printf("Function error: %d",COMMAND_FALIURE);
      free(cmd2);
      return COMMAND_FALIURE;
    case CommandTimeout:
      // printf("Function error: %d",COMMAND_TIMEOUT);
      free(cmd2);
      return COMMAND_TIMEOUT;
    case CommandNotSupport:
      // printf("Function error: %d",AP_TOOL_FAILED);
      free(cmd2);
      return AP_TOOL_FAILED;
    }
  } while (count < maxRetry);

  // printf("Function error: %d",COMMAND_OVER_RETRY_TIMES);
  free(cmd2);
  return COMMAND_OVER_RETRY_TIMES;
}
int Function2(uint8_t CmdClass, uint8_t CmdId, uint8_t FlashId, uint8_t *data,
              int dataSize, uint8_t *output) {
  int interface = 2;
  int PacketSize = Interface2Length;
  uint8_t *cmd = (uint8_t *)calloc(PacketSize, sizeof(uint8_t));
  uint8_t *cmd2 = (uint8_t *)calloc(PacketSize, sizeof(uint8_t));

  if (!cmd || !cmd2) {
    if (cmd)
      free(cmd);
    if (cmd2)
      free(cmd2);
    return PARAM_ERROR; // 你可換成你的 error code
  }

  if (dataSize < 0 || dataSize > (PacketSize - 7)) {
    free(cmd);
    free(cmd2);
    return PARAM_ERROR;
  }
  if (dataSize > 0 && data == NULL) {
    free(cmd);
    free(cmd2);
    return PARAM_ERROR;
  }
  if (output == NULL) {
    free(cmd);
    free(cmd2);
    return PARAM_ERROR;
  }

  // Set Report ID
  cmd[0] = 0x10;
  cmd2[0] = 0x10;

  // Header
  cmd[1] = Target_Status_Defult;
  cmd[2] = dataSize;
  cmd[3] = CmdClass;
  cmd[4] = CmdId;
  cmd[5] = FlashId;
  cmd[6] = 0x00; // reseved

  // Body
  for (int i = 0; i < dataSize; i++)
    cmd[7 + i] = data[i];

  SetFeature(cmd, interface);
  for (int i = 0; i < PacketSize; i++)
    printf("%02X ", cmd[i]);
  printf("\n");
  printf("******************SetFeature for interface 2 PackagetSize = "
         "%d******************",
         PacketSize);
  printf("\n");
  free(cmd);

  int count = 0;

  do {
    GetFeature(cmd2, interface);
    for (int i = 0; i < PacketSize; i++)
      printf("%02X ", cmd2[i]);
    printf("\n");
    printf("******************GetFeature for interface 2 PackagetSize = "
           "%d******************",
           PacketSize);
    printf("\n");
    switch (cmd2[0 + ReportIdOffset]) {
    case CommandDefault:
      // printf("Function error: %d",REPORT_DATA_FAILED);
      free(cmd2);
      return REPORT_DATA_FAILED;
    case CommandBusy:
      count++;
      nanosleep(&req, NULL);
      break;
    case CommandSuccess:
      if (cmd2[4 + ReportIdOffset] != FlashId) {
        // printf("Function error: %d",REPORT_DATA_FAILED);
        return REPORT_DATA_FAILED;
      } else {
        for (int i = 0; i < PacketSize; i++) {
          output[i] = cmd2[i];
        }
        free(cmd2);
        return SUCCESS;
      }

    case CommandFaliure:
      // printf("Function error: %d",COMMAND_FALIURE);
      free(cmd2);
      return COMMAND_FALIURE;
    case CommandTimeout:
      // printf("Function error: %d",COMMAND_TIMEOUT);
      free(cmd2);
      return COMMAND_TIMEOUT;
    case CommandNotSupport:
      // printf("Function error: %d",AP_TOOL_FAILED);
      free(cmd2);
      return AP_TOOL_FAILED;
    }
  } while (count < maxRetry);

  free(cmd2);
  // printf("Function error: %d",COMMAND_OVER_RETRY_TIMES);
  return COMMAND_OVER_RETRY_TIMES;
}

uint8_t *GetCommandBody1(uint8_t *data) {
  int size = data[1];
  uint8_t *res = (uint8_t *)calloc(size, sizeof(uint8_t));
  for (int i = 0; i < size; i++)
    res[i] = data[6 + i];

  return res;
}
uint8_t *GetCommandBody2(uint8_t *data) {
  int size = data[2];
  uint8_t *res = (uint8_t *)calloc(size, sizeof(uint8_t));
  for (int i = 0; i < size; i++)
    res[i] = data[7 + i];

  return res;
}