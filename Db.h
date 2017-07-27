#pragma once
#include<string>
#include<vector>
#include"Status.h"
class DB {
public:
	DB() { }
	virtual ~DB();
private:
	DB(const DB&);
	void operator=(const DB&);
};