#pragma once
#include <stdint.h>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include<memory>
#include"Slice.h"

class BlockBasedTableBuilder {
 public:
  BlockBasedTableBuilder();
  ~BlockBasedTableBuilder();
  void Add(const Slice& key, const Slice& value);
  uint64_t NumEntries() const ;
  uint64_t FileSize() const;

};
