#pragma once
#include <Windows.h>
#include <tuple>
#include <iostream>
#include <fstream>

namespace read {
	std::tuple<BYTE*, uintptr_t> f_info(const char* fName);
}

