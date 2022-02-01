#include "process.h"
#include <err.h>
#include <map.h>
#include <iostream>

process::process(STARTUPINFOA startupInfo, PROCESS_INFORMATION procInfo) {
	this->procInfo = procInfo;
	this->startupInfo = startupInfo;
}

process process::Start(const char* procName) {
	STARTUPINFOA startupInfo; ZeroMemory(&startupInfo, sizeof(STARTUPINFOA));
	PROCESS_INFORMATION procInfo; ZeroMemory(&procInfo, sizeof(PROCESS_INFORMATION));
	if (!CreateProcessA(procName, 0, 0, 0, 0, 0, 0, 0, &startupInfo, &procInfo)) // ignore cause we are using the handles
		err::error();
	return process(startupInfo, procInfo);
}

process process::StartSuspended(const char* procName) {
	STARTUPINFOA startupInfo; ZeroMemory(&startupInfo, sizeof(STARTUPINFOA));
	PROCESS_INFORMATION procInfo; ZeroMemory(&procInfo, sizeof(PROCESS_INFORMATION));
	if (!CreateProcessA(procName, 0, 0, 0, 0, CREATE_SUSPENDED, 0, 0, &startupInfo, &procInfo)) // ignore cause we are using the handles
		err::error();
	return process(startupInfo, procInfo);
}

void process::ResumeProcess()
{
	if (ResumeThread(procInfo.hThread) == -1)
		err::error();
}

void process::SuspendProcess() {
	if (SuspendThread(procInfo.hThread) == -1)
		err::error();
}

void process::Cleanup()
{
	if (!CloseHandle(procInfo.hProcess))
		err::error();
	if (!CloseHandle(procInfo.hThread))
		err::error();

	ZeroMemory(&procInfo, sizeof(PROCESS_INFORMATION));
	ZeroMemory(&startupInfo, sizeof(STARTUPINFOA));

	delete this;
}

void __stdcall Shellcode(MANUAL_MAPPING_DATA* pData) {
	if (!pData) {
		pData->hMod = (HINSTANCE)0x404040;
		return;
	}

	BYTE* pBase = pData->pbase;
	auto* pOpt = &reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + reinterpret_cast<IMAGE_DOS_HEADER*>((uintptr_t)pBase)->e_lfanew)->OptionalHeader;

	auto _LoadLibraryA = pData->pLoadLibraryA;
	auto _GetProcAddress = pData->pGetProcAddress;
#ifdef _WIN64
	auto _RtlAddFunctionTable = pData->pRtlAddFunctionTable;
#endif
	auto _DllMain = reinterpret_cast<f_DLL_ENTRY_POINT>(pBase + pOpt->AddressOfEntryPoint);

	BYTE* LocationDelta = pBase - pOpt->ImageBase;
	if (LocationDelta) {
		if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size) {
			auto* pRelocData = reinterpret_cast<IMAGE_BASE_RELOCATION*>(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
			const auto* pRelocEnd = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<uintptr_t>(pRelocData) + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size);
			while (pRelocData < pRelocEnd && pRelocData->SizeOfBlock) {
				UINT AmountOfEntries = (pRelocData->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
				WORD* pRelativeInfo = reinterpret_cast<WORD*>(pRelocData + 1);

				for (UINT i = 0; i != AmountOfEntries; ++i, ++pRelativeInfo) {
					if (RELOC_FLAG(*pRelativeInfo)) {
						UINT_PTR* pPatch = reinterpret_cast<UINT_PTR*>(pBase + pRelocData->VirtualAddress + ((*pRelativeInfo) & 0xFFF));
						*pPatch += reinterpret_cast<UINT_PTR>(LocationDelta);
					}
				}
				pRelocData = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<BYTE*>(pRelocData) + pRelocData->SizeOfBlock);
			}
		}
	}

	if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) {
		auto* pImportDescr = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
		while (pImportDescr->Name) {
			char* szMod = reinterpret_cast<char*>(pBase + pImportDescr->Name);
			HINSTANCE hDll = _LoadLibraryA(szMod);

			ULONG_PTR* pThunkRef = reinterpret_cast<ULONG_PTR*>(pBase + pImportDescr->OriginalFirstThunk);
			ULONG_PTR* pFuncRef = reinterpret_cast<ULONG_PTR*>(pBase + pImportDescr->FirstThunk);

			if (!pThunkRef)
				pThunkRef = pFuncRef;

			for (; *pThunkRef; ++pThunkRef, ++pFuncRef) {
				if (IMAGE_SNAP_BY_ORDINAL(*pThunkRef)) {
					*pFuncRef = (ULONG_PTR)_GetProcAddress(hDll, reinterpret_cast<char*>(*pThunkRef & 0xFFFF));
				}
				else {
					auto* pImport = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(pBase + (*pThunkRef));
					*pFuncRef = (ULONG_PTR)_GetProcAddress(hDll, pImport->Name);
				}
			}
			++pImportDescr;
		}
	}

	if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size) {
		auto* pTLS = reinterpret_cast<IMAGE_TLS_DIRECTORY*>(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
		auto* pCallback = reinterpret_cast<PIMAGE_TLS_CALLBACK*>(pTLS->AddressOfCallBacks);
		for (; pCallback && *pCallback; ++pCallback)
			(*pCallback)(pBase, DLL_PROCESS_ATTACH, nullptr);
	}

	bool ExceptionSupportFailed = false;

#ifdef _WIN64

	if (pData->SEHSupport) {
		auto excep = pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
		if (excep.Size) {
			if (!_RtlAddFunctionTable(
				reinterpret_cast<IMAGE_RUNTIME_FUNCTION_ENTRY*>(pBase + excep.VirtualAddress),
				excep.Size / sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY), (DWORD64)pBase)) {
				ExceptionSupportFailed = true;
			}
		}
	}

#endif

	_DllMain(pBase, pData->fdwReasonParam, pData->reservedParam);

	if (ExceptionSupportFailed)
		pData->hMod = reinterpret_cast<HINSTANCE>(0x505050);
	else
		pData->hMod = reinterpret_cast<HINSTANCE>(pBase);
}


#if defined(DISABLE_OUTPUT)
#define ILog(data, ...)
#else
#define ILog(text, ...) printf(text, __VA_ARGS__);
#endif


bool process::MapDll(BYTE* pSrcData, SIZE_T FileSize, bool ClearHeader, bool ClearNonNeededSections, bool AdjustProtections, bool SEHExceptionSupport, DWORD fdwReason, LPVOID lpReserved) {
	IMAGE_NT_HEADERS* pOldNtHeader = nullptr;
	IMAGE_OPTIONAL_HEADER* pOldOptHeader = nullptr;
	IMAGE_FILE_HEADER* pOldFileHeader = nullptr;
	BYTE* pTargetBase = nullptr;

	if (reinterpret_cast<IMAGE_DOS_HEADER*>(pSrcData)->e_magic != 0x5A4D) { //"MZ"
		ILog("Invalid file\n");
		return false;
	}

	pOldNtHeader = reinterpret_cast<IMAGE_NT_HEADERS*>(pSrcData + reinterpret_cast<IMAGE_DOS_HEADER*>(pSrcData)->e_lfanew);
	pOldOptHeader = &pOldNtHeader->OptionalHeader;
	pOldFileHeader = &pOldNtHeader->FileHeader;

	if (pOldFileHeader->Machine != CURRENT_ARCH) {
		ILog("Invalid platform\n");
		return false;
	}

	ILog("File ok\n");

	pTargetBase = reinterpret_cast<BYTE*>(VirtualAllocEx(procInfo.hProcess, nullptr, pOldOptHeader->SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!pTargetBase) {
		ILog("Target process memory allocation failed (ex) 0x%X\n", GetLastError());
		return false;
	}

	DWORD oldp = 0;
	VirtualProtectEx(procInfo.hProcess, pTargetBase, pOldOptHeader->SizeOfImage, PAGE_EXECUTE_READWRITE, &oldp);

	MANUAL_MAPPING_DATA data{ 0 };
	data.pLoadLibraryA = LoadLibraryA;
	data.pGetProcAddress = GetProcAddress;
#ifdef _WIN64
	data.pRtlAddFunctionTable = (f_RtlAddFunctionTable)RtlAddFunctionTable;
#else 
	SEHExceptionSupport = false;
#endif
	data.pbase = pTargetBase;
	data.fdwReasonParam = fdwReason;
	data.reservedParam = lpReserved;
	data.SEHSupport = SEHExceptionSupport;


	//File header
	if (!WriteProcessMemory(procInfo.hProcess, pTargetBase, pSrcData, 0x1000, nullptr)) { //only first 0x1000 bytes for the header
		ILog("Can't write file header 0x%X\n", GetLastError());
		VirtualFreeEx(procInfo.hProcess, pTargetBase, 0, MEM_RELEASE);
		return false;
	}

	IMAGE_SECTION_HEADER* pSectionHeader = IMAGE_FIRST_SECTION(pOldNtHeader);
	for (UINT i = 0; i != pOldFileHeader->NumberOfSections; ++i, ++pSectionHeader) {
		if (pSectionHeader->SizeOfRawData) {
			if (!WriteProcessMemory(procInfo.hProcess, pTargetBase + pSectionHeader->VirtualAddress, pSrcData + pSectionHeader->PointerToRawData, pSectionHeader->SizeOfRawData, nullptr)) {
				ILog("Can't map sections: 0x%x\n", GetLastError());
				VirtualFreeEx(procInfo.hProcess, pTargetBase, 0, MEM_RELEASE);
				return false;
			}
		}
	}

	//Mapping params
	BYTE* MappingDataAlloc = reinterpret_cast<BYTE*>(VirtualAllocEx(procInfo.hProcess, nullptr, sizeof(MANUAL_MAPPING_DATA), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!MappingDataAlloc) {
		ILog("Target process mapping allocation failed (ex) 0x%X\n", GetLastError());
		VirtualFreeEx(procInfo.hProcess, pTargetBase, 0, MEM_RELEASE);
		return false;
	}

	if (!WriteProcessMemory(procInfo.hProcess, MappingDataAlloc, &data, sizeof(MANUAL_MAPPING_DATA), nullptr)) {
		ILog("Can't write mapping 0x%X\n", GetLastError());
		VirtualFreeEx(procInfo.hProcess, pTargetBase, 0, MEM_RELEASE);
		VirtualFreeEx(procInfo.hProcess, MappingDataAlloc, 0, MEM_RELEASE);
		return false;
	}

	//Shell code
	void* pShellcode = VirtualAllocEx(procInfo.hProcess, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!pShellcode) {
		ILog("Memory shellcode allocation failed (ex) 0x%X\n", GetLastError());
		VirtualFreeEx(procInfo.hProcess, pTargetBase, 0, MEM_RELEASE);
		VirtualFreeEx(procInfo.hProcess, MappingDataAlloc, 0, MEM_RELEASE);
		return false;
	}

	if (!WriteProcessMemory(procInfo.hProcess, pShellcode, Shellcode, 0x1000, nullptr)) {
		ILog("Can't write shellcode 0x%X\n", GetLastError());
		VirtualFreeEx(procInfo.hProcess, pTargetBase, 0, MEM_RELEASE);
		VirtualFreeEx(procInfo.hProcess, MappingDataAlloc, 0, MEM_RELEASE);
		VirtualFreeEx(procInfo.hProcess, pShellcode, 0, MEM_RELEASE);
		return false;
	}

	ILog("Mapped DLL at %p\n", pTargetBase);
	ILog("Mapping info at %p\n", MappingDataAlloc);
	ILog("Shell code at %p\n", pShellcode);

	ILog("Data allocated\n");

#ifdef _DEBUG
	ILog("My shellcode pointer %p\n", Shellcode);
	ILog("Target point %p\n", pShellcode);
	system("pause");
#endif

	HANDLE hThread = CreateRemoteThread(procInfo.hProcess, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(pShellcode), MappingDataAlloc, 0, nullptr);
	if (!hThread) {
		ILog("Thread creation failed 0x%X\n", GetLastError());
		VirtualFreeEx(procInfo.hProcess, pTargetBase, 0, MEM_RELEASE);
		VirtualFreeEx(procInfo.hProcess, MappingDataAlloc, 0, MEM_RELEASE);
		VirtualFreeEx(procInfo.hProcess, pShellcode, 0, MEM_RELEASE);
		return false;
	}
	CloseHandle(hThread);

	ILog("Thread created at: %p, waiting for return...\n", pShellcode);

	HINSTANCE hCheck = NULL;
	while (!hCheck) {
		DWORD exitcode = 0;
		GetExitCodeProcess(procInfo.hProcess, &exitcode);
		if (exitcode != STILL_ACTIVE) {
			ILog("Process crashed, exit code: %d\n", exitcode);
			return false;
		}

		MANUAL_MAPPING_DATA data_checked{ 0 };
		ReadProcessMemory(procInfo.hProcess, MappingDataAlloc, &data_checked, sizeof(data_checked), nullptr);
		hCheck = data_checked.hMod;

		if (hCheck == (HINSTANCE)0x404040) {
			ILog("Wrong mapping ptr\n");
			VirtualFreeEx(procInfo.hProcess, pTargetBase, 0, MEM_RELEASE);
			VirtualFreeEx(procInfo.hProcess, MappingDataAlloc, 0, MEM_RELEASE);
			VirtualFreeEx(procInfo.hProcess, pShellcode, 0, MEM_RELEASE);
			return false;
		}
		else if (hCheck == (HINSTANCE)0x505050) {
			ILog("WARNING: Exception support failed!\n");
		}

		Sleep(10);
	}

	BYTE* emptyBuffer = (BYTE*)malloc(1024 * 1024 * 20);
	if (emptyBuffer == nullptr) {
		ILog("Unable to allocate memory\n");
		return false;
	}
	memset(emptyBuffer, 0, 1024 * 1024 * 20);

	//CLEAR PE HEAD
	if (ClearHeader) {
		if (!WriteProcessMemory(procInfo.hProcess, pTargetBase, emptyBuffer, 0x1000, nullptr)) {
			ILog("WARNING!: Can't clear HEADER\n");
		}
	}
	//END CLEAR PE HEAD


	if (ClearNonNeededSections) {
		pSectionHeader = IMAGE_FIRST_SECTION(pOldNtHeader);
		for (UINT i = 0; i != pOldFileHeader->NumberOfSections; ++i, ++pSectionHeader) {
			if (pSectionHeader->Misc.VirtualSize) {
				if ((SEHExceptionSupport ? 0 : strcmp((char*)pSectionHeader->Name, ".pdata") == 0) ||
					strcmp((char*)pSectionHeader->Name, ".rsrc") == 0 ||
					strcmp((char*)pSectionHeader->Name, ".reloc") == 0) {
					ILog("Processing %s removal\n", pSectionHeader->Name);
					if (!WriteProcessMemory(procInfo.hProcess, pTargetBase + pSectionHeader->VirtualAddress, emptyBuffer, pSectionHeader->Misc.VirtualSize, nullptr)) {
						ILog("Can't clear section %s: 0x%x\n", pSectionHeader->Name, GetLastError());
					}
				}
			}
		}
	}

	if (AdjustProtections) {
		pSectionHeader = IMAGE_FIRST_SECTION(pOldNtHeader);
		for (UINT i = 0; i != pOldFileHeader->NumberOfSections; ++i, ++pSectionHeader) {
			if (pSectionHeader->Misc.VirtualSize) {
				DWORD old = 0;
				DWORD newP = PAGE_READONLY;

				if ((pSectionHeader->Characteristics & IMAGE_SCN_MEM_WRITE) > 0) {
					newP = PAGE_READWRITE;
				}
				else if ((pSectionHeader->Characteristics & IMAGE_SCN_MEM_EXECUTE) > 0) {
					newP = PAGE_EXECUTE_READ;
				}
				if (VirtualProtectEx(procInfo.hProcess, pTargetBase + pSectionHeader->VirtualAddress, pSectionHeader->Misc.VirtualSize, newP, &old)) {
					ILog("section %s set as %lX\n", (char*)pSectionHeader->Name, newP);
				}
				else {
					ILog("FAIL: section %s not set as %lX\n", (char*)pSectionHeader->Name, newP);
				}
			}
		}
		DWORD old = 0;
		VirtualProtectEx(procInfo.hProcess, pTargetBase, IMAGE_FIRST_SECTION(pOldNtHeader)->VirtualAddress, PAGE_READONLY, &old);
	}

	if (!WriteProcessMemory(procInfo.hProcess, pShellcode, emptyBuffer, 0x1000, nullptr)) {
		ILog("WARNING: Can't clear shellcode\n");
	}
	if (!VirtualFreeEx(procInfo.hProcess, pShellcode, 0, MEM_RELEASE)) {
		ILog("WARNING: can't release shell code memory\n");
	}
	if (!VirtualFreeEx(procInfo.hProcess, MappingDataAlloc, 0, MEM_RELEASE)) {
		ILog("WARNING: can't release mapping data memory\n");
	}

	return true;
}