#include "common.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <stdio.h>

// implement external utilities
#define FIRE_OS_WINDOW_IMPLEMENTATION

// -- Globals ---------------
extern DS_Arena* TEMP;
// --------------------------

bool OS_FileLastModificationTime(STR_View filepath, uint64_t* out_modtime) {
	wchar_t filepath_wide[MAX_PATH];
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, STR_ToC(TEMP, filepath.data), -1, filepath_wide, MAX_PATH);

	HANDLE h = CreateFileW(filepath_wide, 0, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY | FILE_FLAG_BACKUP_SEMANTICS, NULL);
	bool ok = h != INVALID_HANDLE_VALUE;
	if (ok) {
		ok = GetFileTime(h, NULL, NULL, (FILETIME*)out_modtime) != 0;
		CloseHandle(h);
	}
	return ok;
}

void OS_SleepMilliseconds(uint32_t ms) {
	Sleep(ms);
}

void OS_MessageBox(STR_View message) {
	MessageBoxA(NULL, STR_ToC(TEMP, message), "INFO", 0);
}

bool OS_ReadEntireFile(DS_Arena* arena, STR_View filepath, STR_View* out_data) {
	FILE* f = NULL;
	errno_t err = fopen_s(&f, STR_ToC(TEMP, filepath), "rb");
	if (f) {
		fseek(f, 0, SEEK_END);
		long fsize = ftell(f);
		fseek(f, 0, SEEK_SET);

		char* data = DS_ArenaPush(arena, fsize);
		fread(data, fsize, 1, f);

		fclose(f);
		STR_View result = {data, (size_t)fsize};
		*out_data = result;
	}
	return f != NULL;
}
