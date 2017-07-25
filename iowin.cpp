#include<assert.h>
#include "iowin.h"

SSIZE_T pwrite(HANDLE hFile, const char* src, size_t numBytes,uint64_t offset) {
	OVERLAPPED overlapped = { 0 };
	ULARGE_INTEGER offsetUnion;
	offsetUnion.QuadPart = offset;

	overlapped.Offset = offsetUnion.LowPart;
    overlapped.OffsetHigh = offsetUnion.HighPart;

	SSIZE_T result = 0;

	unsigned long bytesWritten = 0;

	if (FALSE == WriteFile(hFile, src, static_cast<DWORD>(numBytes), &bytesWritten,	&overlapped)) {
		result = -1;
	} else {
		result = bytesWritten;
	}

	return result;
}

SSIZE_T pread(HANDLE hFile, char* src, size_t numBytes, uint64_t offset) {
			OVERLAPPED overlapped = { 0 };
			ULARGE_INTEGER offsetUnion;
			offsetUnion.QuadPart = offset;

			overlapped.Offset = offsetUnion.LowPart;
			overlapped.OffsetHigh = offsetUnion.HighPart;

			SSIZE_T result = 0;

			unsigned long bytesRead = 0;

			if (FALSE == ReadFile(hFile, src, static_cast<DWORD>(numBytes), &bytesRead,
				&overlapped)) {
				return -1;
			}
			else {
				result = bytesRead;
			}

			return result;
}

