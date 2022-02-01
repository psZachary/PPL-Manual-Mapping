#include <iostream>
#include <Windows.h>
#include <err.h>
#include <process.h>
#include <fstream>
#include <read.h>
int main()
{
	const char* procName = "Test-App.exe";
	process p = process::StartSuspended(procName);

	auto [data, size] = read::f_info("Test-Dll.dll");

	if (!p.MapDll(data, size)) {
		err::error();
	}
	
	Sleep(3000);

	p.ResumeProcess();
}
