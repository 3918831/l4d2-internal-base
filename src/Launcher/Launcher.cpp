// L4D2 Launcher - 自动加载DLL的启动器
//
// 使用方法：
// 1. 将编译后的Launcher.exe放到游戏目录
// 2. 将Lak3_l4d2_hack.dll放到同一目录
// 3. 运行Launcher.exe即可

#include <windows.h>
#include <iostream>
#include <string>
#include <TlHelp32.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

// 查找进程ID
DWORD FindProcessId(const char* processName)
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(hSnapshot, &pe32))
    {
        CloseHandle(hSnapshot);
        return 0;
    }

    DWORD pid = 0;
    do
    {
        if (_stricmp(pe32.szExeFile, processName) == 0)
        {
            pid = pe32.th32ProcessID;
            break;
        }
    } while (Process32Next(hSnapshot, &pe32));

    CloseHandle(hSnapshot);
    return pid;
}

// 注入DLL到进程
bool InjectDLL(DWORD processId, const char* dllPath)
{
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProcess)
    {
        std::cerr << "Failed to open process. Error: " << GetLastError() << std::endl;
        return false;
    }

    // 在目标进程中分配内存
    size_t pathLen = strlen(dllPath) + 1;
    LPVOID pRemoteMem = VirtualAllocEx(hProcess, nullptr, pathLen, MEM_COMMIT, PAGE_READWRITE);
    if (!pRemoteMem)
    {
        std::cerr << "Failed to allocate memory in target process." << std::endl;
        CloseHandle(hProcess);
        return false;
    }

    // 写入DLL路径
    if (!WriteProcessMemory(hProcess, pRemoteMem, dllPath, pathLen, nullptr))
    {
        std::cerr << "Failed to write to process memory." << std::endl;
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // 获取LoadLibraryA地址
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    LPVOID pLoadLibrary = (LPVOID)GetProcAddress(hKernel32, "LoadLibraryA");
    if (!pLoadLibrary)
    {
        std::cerr << "Failed to get LoadLibraryA address." << std::endl;
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // 创建远程线程
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
        (LPTHREAD_START_ROUTINE)pLoadLibrary, pRemoteMem, 0, nullptr);
    if (!hThread)
    {
        std::cerr << "Failed to create remote thread." << std::endl;
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // 等待线程完成
    WaitForSingleObject(hThread, INFINITE);

    // 清理
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    return true;
}

// 启动游戏进程
PROCESS_INFORMATION LaunchGame(const char* gameExe, const char* commandLine)
{
    PROCESS_INFORMATION pi = { 0 };
    STARTUPINFOA si = { 0 };
    si.cb = sizeof(si);

    char cmdLine[MAX_PATH * 2];
    sprintf_s(cmdLine, "\"%s\" %s", gameExe, commandLine ? commandLine : "");

    if (!CreateProcessA(nullptr, cmdLine, nullptr, nullptr, FALSE,
        CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi))
    {
        std::cerr << "Failed to launch game. Error: " << GetLastError() << std::endl;
    }

    return pi;
}

int main(int argc, char* argv[])
{
    std::cout << "========================================" << std::endl;
    std::cout << "L4D2 Internal Base - Launcher" << std::endl;
    std::cout << "========================================" << std::endl;

    // 获取当前目录（游戏目录）
    char gameDir[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, gameDir);

    // DLL路径
    char dllPath[MAX_PATH];
    sprintf_s(dllPath, "%s\\L4D2_Portal.dll", gameDir);

    // 检查DLL是否存在
    if (GetFileAttributesA(dllPath) == INVALID_FILE_ATTRIBUTES)
    {
        std::cerr << "Error: L4D2_Portal.dll not found!" << std::endl;
        std::cerr << "Please make sure the DLL is in the same directory as Launcher.exe" << std::endl;
        std::cout << "\nPress any key to exit..." << std::endl;
        getchar();
        return 1;
    }

    // 游戏可执行文件路径
    char gameExe[MAX_PATH];
    sprintf_s(gameExe, "%s\\left4dead2.exe", gameDir);

    // 检查游戏是否存在
    if (GetFileAttributesA(gameExe) == INVALID_FILE_ATTRIBUTES)
    {
        std::cerr << "Error: left4dead2.exe not found!" << std::endl;
        std::cerr << "Please make sure Launcher.exe is in the L4D2 game directory" << std::endl;
        std::cout << "\nPress any key to exit..." << std::endl;
        getchar();
        return 1;
    }

    // 检查游戏是否已运行
    DWORD existingPid = FindProcessId("left4dead2.exe");
    if (existingPid != 0)
    {
        std::cout << "\nL4D2 is already running. Injecting DLL..." << std::endl;

        if (InjectDLL(existingPid, dllPath))
        {
            std::cout << "DLL injected successfully!" << std::endl;
        }
        else
        {
            std::cerr << "Failed to inject DLL!" << std::endl;
            std::cout << "\nPress any key to exit..." << std::endl;
            getchar();
            return 1;
        }
    }
    else
    {
        // 启动游戏
        std::cout << "\nLaunching L4D2..." << std::endl;

        // 构建命令行参数（可以添加额外参数，如 -insecure, -console 等）
        const char* cmdArgs = "-insecure -steam -novid";

        PROCESS_INFORMATION pi = LaunchGame(gameExe, cmdArgs);
        if (pi.hProcess == nullptr)
        {
            std::cerr << "Failed to launch game!" << std::endl;
            std::cout << "\nPress any key to exit..." << std::endl;
            getchar();
            return 1;
        }

        std::cout << "Game launched. Waiting for initialization..." << std::endl;

        // 等待进程初始化（约10秒后注入）
        Sleep(10000);

        // 注入DLL
        std::cout << "Injecting DLL..." << std::endl;
        if (InjectDLL(pi.dwProcessId, dllPath))
        {
            std::cout << "DLL injected successfully!" << std::endl;
        }
        else
        {
            std::cerr << "Failed to inject DLL!" << std::endl;
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            std::cout << "\nPress any key to exit..." << std::endl;
            getchar();
            return 1;
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    std::cout << "\nDone! You can now close this window." << std::endl;
    std::cout << "Press any key to exit..." << std::endl;
    getchar();

    return 0;
}
