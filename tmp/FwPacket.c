#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include "dock_command_declaration.h"

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