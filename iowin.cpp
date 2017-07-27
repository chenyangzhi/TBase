#include<assert.h>
#include<algorithm>
#include "iowin.h"

Status WinMmapReadableFile::InvalidateCache(size_t offset, size_t length) {
	return Status::OK();
}

size_t WinMmapReadableFile::GetUniqueId(char* id, size_t max_size) const {
	return 1;
}
size_t WinMmapFile::GetUniqueId(char* id, size_t max_size) const {
	return 1;
}

Status WinMmapFile::InvalidateCache(size_t offset, size_t length) {
	return Status::OK();
}

std::string GetWindowsErrSz(DWORD err) {
	LPSTR lpMsgBuf;
	FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, err,
		0,  // Default language
		reinterpret_cast<LPSTR>(&lpMsgBuf), 0, NULL);

	std::string Err = lpMsgBuf;
	LocalFree(lpMsgBuf);
	return Err;
}

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

Status WinMmapReadableFile::Read(uint64_t offset, size_t n, Slice* result,
	char* scratch) const {
	Status s;

	if (offset > length_) {
		*result = Slice();
		return Status::IOError("1","1");
	}
	else if (offset + n > length_) {
		n = length_ - offset;
	}
	*result =
		Slice(reinterpret_cast<const char*>(mapped_region_) + offset, n);
	return s;
}

WinMmapFile::WinMmapFile(const std::string& fname, HANDLE hFile, size_t page_size,
	size_t allocation_granularity)
	: filename_(fname),
	hFile_(hFile),
	hMap_(NULL),
	page_size_(page_size),
	allocation_granularity_(allocation_granularity),
	reserved_size_(0),
	mapping_size_(0),
	view_size_(0),
	mapped_begin_(nullptr),
	mapped_end_(nullptr),
	dst_(nullptr),
	last_sync_(nullptr),
	file_offset_(0),
	pending_sync_(false) {
	// View size must be both the multiple of allocation_granularity AND the
	// page size and the granularity is usually a multiple of a page size.
	const size_t viewSize = 32 * 1024; // 32Kb similar to the Windows File Cache in buffered mode
	view_size_ = Roundup(viewSize, allocation_granularity_);
}

WinMmapFile::~WinMmapFile() {
	if (hFile_) {
		this->Close();
	}
}

Status WinMmapFile::Append(const Slice& data) {
	const char* src = data.data();
	size_t left = data.size();

	while (left > 0) {
		assert(mapped_begin_ <= dst_);
		size_t avail = mapped_end_ - dst_;

		if (avail == 0) {
			Status s = UnmapCurrentRegion();
			if (s.ok()) {
				s = MapNewRegion();
			}

			if (!s.ok()) {
				return s;
			}
		}
		else {
			size_t n = min(left, avail);
			memcpy(dst_, src, n);
			dst_ += n;
			src += n;
			left -= n;
			pending_sync_ = true;
		}
	}

	// Now make sure that the last partial page is padded with zeros if needed
	size_t bytesToPad = Roundup(size_t(dst_), page_size_) - size_t(dst_);
	if (bytesToPad > 0) {
		memset(dst_, 0, bytesToPad);
	}

	return Status::OK();
}

// Means Close() will properly take care of truncate
// and it does not need any additional information
Status WinMmapFile::Truncate(uint64_t size) {
	return Status::OK();
}

Status WinMmapFile::Close() {
	Status s;

	assert(NULL != hFile_);

	// We truncate to the precise size so no
	// uninitialized data at the end. SetEndOfFile
	// which we use does not write zeros and it is good.
	uint64_t targetSize = GetFileSize();

	if (mapped_begin_ != nullptr) {
		// Sync before unmapping to make sure everything
		// is on disk and there is not a lazy writing
		// so we are deterministic with the tests
		Sync();
		s = UnmapCurrentRegion();
	}

	if (NULL != hMap_) {
		BOOL ret = ::CloseHandle(hMap_);
		if (!ret && s.ok()) {
			auto lastError = GetLastError();
			s = IOErrorFromWindowsError(
				"Failed to Close mapping for file: " + filename_, lastError);
		}

		hMap_ = NULL;
	}

	if (hFile_ != NULL) {

		TruncateFile(targetSize);

		BOOL ret = ::CloseHandle(hFile_);
		hFile_ = NULL;

		if (!ret && s.ok()) {
			auto lastError = GetLastError();
			s = IOErrorFromWindowsError(
				"Failed to close file map handle: " + filename_, lastError);
		}
	}

	return s;
}

Status WinMmapFile::Flush() { return Status::OK(); }

// Flush only data
Status WinMmapFile::Sync() {
	Status s;

	// Some writes occurred since last sync
	if (dst_ > last_sync_) {
		assert(mapped_begin_);
		assert(dst_);
		assert(dst_ > mapped_begin_);
		assert(dst_ < mapped_end_);

		size_t page_begin =
			TruncateToPageBoundary(page_size_, last_sync_ - mapped_begin_);
		size_t page_end =
			TruncateToPageBoundary(page_size_, dst_ - mapped_begin_ - 1);

		// Flush only the amount of that is a multiple of pages
		if (!::FlushViewOfFile(mapped_begin_ + page_begin,
			(page_end - page_begin) + page_size_)) {
			s = IOErrorFromWindowsError("Failed to FlushViewOfFile: " + filename_,
				GetLastError());
		}
		else {
			last_sync_ = dst_;
		}
	}

	return s;
}

/**
* Flush data as well as metadata to stable storage.
*/
Status WinMmapFile::Fsync() {
	Status s = Sync();

	// Flush metadata
	if (s.ok() && pending_sync_) {
		if (!::FlushFileBuffers(hFile_)) {
			s = IOErrorFromWindowsError("Failed to FlushFileBuffers: " + filename_,
				GetLastError());
		}
		pending_sync_ = false;
	}

	return s;
}

Status ftruncate(const std::string& filename, HANDLE hFile,
	uint64_t toSize) {
	Status status;

	FILE_END_OF_FILE_INFO end_of_file;
	end_of_file.EndOfFile.QuadPart = toSize;

	if (!SetFileInformationByHandle(hFile, FileEndOfFileInfo, &end_of_file,
		sizeof(FILE_END_OF_FILE_INFO))) {
		auto lastError = GetLastError();
		status = IOErrorFromWindowsError("Failed to Set end of file: " + filename,
			lastError);
	}

	return status;
}

// Can only truncate or reserve to a sector size aligned if
// used on files that are opened with Unbuffered I/O
Status WinMmapFile::TruncateFile(uint64_t toSize) {
	return ftruncate(filename_, hFile_, toSize);
}

Status WinMmapFile::UnmapCurrentRegion() {
	Status status;

	if (mapped_begin_ != nullptr) {
		if (!::UnmapViewOfFile(mapped_begin_)) {
			status = IOErrorFromWindowsError(
				"Failed to unmap file view: " + filename_, GetLastError());
		}

		// Move on to the next portion of the file
		file_offset_ += view_size_;

		// UnmapView automatically sends data to disk but not the metadata
		// which is good and provides some equivalent of fdatasync() on Linux
		// therefore, we donot need separate flag for metadata
		mapped_begin_ = nullptr;
		mapped_end_ = nullptr;
		dst_ = nullptr;

		last_sync_ = nullptr;
		pending_sync_ = false;
	}

	return status;
}

Status WinMmapFile::MapNewRegion() {

	Status status;

	assert(mapped_begin_ == nullptr);

	size_t minDiskSize = file_offset_ + view_size_;

	if (minDiskSize > reserved_size_) {
		status = Allocate(file_offset_, view_size_);
		if (!status.ok()) {
			return status;
		}
	}

	// Need to remap
	if (hMap_ == NULL || reserved_size_ > mapping_size_) {

		if (hMap_ != NULL) {
			// Unmap the previous one
			BOOL ret = ::CloseHandle(hMap_);
			assert(ret);
			hMap_ = NULL;
		}

		ULARGE_INTEGER mappingSize;
		mappingSize.QuadPart = reserved_size_;

		hMap_ = CreateFileMappingA(
			hFile_,
			NULL,                  // Security attributes
			PAGE_READWRITE,        // There is not a write only mode for mapping
			mappingSize.HighPart,  // Enable mapping the whole file but the actual
								   // amount mapped is determined by MapViewOfFile
			mappingSize.LowPart,
			NULL);  // Mapping name

		if (NULL == hMap_) {
			return IOErrorFromWindowsError(
				"WindowsMmapFile failed to create file mapping for: " + filename_,
				GetLastError());
		}

		mapping_size_ = reserved_size_;
	}

	ULARGE_INTEGER offset;
	offset.QuadPart = file_offset_;

	// View must begin at the granularity aligned offset
	mapped_begin_ = reinterpret_cast<char*>(
		MapViewOfFileEx(hMap_, FILE_MAP_WRITE, offset.HighPart, offset.LowPart,
			view_size_, NULL));

	if (!mapped_begin_) {
		status = IOErrorFromWindowsError(
			"WindowsMmapFile failed to map file view: " + filename_,
			GetLastError());
	}
	else {
		mapped_end_ = mapped_begin_ + view_size_;
		dst_ = mapped_begin_;
		last_sync_ = mapped_begin_;
		pending_sync_ = false;
	}
	return status;
}

Status fallocate(const std::string& filename, HANDLE hFile,
	uint64_t to_size) {
	Status status;

	FILE_ALLOCATION_INFO alloc_info;
	alloc_info.AllocationSize.QuadPart = to_size;

	if (!SetFileInformationByHandle(hFile, FileAllocationInfo, &alloc_info,
		sizeof(FILE_ALLOCATION_INFO))) {
		auto lastError = GetLastError();
		status = IOErrorFromWindowsError(
			"Failed to pre-allocate space: " + filename, lastError);
	}

	return status;
}

Status WinMmapFile::PreallocateInternal(uint64_t spaceToReserve) {
	return fallocate(filename_, hFile_, spaceToReserve);
}

uint64_t WinMmapFile::GetFileSize() {
	size_t used = dst_ - mapped_begin_;
	return file_offset_ + used;
}

Status WinMmapFile::Allocate(uint64_t offset, uint64_t len) {
	Status status;
	// Make sure that we reserve an aligned amount of space
	// since the reservation block size is driven outside so we want
	// to check if we are ok with reservation here
	size_t spaceToReserve = Roundup(offset + len, view_size_);
	// Nothing to do
	if (spaceToReserve <= reserved_size_) {
		return status;
	}

	status = PreallocateInternal(spaceToReserve);
	if (status.ok()) {
		reserved_size_ = spaceToReserve;
	}
	return status;
}

WinMmapReadableFile::WinMmapReadableFile(const std::string& fileName, HANDLE hFile, HANDLE hMap,
	const void* mapped_region, size_t length)
	: fileName_(fileName),
	hFile_(hFile),
	hMap_(hMap),
	mapped_region_(mapped_region),
	length_(length) {}