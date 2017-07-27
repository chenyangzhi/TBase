#pragma once
#include <windows.h>
#include <string>
#include <string.h>
#include <mutex>
#include <limits>
#include <condition_variable>
#include<assert.h>
#include <stdint.h>
#include"Status.h"

std::string GetWindowsErrSz(DWORD err);
inline size_t TruncateToPageBoundary(size_t page_size, size_t s) {
	s -= (s & (page_size - 1));
	assert((s % page_size) == 0);
	return s;
}

inline Status IOErrorFromWindowsError(const std::string& context, DWORD err) {
	return Status::IOError("1");
}

inline size_t Roundup(size_t x, size_t y) {
	return ((x + y - 1) / y) * y;
}
// A file abstraction for randomly reading the contents of a file.
class RandomAccessFile {
public:
	RandomAccessFile() { }
	virtual ~RandomAccessFile() {};

	// Read up to "n" bytes from the file starting at "offset".
	// "scratch[0..n-1]" may be written by this routine.  Sets "*result"
	// to the data that was read (including if fewer than "n" bytes were
	// successfully read).  May set "*result" to point at data in
	// "scratch[0..n-1]", so "scratch[0..n-1]" must be live when
	// "*result" is used.  If an error was encountered, returns a non-OK
	// status.
	//
	// Safe for concurrent use by multiple threads.
	virtual Status Read(uint64_t offset, size_t n, Slice* result,
		char* scratch) const = 0;

	// Used by the file_reader_writer to decide if the ReadAhead wrapper
	// should simply forward the call and do not enact buffering or locking.
	virtual bool ShouldForwardRawRequest() const {
		return false;
	}

	// For cases when read-ahead is implemented in the platform dependent
	// layer
	virtual void EnableReadAhead() {}

	// Tries to get an unique ID for this file that will be the same each time
	// the file is opened (and will stay the same while the file is open).
	// Furthermore, it tries to make this ID at most "max_size" bytes. If such an
	// ID can be created this function returns the length of the ID and places it
	// in "id"; otherwise, this function returns 0, in which case "id"
	// may not have been modified.
	//
	// This function guarantees, for IDs from a given environment, two unique ids
	// cannot be made equal to eachother by adding arbitrary bytes to one of
	// them. That is, no unique ID is the prefix of another.
	//
	// This function guarantees that the returned ID will not be interpretable as
	// a single varint.
	//
	// Note: these IDs are only valid for the duration of the process.
	virtual size_t GetUniqueId(char* id, size_t max_size) const {
		return 0; // Default implementation to prevent issues with backwards
				  // compatibility.
	};

	enum AccessPattern { NORMAL, RANDOM, SEQUENTIAL, WILLNEED, DONTNEED };

	virtual void Hint(AccessPattern pattern) {}

	// Remove any kind of caching of data from the offset to offset+length
	// of this file. If the length is 0, then it refers to the end of file.
	// If the system is not caching the file contents, then this is a noop.
	virtual Status InvalidateCache(size_t offset, size_t length) {
		return Status::NotSupported("InvalidateCache not supported.");
	}
};

class WinMmapReadableFile : public RandomAccessFile {
	const std::string fileName_;
	HANDLE hFile_;
	HANDLE hMap_;

	const void* mapped_region_;
	const size_t length_;

public:
	// mapped_region_[0,length-1] contains the mmapped contents of the file.
	WinMmapReadableFile(const std::string& fileName, HANDLE hFile, HANDLE hMap,
		const void* mapped_region, size_t length);

	~WinMmapReadableFile() {};

	virtual Status Read(uint64_t offset, size_t n, Slice* result,
		char* scratch) const override;

	virtual Status InvalidateCache(size_t offset, size_t length) override;

	virtual size_t GetUniqueId(char* id, size_t max_size) const override;
};

// A file abstraction for sequential writing.  The implementation
// must provide buffering since callers may append small fragments
// at a time to the file.
class WritableFile {
public:
	WritableFile()
		: last_preallocated_block_(0),
		preallocation_block_size_(0) {
	}
	virtual ~WritableFile() {};

	// Indicates if the class makes use of unbuffered I/O
	virtual bool UseOSBuffer() const {
		return true;
	}

	const size_t c_DefaultPageSize = 4 * 1024;

	// This is needed when you want to allocate
	// AlignedBuffer for use with file I/O classes
	// Used for unbuffered file I/O when UseOSBuffer() returns false
	virtual size_t GetRequiredBufferAlignment() const {
		return c_DefaultPageSize;
	}

	virtual Status Append(const Slice& data) = 0;

	// Positioned write for unbuffered access default forward
	// to simple append as most of the tests are buffered by default
	virtual Status PositionedAppend(const Slice& /* data */, uint64_t /* offset */) {
		return Status::NotSupported();
	}

	// Truncate is necessary to trim the file to the correct size
	// before closing. It is not always possible to keep track of the file
	// size due to whole pages writes. The behavior is undefined if called
	// with other writes to follow.
	virtual Status Truncate(uint64_t size) {
		return Status::OK();
	}
	virtual Status Close() = 0;
	virtual Status Flush() = 0;
	virtual Status Sync() = 0; // sync data

							   /*
							   * Sync data and/or metadata as well.
							   * By default, sync only data.
							   * Override this method for environments where we need to sync
							   * metadata as well.
							   */
	virtual Status Fsync() {
		return Sync();
	}

	// true if Sync() and Fsync() are safe to call concurrently with Append()
	// and Flush().
	virtual bool IsSyncThreadSafe() const {
		return false;
	}

	// Indicates the upper layers if the current WritableFile implementation
	// uses direct IO.
	virtual bool UseDirectIO() const { return false; }

	/*
	* Get the size of valid data in the file.
	*/
	virtual uint64_t GetFileSize() {
		return 0;
	}

	/*
	* Get and set the default pre-allocation block size for writes to
	* this file.  If non-zero, then Allocate will be used to extend the
	* underlying storage of a file (generally via fallocate) if the Env
	* instance supports it.
	*/
	virtual void SetPreallocationBlockSize(size_t size) {
		preallocation_block_size_ = size;
	}

	virtual void GetPreallocationStatus(size_t* block_size,
		size_t* last_allocated_block) {
		*last_allocated_block = last_preallocated_block_;
		*block_size = preallocation_block_size_;
	}

	// For documentation, refer to RandomAccessFile::GetUniqueId()
	virtual size_t GetUniqueId(char* id, size_t max_size) const {
		return 0; // Default implementation to prevent issues with backwards
	}

	// Remove any kind of caching of data from the offset to offset+length
	// of this file. If the length is 0, then it refers to the end of file.
	// If the system is not caching the file contents, then this is a noop.
	// This call has no effect on dirty pages in the cache.
	virtual Status InvalidateCache(size_t offset, size_t length) {
		return Status::NotSupported("InvalidateCache not supported.");
	}

	// Sync a file range with disk.
	// offset is the starting byte of the file range to be synchronized.
	// nbytes specifies the length of the range to be synchronized.
	// This asks the OS to initiate flushing the cached data to disk,
	// without waiting for completion.
	// Default implementation does nothing.
	virtual Status RangeSync(uint64_t offset, uint64_t nbytes) { return Status::OK(); }

	// PrepareWrite performs any necessary preparation for a write
	// before the write actually occurs.  This allows for pre-allocation
	// of space on devices where it can result in less file
	// fragmentation and/or less waste from over-zealous filesystem
	// pre-allocation.
	virtual void PrepareWrite(size_t offset, size_t len) {
		if (preallocation_block_size_ == 0) {
			return;
		}
		// If this write would cross one or more preallocation blocks,
		// determine what the last preallocation block necesessary to
		// cover this write would be and Allocate to that point.
		const auto block_size = preallocation_block_size_;
		size_t new_last_preallocated_block =
			(offset + len + block_size - 1) / block_size;
		if (new_last_preallocated_block > last_preallocated_block_) {
			size_t num_spanned_blocks =
				new_last_preallocated_block - last_preallocated_block_;
			Allocate(block_size * last_preallocated_block_,
				block_size * num_spanned_blocks);
			last_preallocated_block_ = new_last_preallocated_block;
		}
	}

protected:
	/*
	* Pre-allocate space for a file.
	*/
	virtual Status Allocate(uint64_t offset, uint64_t len) {
		return Status::OK();
	}

	size_t preallocation_block_size() { return preallocation_block_size_; }

private:
	size_t last_preallocated_block_;
	size_t preallocation_block_size_;
	// No copying allowed
	WritableFile(const WritableFile&);
	void operator=(const WritableFile&);

protected:
	friend class WritableFileWrapper;
	friend class WritableFileMirror;
};



// We preallocate and use memcpy to append new
// data to the file.  This is safe since we either properly close the
// file before reading from it, or for log files, the reading code
// knows enough to skip zero suffixes.
class WinMmapFile : public WritableFile {
private:
	const std::string filename_;
	HANDLE hFile_;
	HANDLE hMap_;

	const size_t page_size_;  // We flush the mapping view in page_size
							  // increments. We may decide if this is a memory
							  // page size or SSD page size
	const size_t
		allocation_granularity_;  // View must start at such a granularity

	size_t reserved_size_;      // Preallocated size

	size_t mapping_size_;         // The max size of the mapping object
								  // we want to guess the final file size to minimize the remapping
	size_t view_size_;            // How much memory to map into a view at a time

	char* mapped_begin_;  // Must begin at the file offset that is aligned with
						  // allocation_granularity_
	char* mapped_end_;
	char* dst_;  // Where to write next  (in range [mapped_begin_,mapped_end_])
	char* last_sync_;  // Where have we synced up to

	uint64_t file_offset_;  // Offset of mapped_begin_ in file

							// Do we have unsynced writes?
	bool pending_sync_;

	// Can only truncate or reserve to a sector size aligned if
	// used on files that are opened with Unbuffered I/O
	Status TruncateFile(uint64_t toSize);

	Status UnmapCurrentRegion();

	Status MapNewRegion();

	virtual Status PreallocateInternal(uint64_t spaceToReserve);

public:

	WinMmapFile(const std::string& fname, HANDLE hFile, size_t page_size,
		size_t allocation_granularity);

	~WinMmapFile();

	virtual Status Append(const Slice& data) override;

	// Means Close() will properly take care of truncate
	// and it does not need any additional information
	virtual Status Truncate(uint64_t size) override;

	virtual Status Close() override;

	virtual Status Flush() override;

	// Flush only data
	virtual Status Sync() override;

	/**
	* Flush data as well as metadata to stable storage.
	*/
	virtual Status Fsync() override;

	/**
	* Get the size of valid data in the file. This will not match the
	* size that is returned from the filesystem because we use mmap
	* to extend file by map_size every time.
	*/
	virtual uint64_t GetFileSize() override;

	virtual Status InvalidateCache(size_t offset, size_t length) override;

	virtual Status Allocate(uint64_t offset, uint64_t len) override;

	virtual size_t GetUniqueId(char* id, size_t max_size) const override;
};