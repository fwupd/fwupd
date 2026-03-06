#ifndef flash_id_usage_information_h
#define flash_id_usage_information_h
#include <stdbool.h>
#include <stdint.h>

static const int FlashIdUsageLength = 32;
struct FlashIdUsageInformation {
  uint8_t PhysicalAddress[4];
  uint8_t MaxSize[4];
  uint8_t CurrentFwVersion[4];
  uint8_t TargetFwVersion[4];
  uint8_t TargetFwFileSize[4];
  uint8_t TargetFwFileCrc32[4];
  uint8_t ComponentID;
  bool Flag;
};

#endif