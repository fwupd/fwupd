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

static const char *Path_Of_Usage_Information_Table = "FW/ldc_u4_usage_information_table.bin";

static const uint8_t UsageTableFlashID = 0xff;
static const int UsageTableSize = 4096;

typedef enum { Unsigned, RSA2048, RSA3072, ECC256, ECC384 } DSAType;

uint8_t CurrentFwVerForGUI[8][4];

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

static const uint16_t DockVid = 0x17ef;
static const uint16_t DockPid = 0x111e;

enum Target_Status {
	CommandDefault = 0x00,
	CommandBusy = 0x01,
	CommandSuccess = 0x02,
	CommandFaliure = 0x03,
	CommandTimeout = 0x04,
	CommandNotSupport = 0x05
};

enum ClassID {
	Unnecessary = 0x00,
	Device_Information = 0x00,
	DFU = 0x09,
	Test = 0x0A,
	External_Flash = 0x0C,
	Dock = 0x0E
};

/*External Flash ID*/
static const uint8_t Set_Flash_ID_Usage_Information = 0x03;
static const uint8_t Set_Flash_Memory_Access = 0x04;
static const uint8_t Get_Support_List = 0x80;
static const uint8_t Get_Flash_ID_List = 0x81;
static const uint8_t Get_Flash_Attribute = 0x82;
static const uint8_t Get_Flash_ID_Usage_Information = 0x83;
static const uint8_t Get_Flash_Memory_Access = 0x84;
static const uint8_t Get_Flash_Memory_Self_Verify = 0x85;
typedef enum { Common, ApplicationData, ImageData, FirmwareFile } ExternalFlashIdPurpose;

struct FlashIdAttribute {
	int FLashId;
	ExternalFlashIdPurpose Purpose;
	int StorageSize;
	int EraseSize;
	int ProgramSize;
};

struct FlashMemoryAccessCMD {
	enum OPCode {
		AcccessCtrl,
		Erase,
		Program,
		Read,
		Dock_Erase,
		Dock_Program,
		Dock_Read,
		Dock_Erase_With_Address,
		Dock_Program_With_Address,
		Dock_Read_With_Address

	};
	enum AccessCtrl { Release, Request };
};
struct FlashMemorySelfVerify {
	enum Type { Signature, CRC };

	enum VerifyPayload { Fail, Pass };
};

/*Dock Command ID*/
static const uint8_t Set_Dock_Port_Ctrl = 0x03;
static const uint8_t Set_Dock_Fan_Ctrl = 0x05;
static const uint8_t Set_Dock_IoT_Configure = 0x06;
static const uint8_t Set_Dock_USB_Container_ID = 0x07;
static const uint8_t Set_Dock_Lan_Mac_Address = 0x08;
static const uint8_t Set_Dock_Aux_Log = 0x09;
static const uint8_t Set_Dock_Firmware_Upgrade_Ctrl = 0x0A;
static const uint8_t Get_CMD_Support_List = 0x80;
static const uint8_t Get_Dock_Attribute = 0x81;
static const uint8_t Get_Dock_Port_Status = 0x82;
static const uint8_t Get_Dock_Port_Ctrl = 0x83;
static const uint8_t Get_Dock_Port_Connected_Device_Information = 0x84;
static const uint8_t Get_Dock_Dock_Fan_Ctrl = 0x85;
static const uint8_t Get_Dock_IoT_Configure = 0x86;
static const uint8_t Get_Dock_USB_Container_ID = 0x87;
static const uint8_t Get_Dock_Lan_Mac_Address = 0x88;
static const uint8_t Get_Dock_Aux_Log = 0x89;
static const uint8_t Get_Dock_Firmware_Upgrade_Ctrl = 0x8A;

static const int ReportIdOffset = 1;
static const int Target_Status_Defult = 0;
static const int Interface1Length = 64;
static const int Interface2Length = 272;

// FlashSize
static const int UsageInfo = 4096;
static const int SignatureSize = 256;
static const int EraseSizeByteLength = 2;
static const int ProgramSizeByteLength = 2;
static const int AddressByteLength = 4;
static const int StorageSizeByteLength = 4;
static const int DfuFilePayLoadLength = 5;

// Flash Address
static const int ImageStart = 0;
static const int UsageInfoStart = 16773120;

// Device Information ID
static const uint8_t Set_Hardware_Version = 0x02;
static const uint8_t Set_Serial_Number = 0x03;
static const uint8_t Set_Device_Mode = 0x04;
static const uint8_t Set_Device_Edition = 0x06;
static const uint8_t Set_Device_Name = 0x08;
static const uint8_t Set_Device_Reset = 0x09;
static const uint8_t Set_Device_UUID = 0x14;
static const uint8_t Get_Command_Support_List = 0x80;
static const uint8_t Get_Firmware_Version = 0x81;
static const uint8_t Get_Hardware_Version = 0x82;
static const uint8_t Get_Serial_Number = 0x83;
static const uint8_t Get_Device_Mode = 0x84;
static const uint8_t Get_Device_Edition = 0x86;
static const uint8_t Get_Device_Name = 0x88;
static const uint8_t Get_Device_UUID = 0x9;

libusb_context *ctx = NULL;
volatile int device_connected = 1; // 1表示设备连接，0表示设备已断开
libusb_device_handle *devh = NULL;

struct DockFwCtrl {
	enum FwUpgradeLocked { nonLock, Locked };
	enum FwUpgradePhaseCtrl {
		NA,
		InPhase1,
		unplug,
		nonUnplug,
		waitForTimer,
	};
};

/*error code*/
static const int SUCCESS = 0;
static const int PARAM_ERROR = 1300;
static const int REPORT_DATA_FAILED = 1027;
static const int COMMAND_FALIURE = 1281;
static const int COMMAND_TIMEOUT = 1282;
static const int AP_TOOL_FAILED = 1;
static const int COMMAND_OVER_RETRY_TIMES = 1283;
static const int USAGE_INFORMATION_NOT_FOUND = 258;
static const int USAGE_INFORMATION_FILE_ERROR = 259;
static const int ARGUMENTS_SETTING_ERROR = 0x300;
static const int USAGE_INFORMATION_CRC_FAILED = 266;
static const int USAGE_INFORMATION_PAGE_FAILED = 1030;
static const int XML_FILE_FORMAT_ERROR = 265;
static const int COMPOSITE_IMAGE_FILE_NOT_FOUND = 260;
static const int COMPOSITE_IMAGE_FILE_ERROR = 261;
static const int UPDATE_USAGE_INFORMATION_PAGE_FAILED = 1030;
static const int UPDATE_DOCK_FAILED = 0x400;

static const int waitMcuBusyTime = 25000000; // 0.025sec
static const int maxRetry = 1600;

struct timespec req = {0, waitMcuBusyTime};

/*Phase-1 Firmware Update Status RC*/
typedef enum {
	FWU_UPDATE_SUCCESS = 0,
	FWU_NO_UPDATE_NEEDED,
	FWU_FAILED_OR_DOCK_NOT_FOUND = -1,
	FWU_PHASE1_LOCKED = 2,
	FWU_OTA_DEPLOYEEING = 3
} FwUpdatgeResult;

static const uint32_t crcTable[256] = {
    0x00000000u, 0x77073096u, 0xee0e612cu, 0x990951bau, 0x076dc419u, 0x706af48fu, 0xe963a535u,
    0x9e6495a3u, 0x0edb8832u, 0x79dcb8a4u, 0xe0d5e91eu, 0x97d2d988u, 0x09b64c2bu, 0x7eb17cbdu,
    0xe7b82d07u, 0x90bf1d91u, 0x1db71064u, 0x6ab020f2u, 0xf3b97148u, 0x84be41deu, 0x1adad47du,
    0x6ddde4ebu, 0xf4d4b551u, 0x83d385c7u, 0x136c9856u, 0x646ba8c0u, 0xfd62f97au, 0x8a65c9ecu,
    0x14015c4fu, 0x63066cd9u, 0xfa0f3d63u, 0x8d080df5u, 0x3b6e20c8u, 0x4c69105eu, 0xd56041e4u,
    0xa2677172u, 0x3c03e4d1u, 0x4b04d447u, 0xd20d85fdu, 0xa50ab56bu, 0x35b5a8fau, 0x42b2986cu,
    0xdbbbc9d6u, 0xacbcf940u, 0x32d86ce3u, 0x45df5c75u, 0xdcd60dcfu, 0xabd13d59u, 0x26d930acu,
    0x51de003au, 0xc8d75180u, 0xbfd06116u, 0x21b4f4b5u, 0x56b3c423u, 0xcfba9599u, 0xb8bda50fu,
    0x2802b89eu, 0x5f058808u, 0xc60cd9b2u, 0xb10be924u, 0x2f6f7c87u, 0x58684c11u, 0xc1611dabu,
    0xb6662d3du, 0x76dc4190u, 0x01db7106u, 0x98d220bcu, 0xefd5102au, 0x71b18589u, 0x06b6b51fu,
    0x9fbfe4a5u, 0xe8b8d433u, 0x7807c9a2u, 0x0f00f934u, 0x9609a88eu, 0xe10e9818u, 0x7f6a0dbbu,
    0x086d3d2du, 0x91646c97u, 0xe6635c01u, 0x6b6b51f4u, 0x1c6c6162u, 0x856530d8u, 0xf262004eu,
    0x6c0695edu, 0x1b01a57bu, 0x8208f4c1u, 0xf50fc457u, 0x65b0d9c6u, 0x12b7e950u, 0x8bbeb8eau,
    0xfcb9887cu, 0x62dd1ddfu, 0x15da2d49u, 0x8cd37cf3u, 0xfbd44c65u, 0x4db26158u, 0x3ab551ceu,
    0xa3bc0074u, 0xd4bb30e2u, 0x4adfa541u, 0x3dd895d7u, 0xa4d1c46du, 0xd3d6f4fbu, 0x4369e96au,
    0x346ed9fcu, 0xad678846u, 0xda60b8d0u, 0x44042d73u, 0x33031de5u, 0xaa0a4c5fu, 0xdd0d7cc9u,
    0x5005713cu, 0x270241aau, 0xbe0b1010u, 0xc90c2086u, 0x5768b525u, 0x206f85b3u, 0xb966d409u,
    0xce61e49fu, 0x5edef90eu, 0x29d9c998u, 0xb0d09822u, 0xc7d7a8b4u, 0x59b33d17u, 0x2eb40d81u,
    0xb7bd5c3bu, 0xc0ba6cadu, 0xedb88320u, 0x9abfb3b6u, 0x03b6e20cu, 0x74b1d29au, 0xead54739u,
    0x9dd277afu, 0x04db2615u, 0x73dc1683u, 0xe3630b12u, 0x94643b84u, 0x0d6d6a3eu, 0x7a6a5aa8u,
    0xe40ecf0bu, 0x9309ff9du, 0x0a00ae27u, 0x7d079eb1u, 0xf00f9344u, 0x8708a3d2u, 0x1e01f268u,
    0x6906c2feu, 0xf762575du, 0x806567cbu, 0x196c3671u, 0x6e6b06e7u, 0xfed41b76u, 0x89d32be0u,
    0x10da7a5au, 0x67dd4accu, 0xf9b9df6fu, 0x8ebeeff9u, 0x17b7be43u, 0x60b08ed5u, 0xd6d6a3e8u,
    0xa1d1937eu, 0x38d8c2c4u, 0x4fdff252u, 0xd1bb67f1u, 0xa6bc5767u, 0x3fb506ddu, 0x48b2364bu,
    0xd80d2bdau, 0xaf0a1b4cu, 0x36034af6u, 0x41047a60u, 0xdf60efc3u, 0xa867df55u, 0x316e8eefu,
    0x4669be79u, 0xcb61b38cu, 0xbc66831au, 0x256fd2a0u, 0x5268e236u, 0xcc0c7795u, 0xbb0b4703u,
    0x220216b9u, 0x5505262fu, 0xc5ba3bbeu, 0xb2bd0b28u, 0x2bb45a92u, 0x5cb36a04u, 0xc2d7ffa7u,
    0xb5d0cf31u, 0x2cd99e8bu, 0x5bdeae1du, 0x9b64c2b0u, 0xec63f226u, 0x756aa39cu, 0x026d930au,
    0x9c0906a9u, 0xeb0e363fu, 0x72076785u, 0x05005713u, 0x95bf4a82u, 0xe2b87a14u, 0x7bb12baeu,
    0x0cb61b38u, 0x92d28e9bu, 0xe5d5be0du, 0x7cdcefb7u, 0x0bdbdf21u, 0x86d3d2d4u, 0xf1d4e242u,
    0x68ddb3f8u, 0x1fda836eu, 0x81be16cdu, 0xf6b9265bu, 0x6fb077e1u, 0x18b74777u, 0x88085ae6u,
    0xff0f6a70u, 0x66063bcau, 0x11010b5cu, 0x8f659effu, 0xf862ae69u, 0x616bffd3u, 0x166ccf45u,
    0xa00ae278u, 0xd70dd2eeu, 0x4e048354u, 0x3903b3c2u, 0xa7672661u, 0xd06016f7u, 0x4969474du,
    0x3e6e77dbu, 0xaed16a4au, 0xd9d65adcu, 0x40df0b66u, 0x37d83bf0u, 0xa9bcae53u, 0xdebb9ec5u,
    0x47b2cf7fu, 0x30b5ffe9u, 0xbdbdf21cu, 0xcabac28au, 0x53b39330u, 0x24b4a3a6u, 0xbad03605u,
    0xcdd70693u, 0x54de5729u, 0x23d967bfu, 0xb3667a2eu, 0xc4614ab8u, 0x5d681b02u, 0x2a6f2b94u,
    0xb40bbe37u, 0xc30c8ea1u, 0x5a05df1bu, 0x2d02ef8du};

struct UsageInformation {
	uint8_t Totalnumber;
	uint8_t MajorVersion;
	uint8_t MinorVersion;
	DSAType Dsa;
	uint8_t IoTUpdateFlag;
	uint8_t CompositeFwVersion[4];
	uint8_t DockPid[2];
	uint8_t Crc32[4];
	struct FlashIdUsageInformation *FlashIdList;
};

int
BytesToInt(uint8_t *data, int length)
{
	int size = 0;
	if (length == 2) {
		size = (data[1] << 8) + data[0];
	} else if (length == 4) {
		size = (data[3] << 24) + (data[2] << 16) + (data[1] << 8) + data[0];
	} else
		return AP_TOOL_FAILED;

	return size;
}
uint8_t *
IntToBytes(int size)
{
	static uint8_t res[4];
	res[3] = (uint8_t)((size >> 24) & 0xff);
	res[2] = (uint8_t)((size >> 16) & 0xff);
	res[1] = (uint8_t)((size >> 8) & 0xff);
	res[0] = (uint8_t)(size & 0xff);

	return res;
}
uint8_t *
ToBytes(uint32_t size)
{
	static uint8_t res[4];
	res[3] = (uint8_t)((size >> 24) & 0xff);
	res[2] = (uint8_t)((size >> 16) & 0xff);
	res[1] = (uint8_t)((size >> 8) & 0xff);
	res[0] = (uint8_t)(size & 0xff);

	return res;
}
uint32_t
Compute(uint8_t *buffer, size_t bufferlength, int offset, int length)
{
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
bool
arraysEqual(uint8_t *array1, uint8_t *array2, size_t length)
{
	for (size_t i = 0; i < length; i++) {
		if (array1[i] != array2[i])
			return false;
	}
	return true;
}

static const char *Path_Of_Composite_Image = "FW/ldc_u4_composite_image.bin";

int
ReadCompositeImageFile(char **data)
{
	struct stat fileInfo;
	FILE *fp;
	if (stat(Path_Of_Composite_Image, &fileInfo) == 0) {
		if (fileInfo.st_size > 16773120) {
			return COMPOSITE_IMAGE_FILE_ERROR;
		} else {
			/*read the file actions*/
			fp = fopen(Path_Of_Composite_Image, "rb");
			char *buffer = (char *)calloc(16773120, sizeof(char));
			size_t bytesRead = fread(buffer, sizeof(char), 16773120, fp);
			*data = buffer;
			fclose(fp);
			return SUCCESS;
		}
	}

	else
		return COMPOSITE_IMAGE_FILE_NOT_FOUND;
}

uint8_t *
GetBytes(struct UsageInformation targetUsageInformationTable)
{
	uint8_t *table = (uint8_t *)calloc(UsageTableSize, sizeof(uint8_t));
	// Header
	table[0] = targetUsageInformationTable.Totalnumber;
	table[1] = (targetUsageInformationTable.MajorVersion << 4) +
		   (targetUsageInformationTable.MinorVersion & 0x0f);
	table[2] = (uint8_t)targetUsageInformationTable.Dsa;
	table[3] = targetUsageInformationTable.IoTUpdateFlag;
	for (int i = 0; i < 4; i++)
		table[4 + i] = targetUsageInformationTable.CompositeFwVersion[i];
	for (int i = 0; i < 2; i++)
		table[8 + i] = targetUsageInformationTable.DockPid[i];
	// Body
	for (int i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
		int flashId = i;
		uint8_t flashIdData[FlashIdUsageLength];
		for (int j = 0; j < FlashIdUsageLength; j++)
			flashIdData[j] = 0;
		for (int j = 0; j < 4; j++) {
			flashIdData[0 + j] =
			    targetUsageInformationTable.FlashIdList[i].PhysicalAddress[j];
			flashIdData[4 + j] = targetUsageInformationTable.FlashIdList[i].MaxSize[j];
			flashIdData[8 + j] =
			    targetUsageInformationTable.FlashIdList[i].CurrentFwVersion[j];
			flashIdData[12 + j] =
			    targetUsageInformationTable.FlashIdList[i].TargetFwVersion[j];
			flashIdData[16 + j] =
			    targetUsageInformationTable.FlashIdList[i].TargetFwFileSize[j];
			flashIdData[20 + j] =
			    targetUsageInformationTable.FlashIdList[i].TargetFwFileCrc32[j];
			flashIdData[24] = targetUsageInformationTable.FlashIdList[i].ComponentID;
		}
		for (int j = 0; j < FlashIdUsageLength; j++) {
			int addr = (flashId * 32) + j;
			table[addr] = flashIdData[j];
			// printf("%02X ",table[addr]);
		}
	}
	// Crc32
	uint32_t tableCrc = Compute(table, UsageTableSize, 0, 4092);
	uint8_t *computecrc = ToBytes(tableCrc);
	for (int i = 0; i < 4; i++) {
		table[4092 + i] = computecrc[i];
		// printf("CRC: %02X\n",computecrc[i]);
	}

	return table;
}

#if 0
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
#endif

uint8_t *
GetCompositeData(int addr, int size, char *data)
{
	uint8_t *temp = (uint8_t *)calloc(size, sizeof(uint8_t));
	for (int i = 0; i < size; i++) {
		temp[i] = (uint8_t)data[addr + i];
	}
	return temp;
}

bool
CheckFwVerify(int flashId,
	      struct UsageInformation targetUsageInformationTable,
	      char *compositeImageData)
{
	int maxSize = BytesToInt(targetUsageInformationTable.FlashIdList[flashId].MaxSize, 4);
	int targetFwSize =
	    BytesToInt(targetUsageInformationTable.FlashIdList[flashId].TargetFwFileSize, 4);
	if (targetFwSize + SignatureSize > maxSize)
		return false;

	int startAddr =
	    BytesToInt(targetUsageInformationTable.FlashIdList[flashId].PhysicalAddress, 4) +
	    SignatureSize;
	uint8_t *flashIdImageData = GetCompositeData(startAddr, targetFwSize, compositeImageData);
	int a = BytesToInt(targetUsageInformationTable.FlashIdList[flashId].TargetFwFileCrc32, 4);
	uint32_t b = Compute(flashIdImageData, targetFwSize, 0, targetFwSize);
	free(flashIdImageData);
	if ((uint32_t)a != b)
		return false;

	return true;
}

struct FlashIdAttribute
GetFlashIdAttribute(uint8_t *data)
{
	struct FlashIdAttribute fa;
	fa.FLashId = data[0];
	fa.Purpose = (ExternalFlashIdPurpose)data[1];

	int count = 2;
	uint8_t storageBuf[StorageSizeByteLength];
	for (int i = 0; i < StorageSizeByteLength; i++)
		storageBuf[i] = data[count++];
	fa.StorageSize = BytesToInt(storageBuf, StorageSizeByteLength);

	uint8_t eraseSizeBuf[EraseSizeByteLength];
	for (int i = 0; i < EraseSizeByteLength; i++)
		eraseSizeBuf[i] = data[count++];
	fa.EraseSize = BytesToInt(eraseSizeBuf, EraseSizeByteLength);

	uint8_t programSizeBuf[ProgramSizeByteLength];
	for (int i = 0; i < ProgramSizeByteLength; i++)
		programSizeBuf[i] = data[count++];
	fa.ProgramSize = BytesToInt(programSizeBuf, ProgramSizeByteLength);

	return fa;
}

void
SetFeature(uint8_t *cmd, int interface)
{
	int res;
	switch (interface) {
	case 1:
		res = libusb_control_transfer(devh,
					      0b00100001,
					      0x09,
					      0x0300 + cmd[0],
					      interface,
					      cmd,
					      Interface1Length,
					      0);
		// printf("set: %d\n",res);
		break;
	case 2:
		res = libusb_control_transfer(devh,
					      0b00100001,
					      0x09,
					      0x0300 + cmd[0],
					      interface,
					      cmd,
					      Interface2Length,
					      0);
		// printf("set: %d\n",res);
		break;
	default:
		break;
	}

	// if(res < 0)
	//     printf("SetFeature failed : %d\n", res);
}

void
GetFeature(uint8_t *cmd, int interface)
{
	int res;
	switch (interface) {
	case 1:
		res = libusb_control_transfer(devh,
					      0b10100001,
					      0x01,
					      0x0300 + cmd[0],
					      interface,
					      cmd,
					      Interface1Length,
					      0);
		// printf("get: %d\n",res);
		break;
	case 2:
		res = libusb_control_transfer(devh,
					      0b10100001,
					      0x01,
					      0x0300 + cmd[0],
					      interface,
					      cmd,
					      Interface2Length,
					      0);
		// printf("get: %d\n",res);
		break;
	default:
		break;
	}

	// if(res < 0)
	//     printf("GetFeature failed : %d\n", res);
}

int
Function1(uint8_t CmdClass,
	  uint8_t CmdId,
	  uint8_t FlashId,
	  uint8_t *data,
	  int dataSize,
	  uint8_t *output)
{
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
	cmd[5] = 0x00; // reserved

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

void
TriggerPhase2(bool noUnplug)
{
	uint8_t DfuCtrl[2] = {0};
	uint8_t output1[Interface1Length];
	for (int i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	if (noUnplug) {
		DfuCtrl[0] = Locked;
		DfuCtrl[1] = nonUnplug;
		int r = Function1(Dock, Set_Dock_Firmware_Upgrade_Ctrl, 0, DfuCtrl, 2, output1);
	} else {
		DfuCtrl[0] = Locked;
		DfuCtrl[1] = unplug;
		int r = Function1(Dock, Set_Dock_Firmware_Upgrade_Ctrl, 0, DfuCtrl, 2, output1);
	}
}

uint8_t *
GetCommandBody1(uint8_t *data)
{
	int size = data[1];
	uint8_t *res = (uint8_t *)calloc(size, sizeof(uint8_t));
	for (int i = 0; i < size; i++)
		res[i] = data[6 + i];

	return res;
}
uint8_t *
GetCommandBody2(uint8_t *data)
{
	int size = data[2];
	uint8_t *res = (uint8_t *)calloc(size, sizeof(uint8_t));
	for (int i = 0; i < size; i++)
		res[i] = data[7 + i];

	return res;
}

int
Function2(uint8_t CmdClass,
	  uint8_t CmdId,
	  uint8_t FlashId,
	  uint8_t *data,
	  int dataSize,
	  uint8_t *output)
{
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
	cmd[6] = 0x00; // reserved

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

int
WriteUsageInformationTable(uint8_t *usageInformationData)
{
	int errorHandle = 0;
	// Get Usage Information Attribute
	uint8_t output1[Interface1Length];
	uint8_t output2[Interface2Length];
	for (int i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	int usageInformationAttributeData =
	    Function1(External_Flash, Get_Flash_Attribute, UsageTableFlashID, 0, 0, output1);
	if (usageInformationAttributeData != 0)
		return usageInformationAttributeData;
	uint8_t *usageInformationAttributeBody = GetCommandBody1(output1);
	struct FlashIdAttribute usageInformationAttribute =
	    GetFlashIdAttribute(usageInformationAttributeBody);
	free(usageInformationAttributeBody);

	// Set Usage Information Memory Access (Erase)
	for (int readBytes = 0; readBytes < UsageTableSize;
	     readBytes += usageInformationAttribute.EraseSize) {
		int address = UsageInfoStart + readBytes;
		uint8_t *eraseUsageInformationAddress = IntToBytes(address);
		uint8_t setUsageInformationMemoryErase[2 + AddressByteLength + EraseSizeByteLength];
		for (int i = 0; i < 2 + AddressByteLength + EraseSizeByteLength; i++)
			setUsageInformationMemoryErase[i] = 0;

		setUsageInformationMemoryErase[0] = Dock_Erase_With_Address;
		setUsageInformationMemoryErase[1] = 0x00;
		for (int i = 0; i < AddressByteLength; i++)
			setUsageInformationMemoryErase[2 + EraseSizeByteLength + i] =
			    eraseUsageInformationAddress[i];
		for (int i = 0; i < EraseSizeByteLength; i++)
			setUsageInformationMemoryErase[2 + i] =
			    IntToBytes(usageInformationAttribute.EraseSize)[i];
		for (int i = 0; i < Interface2Length; i++)
			output2[i] = 0;
		errorHandle = Function2(External_Flash,
					Set_Flash_Memory_Access,
					UsageTableFlashID,
					setUsageInformationMemoryErase,
					2 + AddressByteLength + EraseSizeByteLength,
					output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	// Set Usage Information Memory Access (Program)
	for (int readBytes = 0; readBytes < UsageTableSize;
	     readBytes += usageInformationAttribute.ProgramSize) {
		int address = UsageInfoStart + readBytes;
		uint8_t *addrBytes = IntToBytes(address);

		int payloadLength = 2 + AddressByteLength + ProgramSizeByteLength;
		uint8_t write[usageInformationAttribute.ProgramSize + payloadLength];
		for (int i = 0; i < usageInformationAttribute.ProgramSize + payloadLength; i++)
			write[i] = 0;
		write[0] = Dock_Program_With_Address;
		write[1] = 0x00;
		for (int i = 0; i < AddressByteLength; i++)
			write[2 + ProgramSizeByteLength + i] = addrBytes[i];
		for (int i = 0; i < ProgramSizeByteLength; i++)
			write[2 + i] = IntToBytes(usageInformationAttribute.ProgramSize)[i];
		for (int i = 0; i < usageInformationAttribute.ProgramSize; i++)
			write[payloadLength + i] = usageInformationData[readBytes + i];

		for (int i = 0; i < Interface2Length; i++)
			output2[i] = 0;
		errorHandle = Function2(External_Flash,
					Set_Flash_Memory_Access,
					UsageTableFlashID,
					write,
					(usageInformationAttribute.ProgramSize + payloadLength),
					output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	// Get Usage Information Page Self-Verify
	uint8_t writeSelfVerify[1];
	writeSelfVerify[0] = CRC;
	for (int i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	int pageSelfVerifyData = Function1(External_Flash,
					   Get_Flash_Memory_Self_Verify,
					   UsageTableFlashID,
					   writeSelfVerify,
					   1,
					   output1);
	if (pageSelfVerifyData != 0)
		return pageSelfVerifyData;
	uint8_t *pageSelfVerifyBody = GetCommandBody1(output1);
	uint8_t pageSelfVerifyCrc[4];
	for (int i = 0; i < 4; i++) {
		pageSelfVerifyCrc[i] = pageSelfVerifyBody[1 + i];
		// printf("%02X\n",pageSelfVerifyCrc[i]);
	}

	// Usge Information CRC
	uint8_t uiCrc[4];
	for (int i = 0; i < 4; i++) {
		uiCrc[i] = usageInformationData[4092 + i];
		// printf("%02X\n",uiCrc[i]);
	}
	if (!arraysEqual(uiCrc, pageSelfVerifyCrc, 4))
		return USAGE_INFORMATION_PAGE_FAILED;

	return 0;
}

int
WriteFlashIdData(int flashId,
		 struct FlashIdAttribute flashIdAttribute,
		 struct UsageInformation changeTagetUsageInformationTable,
		 char *compositeImageData)
{
	int errorHandle = 0;
	uint8_t output1[Interface1Length];
	uint8_t output2[Interface2Length];
	// Check FW Data In Host Self-Verify
	if (!CheckFwVerify(flashId, changeTagetUsageInformationTable, compositeImageData))
		return UPDATE_USAGE_INFORMATION_PAGE_FAILED;

	// Set Flash Memory Access (Erase)
	uint8_t *eraseStartAddrBytes =
	    changeTagetUsageInformationTable.FlashIdList[flashId].PhysicalAddress;
	int targetMaxSize =
	    BytesToInt(changeTagetUsageInformationTable.FlashIdList[flashId].MaxSize, 4);
	int targetFwSize =
	    BytesToInt(changeTagetUsageInformationTable.FlashIdList[flashId].TargetFwFileSize, 4);
	for (int eraseBytes = 0; eraseBytes < targetMaxSize;
	     eraseBytes += flashIdAttribute.EraseSize) {
		uint8_t setFlashMemoryErase[2 + EraseSizeByteLength + AddressByteLength];
		for (int i = 0; i < 2 + EraseSizeByteLength + AddressByteLength; i++)
			setFlashMemoryErase[i] = 0;
		setFlashMemoryErase[0] = Dock_Erase_With_Address;
		setFlashMemoryErase[1] = 0x00;
		int eraseAddr = BytesToInt(eraseStartAddrBytes, 4) + eraseBytes;
		for (int i = 0; i < EraseSizeByteLength; i++)
			setFlashMemoryErase[2 + i] = IntToBytes(flashIdAttribute.EraseSize)[i];
		for (int i = 0; i < AddressByteLength; i++)
			setFlashMemoryErase[2 + EraseSizeByteLength + i] = IntToBytes(eraseAddr)[i];

		for (int i = 0; i < Interface2Length; i++)
			output2[i] = 0;
		errorHandle = Function2(External_Flash,
					Set_Flash_Memory_Access,
					flashId,
					setFlashMemoryErase,
					2 + EraseSizeByteLength + AddressByteLength,
					output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	// Set Flash Memory Access (program)
	int flashIdStartAddr =
	    BytesToInt(changeTagetUsageInformationTable.FlashIdList[flashId].PhysicalAddress, 4);
	int flashIdDataSize =
	    BytesToInt(changeTagetUsageInformationTable.FlashIdList[flashId].MaxSize, 4);
	uint8_t *flashIdCompositeData =
	    GetCompositeData(flashIdStartAddr, flashIdDataSize, compositeImageData);
	for (int readBytes = 0; readBytes < targetFwSize + SignatureSize;
	     readBytes += flashIdAttribute.ProgramSize) {
		int address = flashIdStartAddr + readBytes;
		uint8_t *addrBytes = IntToBytes(address);

		int payloadLength = 2 + AddressByteLength + ProgramSizeByteLength;
		uint8_t write[flashIdAttribute.ProgramSize + payloadLength];
		write[0] = Dock_Program_With_Address;
		write[1] = 0x00;
		for (int i = 0; i < AddressByteLength; i++)
			write[2 + ProgramSizeByteLength + i] = addrBytes[i];
		for (int i = 0; i < ProgramSizeByteLength; i++)
			write[2 + i] = IntToBytes(flashIdAttribute.ProgramSize)[i];
		for (int i = 0; i < flashIdAttribute.ProgramSize; i++)
			write[payloadLength + i] = flashIdCompositeData[readBytes + i];

		for (int i = 0; i < Interface2Length; i++)
			output2[i] = 0;
		errorHandle = Function2(External_Flash,
					Set_Flash_Memory_Access,
					flashId,
					write,
					flashIdAttribute.ProgramSize + payloadLength,
					output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	free(flashIdCompositeData);

	// Get Flash Usage Information Memory Self-Verify
	uint8_t writeFlashUpdateCheckSignature[1];
	writeFlashUpdateCheckSignature[0] = Signature;
	for (int i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	int flashUpdateCheckSignatureData = Function1(External_Flash,
						      Get_Flash_Memory_Self_Verify,
						      flashId,
						      writeFlashUpdateCheckSignature,
						      1,
						      output1);
	if (flashUpdateCheckSignatureData != 0)
		return flashUpdateCheckSignatureData;
	uint8_t *flashUpdateCheckSignatureBody = GetCommandBody1(output1);
	if (flashUpdateCheckSignatureBody[1] != (uint8_t)Pass)
		return UPDATE_DOCK_FAILED;

	uint8_t writeFlashUpdateCheckVerify[1];
	writeFlashUpdateCheckVerify[0] = CRC;
	for (int i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	int flashUpdateCheckVerifyData = Function1(External_Flash,
						   Get_Flash_Memory_Self_Verify,
						   flashId,
						   writeFlashUpdateCheckVerify,
						   1,
						   output1);
	if (flashUpdateCheckVerifyData != 0)
		return flashUpdateCheckVerifyData;
	uint8_t *flashUpdateCheckVerifyBody = GetCommandBody1(output1);
	uint8_t flashUpdateCheckVerifyCrc[4];
	for (int i = 0; i < 4; i++) {
		flashUpdateCheckVerifyCrc[i] = flashUpdateCheckVerifyBody[1 + i];
		// printf("%02X\n",flashUpdateCheckVerifyCrc[i]);
	}
	if (!arraysEqual(changeTagetUsageInformationTable.FlashIdList[flashId].TargetFwFileCrc32,
			 flashUpdateCheckVerifyCrc,
			 4))
		return UPDATE_DOCK_FAILED;

	return 0;
}

/**
 * @brief Execute firmware update procedure for the dock device.
 *
 * This function performs the full firmware update flow including:
 * - Load composite firmware image and Usage Information Table from file
 * - Request flash memory access from the device
 * - Parse and verify Usage Information Table
 * - Retrieve Flash ID list from the device
 * - Perform firmware programming for each component
 *
 * @param forceUpdate
 *        If true, firmware will be updated even if the current firmware version
 * is => the target version.
 *
 * @param noUnplug
 *        If true, skip unplug detection during the firmware update process.
 *
 * @return SUCCESS on success, otherwise returns an error code defined in the
 * firmware update error definitions.(int)
 */

int
FWUpdate(bool forceUpdate, bool noUnplug)
{
	// bool forceUpdate = false;
	// bool noUnplug = false;
	char *compositeImageData;
	int r;
	r = ReadCompositeImageFile(&compositeImageData);
	if (r != 0) {
		// printf("Read composite image error: %d\n",r);
		return r;
	}
	// Set Flash Memory Access (Request)
	uint8_t setFlashMemoryRequest[2];
	uint8_t output1[Interface1Length];
	uint8_t output2[Interface2Length];
	setFlashMemoryRequest[0] = AcccessCtrl;
	setFlashMemoryRequest[1] = Request;
	for (int i = 0; i < Interface2Length; i++)
		output2[i] = 0;
	r = Function2(External_Flash,
		      Set_Flash_Memory_Access,
		      0,
		      setFlashMemoryRequest,
		      2,
		      output2);
	if (r != 0) {
		printf("Set Flash Memory Access (Request) error: %d\n", r);
		return FWU_OTA_DEPLOYEEING;
	}
	// Read Target Usage Information Table
	struct stat fileInfo;
	struct UsageInformation targetUsageInformationTable;
	uint8_t ds[FlashIdUsageLength];
	FILE *fp;
	if (stat(Path_Of_Usage_Information_Table, &fileInfo) == 0) {
		if (fileInfo.st_size != UsageTableSize) {
			return USAGE_INFORMATION_FILE_ERROR;
		} else {
			/*read the file actions*/
			fp = fopen(Path_Of_Usage_Information_Table, "rb");
			char buffer[UsageTableSize];
			size_t bytesRead = fread(&buffer, sizeof(char), UsageTableSize, fp);
			targetUsageInformationTable.Totalnumber = (uint8_t)buffer[0];
			targetUsageInformationTable.MajorVersion =
			    (((uint8_t)buffer[1] >> 4) & 0x0f);
			targetUsageInformationTable.MinorVersion = ((uint8_t)buffer[1] & 0x0f);
			targetUsageInformationTable.Dsa = (DSAType)buffer[2];
			targetUsageInformationTable.IoTUpdateFlag = (uint8_t)buffer[3];
			for (int i = 0; i < 4; i++) {
				targetUsageInformationTable.CompositeFwVersion[i] =
				    (uint8_t)buffer[4 + i];
				targetUsageInformationTable.Crc32[i] =
				    (uint8_t)buffer[UsageTableSize - 4 + i];
			}
			for (int i = 0; i < 2; i++) {
				targetUsageInformationTable.DockPid[i] = (uint8_t)buffer[8 + i];
			}
			targetUsageInformationTable.FlashIdList =
			    (struct FlashIdUsageInformation *)calloc(
				targetUsageInformationTable.Totalnumber + 1,
				sizeof(struct FlashIdUsageInformation));
			for (int i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
				for (int j = 0; j < FlashIdUsageLength; j++) {
					ds[j] = (uint8_t)buffer[i * 32 + j];
				}
				for (int j = 0; j < 4; j++) {
					targetUsageInformationTable.FlashIdList[i]
					    .PhysicalAddress[j] = ds[j];
					targetUsageInformationTable.FlashIdList[i].MaxSize[j] =
					    ds[4 + j];
					targetUsageInformationTable.FlashIdList[i]
					    .CurrentFwVersion[j] = ds[8 + j];
					targetUsageInformationTable.FlashIdList[i]
					    .TargetFwVersion[j] = ds[12 + j];
					targetUsageInformationTable.FlashIdList[i]
					    .TargetFwFileSize[j] = ds[16 + j];
					targetUsageInformationTable.FlashIdList[i]
					    .TargetFwFileCrc32[j] = ds[20 + j];
					targetUsageInformationTable.FlashIdList[i].ComponentID =
					    ds[24];

					//
					targetUsageInformationTable.FlashIdList[i].Flag = false;
				}
			}
			if (BytesToInt(targetUsageInformationTable.Crc32, 4) !=
			    Compute(buffer,
				    sizeof(buffer) / sizeof(buffer[0]),
				    0,
				    UsageTableSize - 4))
				return USAGE_INFORMATION_CRC_FAILED;

			uint16_t targetPid =
			    (uint16_t)((targetUsageInformationTable.DockPid[0] << 8) +
				       targetUsageInformationTable.DockPid[1]);
			if (DockPid != targetPid)
				return XML_FILE_FORMAT_ERROR;
		}
	}
	// Check Flash Id List Count
	for (int i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	int getFlashIdListData = Function1(External_Flash, Get_Flash_ID_List, 0, 0, 0, output1);
	uint8_t flashIdListTotal = output1[6];
	uint8_t flashIdList[flashIdListTotal - 1];
	for (int i = 0; i < flashIdListTotal - 1; i++) {
		flashIdList[i] = output1[7 + i];
	}
	if ((targetUsageInformationTable.Totalnumber + 1) != flashIdListTotal)
		return USAGE_INFORMATION_CRC_FAILED;

	// Read MCU Usage Information Memory Access (Read)
	const int readCountByCycle = 256;
	int mcuUsageInformationCount = 0;
	uint8_t mcuUsageInformationData[UsageInfo];
	do {
		int payload = 8;
		uint8_t setmcuUsageInformationTable[payload];
		setmcuUsageInformationTable[0] = Dock_Read_With_Address;
		setmcuUsageInformationTable[1] = 0x00;

		uint8_t *size = IntToBytes(readCountByCycle);
		setmcuUsageInformationTable[2] = size[0];
		setmcuUsageInformationTable[3] = size[1];

		uint8_t *addr = IntToBytes(UsageInfoStart + mcuUsageInformationCount);
		setmcuUsageInformationTable[4] = addr[0];
		setmcuUsageInformationTable[5] = addr[1];
		setmcuUsageInformationTable[6] = addr[2];
		setmcuUsageInformationTable[7] = addr[3];

		for (int i = 0; i < Interface2Length; i++)
			output2[i] = 0;
		int tempBytes = Function2(External_Flash,
					  Get_Flash_Memory_Access,
					  UsageTableFlashID,
					  setmcuUsageInformationTable,
					  payload,
					  output2);
		for (int i = 0; i < readCountByCycle; i++) {
			mcuUsageInformationData[mcuUsageInformationCount + i] =
			    output2[7 + payload + i];
		}
		mcuUsageInformationCount += readCountByCycle;

	} while (mcuUsageInformationCount < UsageInfo);

	// Check & Transfer MCU Usage Information Table
	uint8_t mcuUIcrc[4];
	for (int i = 0; i < 4; i++) {
		mcuUIcrc[i] = mcuUsageInformationData[UsageTableSize - 4 + i];
		// printf("%02X\n",mcuUIcrc[i]);
	}
	uint32_t t = Compute(mcuUsageInformationData, UsageTableSize, 0, 4092);
	uint8_t *computecrc = ToBytes(t);
	if (!arraysEqual(mcuUIcrc, computecrc, 4)) {
		forceUpdate = true;
	}

	// Check Package Version to Clean Update List
	bool bcdVerUpdateReq = false;
	if (forceUpdate)
		bcdVerUpdateReq = true;

	if (!forceUpdate) {
		for (int i = 0; i < Interface1Length; i++)
			output1[i] = 0;
		int GetBcdData =
		    Function1(Device_Information, Get_Firmware_Version, 0, 0, 0, output1);
		uint8_t *dockFWPackageVer = GetCommandBody1(output1);
		uint8_t *targetFWPackageVer = targetUsageInformationTable.CompositeFwVersion;
		int dockFWPackageVerInt =
		    (dockFWPackageVer[0] << 16) + (dockFWPackageVer[1] << 8) + dockFWPackageVer[2];
		int targetFWPackageVerInt = (targetFWPackageVer[1] << 16) +
					    (targetFWPackageVer[2] << 8) + targetFWPackageVer[3];
		free(dockFWPackageVer);

		if (targetFWPackageVerInt > dockFWPackageVerInt)
			bcdVerUpdateReq = true;
	}
	// Check Dock Component Need to Update
	struct FlashIdUsageInformation *FlashIdUpdateList;
	struct UsageInformation changeTagetUsageInformationTable = targetUsageInformationTable;

	// if (bcdVerUpdateReq) {
	if (forceUpdate) {
		for (int i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
			changeTagetUsageInformationTable.FlashIdList[i] =
			    targetUsageInformationTable.FlashIdList[i];
			changeTagetUsageInformationTable.FlashIdList[i].Flag = true;
		}
	}

	// Compare The Component FW Version
	else {
		if (bcdVerUpdateReq) {
			struct UsageInformation mcuUsageInformationTable;
			struct FlashIdUsageInformation *mcuFlashIdList;
			mcuUsageInformationTable.Totalnumber = mcuUsageInformationData[0];
			mcuUsageInformationTable.MajorVersion =
			    ((mcuUsageInformationData[1] >> 4) & 0x0f);
			mcuUsageInformationTable.MinorVersion = (mcuUsageInformationData[1] & 0x0f);
			mcuUsageInformationTable.Dsa = (DSAType)mcuUsageInformationData[2];
			mcuUsageInformationTable.IoTUpdateFlag = mcuUsageInformationData[3];
			for (int i = 0; i < 4; i++) {
				mcuUsageInformationTable.CompositeFwVersion[i] =
				    mcuUsageInformationData[4 + i];
				mcuUsageInformationTable.Crc32[i] =
				    mcuUsageInformationData[UsageTableSize - 4 + i];
			}
			for (int i = 0; i < 2; i++) {
				mcuUsageInformationTable.DockPid[i] =
				    mcuUsageInformationData[8 + i];
			}
			mcuFlashIdList = (struct FlashIdUsageInformation *)calloc(
			    mcuUsageInformationTable.Totalnumber,
			    sizeof(struct FlashIdUsageInformation));
			for (int i = 1; i <= mcuUsageInformationTable.Totalnumber; i++) {
				for (int j = 0; j < FlashIdUsageLength; j++) {
					ds[j] = mcuUsageInformationData[i * 32 + j];
				}
				for (int j = 0; j < 4; j++) {
					mcuFlashIdList[i].PhysicalAddress[j] = ds[j];
					mcuFlashIdList[i].MaxSize[j] = ds[4 + j];
					mcuFlashIdList[i].CurrentFwVersion[j] = ds[8 + j];
					mcuFlashIdList[i].TargetFwVersion[j] = ds[12 + j];
					mcuFlashIdList[i].TargetFwFileSize[j] = ds[16 + j];
					mcuFlashIdList[i].TargetFwFileCrc32[j] = ds[20 + j];
					mcuFlashIdList[i].ComponentID = ds[24];
				}
			}

			for (int i = 1; i <= mcuUsageInformationTable.Totalnumber; i++) {
				if (!arraysEqual(
					mcuFlashIdList[i].TargetFwVersion,
					targetUsageInformationTable.FlashIdList[i].TargetFwVersion,
					4)) {
					changeTagetUsageInformationTable.FlashIdList[i] =
					    targetUsageInformationTable.FlashIdList[i];
					changeTagetUsageInformationTable.FlashIdList[i].Flag = true;
				} else {
					changeTagetUsageInformationTable.FlashIdList[i] =
					    mcuFlashIdList[i];
					changeTagetUsageInformationTable.FlashIdList[i].Flag =
					    false;
				}
			}
			free(mcuFlashIdList);
		}
	}

	// Check MCU in to phase-1 process
	bool NeedWriteTable = false;
	for (int i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
		if (changeTagetUsageInformationTable.FlashIdList[i].Flag) {
			NeedWriteTable = true;
			break;
		}
	}
	if (NeedWriteTable) {
		// Set Dock FW Update Ctrl
		uint8_t mcuUpdateCtrl[2];
		mcuUpdateCtrl[0] = nonLock;
		mcuUpdateCtrl[1] = InPhase1;
		for (int i = 0; i < Interface1Length; i++)
			output1[i] = 0;
		r = Function1(Dock, Set_Dock_Firmware_Upgrade_Ctrl, 0, mcuUpdateCtrl, 2, output1);

		// Clean Target Fw Version
		for (int i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
			for (int j = 0; j < 4; j++)
				changeTagetUsageInformationTable.FlashIdList[i].TargetFwVersion[j] =
				    0x00;
		}

		// Write Usage Information Table
		uint8_t *changeTagetUsageInformationTableBytes =
		    GetBytes(changeTagetUsageInformationTable);
		r = WriteUsageInformationTable(changeTagetUsageInformationTableBytes);
		free(changeTagetUsageInformationTableBytes);
		if (r != 0)
			return r;
	}

	// Phase-1 Start
	for (int i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
		// Flash ID update start
		int flashId = i;

		// Get Flash ID Attribute
		for (int j = 0; j < Interface1Length; j++)
			output1[j] = 0;
		int flashIdAttributeData =
		    Function1(External_Flash, Get_Flash_Attribute, flashId, 0, 0, output1);
		if (flashIdAttributeData != 0)
			return flashIdAttributeData;
		uint8_t *flashIdAttributeBody = GetCommandBody1(output1);
		struct FlashIdAttribute flashIdAttribute =
		    GetFlashIdAttribute(flashIdAttributeBody);
		free(flashIdAttributeBody);
		// Check Component FW File
		if (flashIdAttribute.Purpose != FirmwareFile)
			continue;

		// Check Update Necessary
		if (!changeTagetUsageInformationTable.FlashIdList[flashId]
			 .Flag) // no need to update
			continue;

		// Write Flash ID Data
		r = WriteFlashIdData(flashId,
				     flashIdAttribute,
				     changeTagetUsageInformationTable,
				     compositeImageData);
		if (r != 0)
			return r;

		// Write Target FW Version
		changeTagetUsageInformationTable.FlashIdList[flashId] =
		    targetUsageInformationTable.FlashIdList[flashId];

		// Write Usage Information Table
		r = WriteUsageInformationTable(GetBytes(changeTagetUsageInformationTable));
		if (r != 0)
			return r;
	}

	// Set Flash Memory Access (Release)
	uint8_t SetFlashMemoryAccessRelease[2];
	SetFlashMemoryAccessRelease[0] = AcccessCtrl;
	SetFlashMemoryAccessRelease[1] = Release;
	for (int i = 0; i < Interface2Length; i++)
		output2[i] = 0;
	r = Function2(External_Flash,
		      Set_Flash_Memory_Access,
		      0,
		      SetFlashMemoryAccessRelease,
		      2,
		      output2);

	// Set Dock FW Update Ctrl
	if (bcdVerUpdateReq) {
		TriggerPhase2(noUnplug);
	}

	return 0;
}

bool
CheckDockReadyForEnterPhase2Update()
{
	bool rs = false;

	uint8_t buffer[65];
	rs = Function1(Dock, Get_Dock_Firmware_Upgrade_Ctrl, 0, 0, 0, buffer);
	uint8_t *DockFirmwareCtrlBody = GetCommandBody1(buffer);
	if (DockFirmwareCtrlBody[0] == Locked && DockFirmwareCtrlBody[1] == 2)
		rs = true;

	return rs;
}

int
myclaim(int interface)
{
	// int release_interface = libusb_release_interface(devh,interface);
	// printf("release_interface%d :
	// %s\n",interface,libusb_strerror(release_interface));
	int r = libusb_claim_interface(devh, interface);
	if (r != 0)
		printf("interface%d error: %s\n", interface, libusb_strerror(r));

	return r;
}
int
init()
{
	libusb_close(devh);
	libusb_exit(ctx);
	// int r = libusb_init_context(/*ctx=*/NULL, /*options=*/NULL,
	// /*num_options=*/0);
	int r = libusb_init(&ctx);
	if (r < 0) {
		// fprintf(stderr, "failed to initialize libusb %d - %s\n", r,
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
int LIBUSB_CALL
hotplug_callback(struct libusb_context *ctx,
		 struct libusb_device *dev,
		 libusb_hotplug_event event,
		 void *user_data)
{
	struct libusb_device_descriptor desc;
	libusb_get_device_descriptor(dev, &desc);

	if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
		printf("Device disconnected: VID=0x%04x, PID=0x%04x\n",
		       desc.idVendor,
		       desc.idProduct);

		// 检查是否是目标设备
		if (desc.idVendor == DockVid && desc.idProduct == DockPid) {
			printf("Target device disconnected! Terminating program...\n");
			exit(-1);
		}
	}

	return 0; // 返回 0 表示回调未被注销
}
void *
usb_event_thread(void *arg)
{
	while (device_connected) {
		libusb_handle_events_completed(ctx, NULL); // 检测并触发 USB 热插拔回调事件
	}
	return NULL;
}

uint8_t
GetCurrentFwVerForGUI(int flashId, int index)
{
	return CurrentFwVerForGUI[flashId][index];
}

struct FlashIdUsageInformation *
check()
{
	uint8_t output[Interface1Length];
	for (int i = 0; i < Interface1Length; i++)
		output[i] = 0;
	int getFlashIdList = Function1(External_Flash, Get_Flash_ID_List, 0, 0, 0, output);
	// printf("getFlashIdList return %d\n",getFlashIdList);
	uint8_t totalFlashId = output[6];
	struct FlashIdUsageInformation *Info =
	    (struct FlashIdUsageInformation *)calloc(totalFlashId,
						     sizeof(struct FlashIdUsageInformation));

	for (int flashId = 1; flashId < totalFlashId; flashId++) {
		int getFlashIdAttribute =
		    Function1(External_Flash, Get_Flash_Attribute, flashId, 0, 0, output);
		uint8_t getPurpose = output[7];
		if (getPurpose == FirmwareFile) {
			for (int i = 0; i < Interface1Length; i++)
				output[i] = 0;
			uint8_t getFlashIdUsageInformation =
			    Function1(External_Flash,
				      Get_Flash_ID_Usage_Information,
				      flashId,
				      0,
				      0,
				      output);
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

int
GetCompositeVersion(char *out, size_t outSize)
{
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
					   1. FW update process is actually be executed because
				   Dock's current composite is < target composite version.
					   2. FW update process doesn't which actually erase & flash
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
