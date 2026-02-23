#include "Entry/Entry.h"
#include <thread>

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	static bool s_bAttached = false;

	if ((fdwReason == DLL_PROCESS_ATTACH) && !s_bAttached)
	{
		s_bAttached = true;

		// 在独立线程中执行初始化，避免 loader lock 死锁
		// DllMain 中不能执行阻塞操作（如等待其他模块加载），
		// 否则会导致死锁：DLL 加载持有 loader lock，
		// 而我们等待的 serverbrowser.dll 也需要 loader lock 才能加载
		std::thread([]() {
			G::ModuleEntry.Load();
		}).detach();
	}

	return TRUE;
}