#include "dock_command_declaration.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

int BytesToInt(uint8_t *data, int length) {
  int size = 0;
  if (length == 2) {
    size = (data[1] << 8) + data[0];
  } else if (length == 4) {
    size = (data[3] << 24) + (data[2] << 16) + (data[1] << 8) + data[0];
  } else
    return AP_TOOL_FAILED;

  return size;
}
uint8_t *IntToBytes(int size) {
  static uint8_t res[4];
  res[3] = (uint8_t)((size >> 24) & 0xff);
  res[2] = (uint8_t)((size >> 16) & 0xff);
  res[1] = (uint8_t)((size >> 8) & 0xff);
  res[0] = (uint8_t)(size & 0xff);

  return res;
}
uint8_t *ToBytes(uint32_t size) {
  static uint8_t res[4];
  res[3] = (uint8_t)((size >> 24) & 0xff);
  res[2] = (uint8_t)((size >> 16) & 0xff);
  res[1] = (uint8_t)((size >> 8) & 0xff);
  res[0] = (uint8_t)(size & 0xff);

  return res;
}
uint32_t Compute(uint8_t *buffer, size_t bufferlength, int offset, int length) {
  if (!((buffer != NULL) && (offset >= 0) && (length >= 0) &&
        (offset <= bufferlength - length))) {
    return ARGUMENTS_SETTING_ERROR;
  }

  uint32_t crc32 = 0xffffffffU;

  while (--length >= 0) {
    crc32 = crcTable[(crc32 ^ buffer[offset++]) & 0xFF] ^ (crc32 >> 8);
  }
  crc32 ^= 0xffffffffU;
  return crc32;
}
bool arraysEqual(uint8_t *array1, uint8_t *array2, size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (array1[i] != array2[i])
      return false;
  }
  return true;
}
