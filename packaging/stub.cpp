#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdint.h>

#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

namespace {

constexpr char kMagic[8] = { 'P', 'R', 'I', 'S', 'M', 'P', 'K', 'G' };

struct Footer {
    uint64_t zipSize = 0;
    uint64_t stamp = 0;
    char magic[8] = {};
};

std::wstring localAppDir()
{
    wchar_t base[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base)))
        return {};
    return std::wstring(base) + L"\\PrismServer\\app";
}

bool readStamp(const std::wstring& path, uint64_t* stamp)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD read = 0;
    const BOOL ok = ReadFile(file, stamp, sizeof(*stamp), &read, nullptr);
    CloseHandle(file);
    return ok && read == sizeof(*stamp);
}

bool writeStamp(const std::wstring& path, uint64_t stamp)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, &stamp, sizeof(stamp), &written, nullptr);
    CloseHandle(file);
    return ok && written == sizeof(stamp);
}

void removeTree(const std::wstring& path)
{
    std::wstring from = path;
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&op);
}

bool extractZip(const std::wstring& tar, const std::wstring& zip, const std::wstring& dest)
{
    std::wstring command = L"\"" + tar + L"\" -xf \"" + zip + L"\" -C \"" + dest + L"\"";
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(tar.c_str(), buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        return false;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code == 0;
}

HWND showStatus(HINSTANCE instance)
{
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"PrismServerPack";
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&windowClass);

    const wchar_t* text = L"Preparing PrismServer…";
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, windowClass.lpszClassName, L"PrismServer", WS_POPUP | WS_BORDER, CW_USEDEFAULT,
                                   CW_USEDEFAULT, 320, 72, nullptr, nullptr, instance, nullptr);
    if (!window)
        return nullptr;
    RECT desktop = {};
    GetWindowRect(GetDesktopWindow(), &desktop);
    MoveWindow(window, (desktop.right - 320) / 2, (desktop.bottom - 72) / 2, 320, 72, FALSE);
    CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 24, 300, 24, window, nullptr, instance, nullptr);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    return window;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    wchar_t selfPath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, selfPath, MAX_PATH))
        return 1;

    HANDLE self = CreateFileW(selfPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (self == INVALID_HANDLE_VALUE)
        return 1;

    LARGE_INTEGER fileSize = {};
    GetFileSizeEx(self, &fileSize);
    if (fileSize.QuadPart < static_cast<LONGLONG>(sizeof(Footer))) {
        CloseHandle(self);
        return 1;
    }

    LARGE_INTEGER footerPos = {};
    footerPos.QuadPart = fileSize.QuadPart - static_cast<LONGLONG>(sizeof(Footer));
    SetFilePointerEx(self, footerPos, nullptr, FILE_BEGIN);
    Footer footer = {};
    DWORD read = 0;
    ReadFile(self, &footer, sizeof(footer), &read, nullptr);
    if (read != sizeof(footer) || memcmp(footer.magic, kMagic, sizeof(kMagic)) != 0 || footer.zipSize == 0 ||
        footerPos.QuadPart < static_cast<LONGLONG>(footer.zipSize)) {
        CloseHandle(self);
        MessageBoxW(nullptr, L"This PrismServer file is damaged.", L"PrismServer", MB_ICONERROR);
        return 1;
    }

    const std::wstring dest = localAppDir();
    if (dest.empty()) {
        CloseHandle(self);
        MessageBoxW(nullptr, L"Could not find a place to set up PrismServer.", L"PrismServer", MB_ICONERROR);
        return 1;
    }

    const std::wstring stampPath = dest + L"\\installed.stamp";
    uint64_t installed = 0;
    const bool current = readStamp(stampPath, &installed) && installed == footer.stamp;
    if (!current) {
        HWND status = showStatus(instance);
        wchar_t tempDir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempDir);
        const std::wstring zipPath = std::wstring(tempDir) + L"PrismServer-payload.zip";
        HANDLE zip = CreateFileW(zipPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (zip == INVALID_HANDLE_VALUE) {
            CloseHandle(self);
            MessageBoxW(nullptr, L"Could not prepare PrismServer.", L"PrismServer", MB_ICONERROR);
            return 1;
        }

        LARGE_INTEGER zipPos = {};
        zipPos.QuadPart = footerPos.QuadPart - static_cast<LONGLONG>(footer.zipSize);
        SetFilePointerEx(self, zipPos, nullptr, FILE_BEGIN);
        std::vector<char> buffer(1024 * 1024);
        uint64_t remaining = footer.zipSize;
        bool wroteZip = true;
        while (remaining > 0) {
            const DWORD chunk = static_cast<DWORD>(remaining > buffer.size() ? buffer.size() : remaining);
            DWORD got = 0;
            if (!ReadFile(self, buffer.data(), chunk, &got, nullptr) || got == 0) {
                wroteZip = false;
                break;
            }
            DWORD written = 0;
            if (!WriteFile(zip, buffer.data(), got, &written, nullptr) || written != got) {
                wroteZip = false;
                break;
            }
            remaining -= got;
        }
        CloseHandle(zip);
        CloseHandle(self);
        self = INVALID_HANDLE_VALUE;

        SHCreateDirectoryExW(nullptr, dest.c_str(), nullptr);
        removeTree(dest);
        SHCreateDirectoryExW(nullptr, dest.c_str(), nullptr);

        wchar_t systemDir[MAX_PATH] = {};
        GetSystemDirectoryW(systemDir, MAX_PATH);
        const std::wstring tar = std::wstring(systemDir) + L"\\tar.exe";
        const bool extracted = wroteZip && extractZip(tar, zipPath, dest);
        DeleteFileW(zipPath.c_str());
        if (status)
            DestroyWindow(status);
        if (!extracted || !writeStamp(stampPath, footer.stamp)) {
            MessageBoxW(nullptr, L"Could not set up PrismServer.", L"PrismServer", MB_ICONERROR);
            return 1;
        }
    } else {
        CloseHandle(self);
    }

    const std::wstring exe = dest + L"\\prismlauncher.exe";
    std::wstring command = L"\"" + exe + L"\"";
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(exe.c_str(), commandBuffer.data(), nullptr, nullptr, FALSE, 0, nullptr, dest.c_str(), &startup, &process)) {
        MessageBoxW(nullptr, L"Could not start PrismServer.", L"PrismServer", MB_ICONERROR);
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
