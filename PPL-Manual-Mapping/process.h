#pragma once
#include <Windows.h>

#define DISABLE_OUTPUT 1

class process
{
public:
	STARTUPINFOA startupInfo;
	PROCESS_INFORMATION	procInfo; 
	static process StartSuspended(const char* procName);
	void ResumeProcess();
	void SuspendProcess();
	void Cleanup();
	bool MapDll(BYTE* pSrcData, SIZE_T FileSize, bool ClearHeader = true, bool ClearNonNeededSections = true, bool AdjustProtections = true, bool SEHExceptionSupport = true, DWORD fdwReason = DLL_PROCESS_ATTACH, LPVOID lpReserved = 0);
	process(STARTUPINFOA startupInfo, PROCESS_INFORMATION procInfo);
	static process Start(const char* procName);
};

