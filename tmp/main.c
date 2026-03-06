#include <errno.h>
#include <libusb-1.0/libusb.h>
#include <mcheck.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include "dock_command_declaration.h"
#include "flash_id_usage_Information.h"
#include "usage_information_table.h"

int
init();
int LIBUSB_CALL
hotplug_callback(struct libusb_context *ctx,
		 struct libusb_device *dev,
		 libusb_hotplug_event event,
		 void *user_data);
void *
usb_event_thread(void *arg);
struct FlashIdUsageInformation *
check();
int
FWUpdate(bool forceUpdate, bool noUnplug);
bool
CheckDockReadyForEnterPhase2Update();
int
GetCompositeVersion(char *out, size_t outSize);

int
main(int argc, char *argv[])
{
	mcheck(NULL);
	libusb_context *ctx = NULL;
	volatile int device_connected = 1; // 1表示设备连接，0表示设备已断开
	int r = init();
	// 注册热插拔回调
	libusb_hotplug_callback_handle callback_handle;
	int rc = libusb_hotplug_register_callback(ctx,
						  LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
						      LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
						  0,
						  DockVid,
						  DockPid,
						  LIBUSB_HOTPLUG_MATCH_ANY,
						  hotplug_callback,
						  NULL, // 用户数据传递
						  &callback_handle);
	if (rc != LIBUSB_SUCCESS) {
		fprintf(stderr, "Error registering hotplug callback: %s\n", libusb_strerror(rc));
		libusb_exit(ctx);
		return EXIT_FAILURE;
	}

	// 创建线程处理 libusb 的事件轮询
	pthread_t usb_thread;
	if (pthread_create(&usb_thread, NULL, usb_event_thread, NULL) != 0) {
		fprintf(stderr, "Failed to create USB event thread\n");
		libusb_hotplug_deregister_callback(ctx, callback_handle);
		libusb_exit(ctx);
		return EXIT_FAILURE;
	}

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "/c") == 0) {
			printf("Checking current FW version\n");
			struct FlashIdUsageInformation *Info = check();
			char version[64]; // 準備一個足夠大的 buffer

			int ret = GetCompositeVersion(version, sizeof(version));

			if (ret == 0) {
				printf("Composite Version = %s\n", version);
			}

			printf("DMC: ");
			printf("%d.%d.%02d\n",
			       Info[1].CurrentFwVersion[1],
			       Info[1].CurrentFwVersion[2],
			       Info[1].CurrentFwVersion[3]);
			printf("DP: ");
			printf("%d.%02d.%03d\n",
			       Info[2].CurrentFwVersion[1],
			       Info[2].CurrentFwVersion[2],
			       Info[2].CurrentFwVersion[3]);
			printf("PD: ");
			for (int i = 0; i < 3; i++)
				printf("%02X.", Info[3].CurrentFwVersion[i]);
			printf("%02X\n", Info[3].CurrentFwVersion[3]);
			printf("USB3: ");
			printf("%02X%02X\n",
			       Info[4].CurrentFwVersion[2],
			       Info[4].CurrentFwVersion[3]);
			printf("USB4: ");
			printf("%02X%02X\n",
			       Info[5].CurrentFwVersion[2],
			       Info[5].CurrentFwVersion[3]);

			return 0;
		} else if (strcmp(argv[i], "/u") == 0) {
			bool rs = CheckDockReadyForEnterPhase2Update();
			// printf("%s\n", rs ? "true" : "false");
			if (rs == true) {
				printf("Phase-1 update is already done, by rc %d",
				       FWU_PHASE1_LOCKED);
				return FWU_PHASE1_LOCKED;
			}

			printf("Please DO NOT remove the dock and wait for a few minutes until "
			       "the white light stops blinking.\n");
			printf("Start updating........\n");
			int r = FWUpdate(true, true);
			if (r == 0) {
				/*Notice : rc == 0 includes 2 condition of FW update result as below
				   .
					   1. FW update process is actaully be executed because
				   Dock's current composite is < target composite version.
					   2. FW update process doesn't wich actually erase & flash
				   FW image since the Dock's current composite version is >= target
				   composite version. *Assume fwupd pluggin will cover for the
				   condition with message output*/

				printf("Phase-1 finished. Please unplug the cable and wait for 30 "
				       "seconds.\nPhase-2 will start automatically with the orange "
				       "light starts blinking.\n");
				return 0;
			} else {
				printf("Phase-1 update failed, by error %d", r);
				return r;
			}
		}

		else {
			printf("Wrong command. Please use /c or /u.\n");
			return 0;
		}
	}

	pthread_join(usb_thread, NULL);
	// 注销回调并释放资源
	libusb_hotplug_deregister_callback(ctx, callback_handle);
	libusb_exit(ctx);

	return 0;
}