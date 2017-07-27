#include<iostream>
#include"iowin.h"
using namespace std;
int main() 
{
	HANDLE hFile = 0;
	HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY,
		0,  // Whole file at its present length
		0,
		NULL);
	cout << "hello world" << endl;
	Slice key("key1");
	Slice value("value1");
	WinMmapFile wfile("E:\\TBaseFile\\tmpfile",hFile,4096,1024);
	wfile.Append(key);
	wfile.Append(value);
	wfile.Flush();
	system("pause");
	return 0;
}