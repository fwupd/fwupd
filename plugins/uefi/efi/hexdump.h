#ifndef STATIC_HEXDUMP_H
#define STATIC_HEXDUMP_H

static int
__attribute__((__unused__))
isprint(char c)
{
	if (c < 0x20)
		return 0;
	if (c > 0x7e)
		return 0;
	return 1;
}

static UINTN
__attribute__((__unused__))
format_hex(UINT8 *data, UINTN size, CHAR16 *buf)
{
	UINTN sz = (UINTN)data % 16;
	CHAR16 hexchars[] = L"0123456789abcdef";
	int offset = 0;
	unsigned int i;
	unsigned int j;

	for (i = 0; i < sz; i++) {
		buf[offset++] = L' ';
		buf[offset++] = L' ';
		buf[offset++] = L' ';
		if (i == 7)
			buf[offset++] = L' ';
	}
	for (j = sz; j < 16 && j < size; j++) {
		UINT8 d = data[j-sz];
		buf[offset++] = hexchars[(d & 0xf0) >> 4];
		buf[offset++] = hexchars[(d & 0x0f)];
		if (j != 15)
			buf[offset++] = L' ';
		if (j == 7)
			buf[offset++] = L' ';
	}
	for (i = j; i < 16; i++) {
		buf[offset++] = L' ';
		buf[offset++] = L' ';
		if (i != 15)
			buf[offset++] = L' ';
		if (i == 7)
			buf[offset++] = L' ';
	}
	buf[offset] = L'\0';
	return j - sz;
}

static void
__attribute__((__unused__))
format_text(UINT8 *data, UINTN size, CHAR16 *buf)
{
	UINTN sz = (UINTN)data % 16;
	int offset = 0;
	unsigned int i;
	unsigned int j;

	for (i = 0; i < sz; i++)
		buf[offset++] = L' ';
	buf[offset++] = L'|';
	for (j = sz; j < 16 && j < size; j++) {
		if (isprint(data[j-sz]))
			buf[offset++] = data[j-sz];
		else
			buf[offset++] = L'.';
	}
	buf[offset++] = L'|';
	for (i = j; i < 16; i++)
		buf[offset++] = L' ';
	buf[offset] = L'\0';
}

static void
__attribute__((__unused__))
hexdump(UINT8 *data, UINTN size)
{
	UINTN display_offset = (UINTN)data & 0xffffffff;
	UINTN offset = 0;
	//Print(L"hexdump: data=0x%016x size=0x%x\n", data, size);

	while (offset < size) {
		CHAR16 hexbuf[49];
		CHAR16 txtbuf[19];
		UINTN sz;

		sz = format_hex(data+offset, size-offset, hexbuf);
		if (sz == 0)
			return;
		uefi_call_wrapper(BS->Stall, 1, 200000);

		format_text(data+offset, size-offset, txtbuf);
		Print(L"%08x  %s  %s\n", display_offset, hexbuf, txtbuf);
		uefi_call_wrapper(BS->Stall, 1, 200000);

		display_offset += sz;
		offset += sz;
	}
}


#endif
