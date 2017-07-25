#pragma once
#include<string>
#include<vector>
#include"Status.h"
class DB {
public:
	static Status Open(const DBOptions& db_options, const std::string& name,
		const std::vector<ColumnFamilyDescriptor>& column_families,
		std::vector<ColumnFamilyHandle*>* handles, DB** dbptr);

	static Status ListColumnFamilies(const DBOptions& db_options,
		const std::string& name,
		std::vector<std::string>* column_families);

	DB() { }
	virtual ~DB();
	virtual Status CreateColumnFamily(const ColumnFamilyOptions& options,
		const std::string& column_family_name,
		ColumnFamilyHandle** handle);
	virtual Status DropColumnFamily(ColumnFamilyHandle* column_family);
	virtual Status Put(const WriteOptions& options,
		ColumnFamilyHandle* column_family, const Slice& key,
		const Slice& value) = 0;
	virtual Status Delete(const WriteOptions& options,
		ColumnFamilyHandle* column_family,
		const Slice& key) = 0;
	virtual Status Write(const WriteOptions& options, WriteBatch* updates) = 0;
	virtual Status Get(const ReadOptions& options,
		ColumnFamilyHandle* column_family, const Slice& key,
		std::string* value) = 0;
	virtual std::vector<Status> MultiGet(
		const ReadOptions& options,
		const std::vector<ColumnFamilyHandle*>& column_family,
		const std::vector<Slice>& keys, std::vector<std::string>* values) = 0;

	virtual bool KeyMayExist(const ReadOptions& options, const Slice& key,
		std::string* value, bool* value_found = nullptr) {
		return KeyMayExist(options, DefaultColumnFamily(), key, value, value_found);
	}
	virtual Iterator* NewIterator(const ReadOptions& options,
		ColumnFamilyHandle* column_family) = 0;
	virtual Iterator* NewIterator(const ReadOptions& options) {
		return NewIterator(options, DefaultColumnFamily());
	}
	virtual Status NewIterators(
		const ReadOptions& options,
		const std::vector<ColumnFamilyHandle*>& column_families,
		std::vector<Iterator*>* iterators) = 0;

private:
	DB(const DB&);
	void operator=(const DB&);
};