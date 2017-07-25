#pragma once
#include <string>
#include <stdint.h>
#include<memory>
#include"Slice.h"
struct BlockContents {
	Slice data;           // Actual contents of data
	bool cachable;        // True iff data can be cached
	std::unique_ptr<char[]> allocation;

	BlockContents() : cachable(false) {}

	BlockContents(const Slice& _data, bool _cachable)
		: data(_data), cachable(_cachable) {}

	BlockContents(std::unique_ptr<char[]>&& _data, int _size, bool _cachable)
		: data(_data.get(), _size),
		cachable(_cachable),
		allocation(std::move(_data)) {}

	BlockContents(BlockContents&& other) { *this = std::move(other); }

	BlockContents& operator=(BlockContents&& other) {
		data = std::move(other.data);
		cachable = other.cachable;
		allocation = std::move(other.allocation);
		return *this;
	}
};