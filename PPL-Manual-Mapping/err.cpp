#include <err.h>
#include <Windows.h>
#include <string>

namespace err {
    int error() {
        MessageBoxA(NULL, (std::string("Code: ") + std::to_string(GetLastError())).c_str(), "Error", MB_OK);
        Sleep(1000);
        return 0;
    }
}