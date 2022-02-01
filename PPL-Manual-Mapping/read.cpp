#include "read.h"
std::tuple<BYTE*, uintptr_t> read::f_info(const char* fName) {
	std::ifstream f(fName, std::ios::binary | std::ios::ate);
	auto size = f.tellg();
	BYTE* data = new BYTE[(uintptr_t)size];
	f.seekg(0, std::ios::beg);
	f.read((char*)(data), size);
	f.close();
	return std::tuple<BYTE*, uintptr_t>(data, size);
}