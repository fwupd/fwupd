#include "dock_command_declaration.h"
#include "flash_id_usage_Information.h"
#include "libusb.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

int Function1(uint8_t CmdClass, uint8_t CmdId, uint8_t FlashId, uint8_t *data,
              int dataSize, uint8_t *output);
uint8_t CurrentFwVerForGUI[8][4];
uint8_t *GetCommandBody1(uint8_t *data);

struct FlashIdUsageInformation *check() {
  uint8_t output[Interface1Length];
  for (int i = 0; i < Interface1Length; i++)
    output[i] = 0;
  int getFlashIdList =
      Function1(External_Flash, Get_Flash_ID_List, 0, 0, 0, output);
  // printf("getFlashIdList return %d\n",getFlashIdList);
  uint8_t totalFlashId = output[6];
  struct FlashIdUsageInformation *Info =
      (struct FlashIdUsageInformation *)calloc(
          totalFlashId, sizeof(struct FlashIdUsageInformation));

  for (int flashId = 1; flashId < totalFlashId; flashId++) {
    int getFlashIdAttribute =
        Function1(External_Flash, Get_Flash_Attribute, flashId, 0, 0, output);
    uint8_t getPurpose = output[7];
    if (getPurpose == FirmwareFile) {
      for (int i = 0; i < Interface1Length; i++)
        output[i] = 0;
      uint8_t getFlashIdUsageInformation =
          Function1(External_Flash, Get_Flash_ID_Usage_Information, flashId, 0,
                    0, output);
      for (int i = 0; i < 4; i++) {
        Info[flashId].PhysicalAddress[i] = output[6 + i];
        Info[flashId].MaxSize[i] = output[10 + i];
        Info[flashId].CurrentFwVersion[i] = output[14 + i];
        Info[flashId].TargetFwVersion[i] = output[18 + i];
        Info[flashId].TargetFwFileSize[i] = output[22 + i];
        Info[flashId].TargetFwFileCrc32[i] = output[26 + i];
        Info[flashId].ComponentID = output[30];
        CurrentFwVerForGUI[flashId][i] = Info[flashId].CurrentFwVersion[i];
      }
    }
  }
  return Info;
}

uint8_t GetCurrentFwVerForGUI(int flashId, int index) {
  return CurrentFwVerForGUI[flashId][index];
}

int GetCompositeVersion(char *out, size_t outSize) {
  if (!out || outSize == 0)
    return -1;

  uint8_t buffer[65] = {0};
  int rc = Function1(Device_Information, Get_Firmware_Version, 0, 0, 0, buffer);
  if (rc != 0) {
    snprintf(out, outSize, "0.0.0.0");
    return -1;
  }

  uint8_t *body = GetCommandBody1(buffer);
  if (!body) {
    snprintf(out, outSize, "0.0.0.0");
    return -1;
  }

  int n = snprintf(out, outSize, "%X.%X.%02X", body[0], body[1], body[2]);

  if (n < 0 || (size_t)n >= outSize) {
    return -1;
  }

  return 0;
}
