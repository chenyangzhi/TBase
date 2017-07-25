#pragma once
#include"Slice.h"
#include<memory>
#include"Format.h"
class TFileBlock {
public:
	TFileBlock(BlockContents contents);
	~TFileBlock();
	size_t size() const { return size_; }
	const char* data() const { return data_; }
private:
	BlockContents contents;
	const char* data_;            // contents_.data.data()
	size_t size_;
};