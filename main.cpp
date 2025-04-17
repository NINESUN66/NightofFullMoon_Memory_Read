#include <iostream>
#include <windows.h>
#include <TlHelp32.h> // 用于 GetModuleBaseAddress 的替代方案
#include <vector>
#include <string> // 用于 wstring
#include <iomanip> // 用于 std::hex 等

// 结构体：存储玩家状态
struct PlayerStats {
	int maxHP = -1;
	int currentHP = -1;
	int playerClass = -1;
	int mana = -1;
	int experience = -1;
	int money = -1;
	int cardDraws = -1;
	int level = -1;
	int actionPoints = -1;
};


// 函数：打印上一个 Windows API 调用的错误信息
void PrintLastError() {
	DWORD errorMessageID = GetLastError();
	if (errorMessageID == 0) {
		return; // 没有错误信息
	}

	LPSTR messageBuffer = nullptr;
	size_t size = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

	// 尝试用 GBK 解码系统返回的错误信息（如果控制台是 GBK）
	// 如果你的控制台是 UTF-8, 可能需要转换
	std::cout << "错误代码 " << errorMessageID << ": " << messageBuffer << std::endl;


	LocalFree(messageBuffer);
}

// 函数：获取指定进程中模块的基地址 (64位兼容)
uintptr_t GetModuleBaseAddress(DWORD procId, const wchar_t* modName) {
	uintptr_t modBaseAddr = 0;
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
	if (hSnap != INVALID_HANDLE_VALUE) {
		MODULEENTRY32 modEntry;
		modEntry.dwSize = sizeof(modEntry);
		if (Module32First(hSnap, &modEntry)) {
			do {
				// 比较模块名（不区分大小写）
				if (_wcsicmp(modEntry.szModule, modName) == 0) {
					modBaseAddr = (uintptr_t)modEntry.modBaseAddr;
					break;
				}
			} while (Module32Next(hSnap, &modEntry));
		}
		CloseHandle(hSnap);
	}
	else {
		std::cout << "CreateToolhelp32Snapshot 失败: ";
		PrintLastError();
	}
	return modBaseAddr;
}

// 辅助函数：读取指定地址的指针值 (64位)
bool ReadPointerValue(HANDLE process, uintptr_t address, uintptr_t& value) {
	SIZE_T bytesRead = 0;
	if (ReadProcessMemory(process, (LPCVOID)address, &value, sizeof(uintptr_t), &bytesRead) && bytesRead == sizeof(uintptr_t)) {
		return true;
	}
	// 在 getPlayerStats 中会处理具体错误，这里可以不打印
	// std::cerr << "ReadProcessMemory 读取指针失败，地址: 0x" << std::hex << address << ". ";
	// PrintLastError();
	return false;
}

// 函数：获取玩家所有状态信息
bool getPlayerStats(HANDLE& process, uintptr_t gameAssemblyBase, PlayerStats& stats) {
	// 根据 CE 图片的指针链:
	// "GameAssembly.dll"+018C9098 -> +40 -> +B8 -> +10 -> +20 -> baseAddrForStats
	uintptr_t basePtrAddr = gameAssemblyBase + 0x018C9098;
	uintptr_t currentAddr = 0; // 这将是进行最后偏移计算的基础地址

	// --- 指针链遍历 ---
	if (!ReadPointerValue(process, basePtrAddr, currentAddr)) {
		std::cerr << "错误：读取基指针失败 (GameAssembly.dll + 0x" << std::hex << 0x018C9098 << ")" << std::endl; return false;
	}
	currentAddr += 0x40;
	if (!ReadPointerValue(process, currentAddr, currentAddr)) {
		std::cerr << "错误：读取指针链偏移 +0x40 失败" << std::endl; return false;
	}
	currentAddr += 0xB8;
	if (!ReadPointerValue(process, currentAddr, currentAddr)) {
		std::cerr << "错误：读取指针链偏移 +0xB8 失败" << std::endl; return false;
	}
	currentAddr += 0x10;
	if (!ReadPointerValue(process, currentAddr, currentAddr)) {
		std::cerr << "错误：读取指针链偏移 +0x10 失败" << std::endl; return false;
	}
	currentAddr += 0x20;
	if (!ReadPointerValue(process, currentAddr, currentAddr)) { // 读取最后一个指针，得到基地址
		std::cerr << "错误：读取指针链偏移 +0x20 失败（获取最终偏移基础地址失败）。" << std::endl; return false;
	}
	// --- 指针链遍历结束 ---

	// currentAddr 现在是应用最终偏移量的基础地址 (即上面分析中的 Address5)
	uintptr_t finalAddr = 0;
	SIZE_T bytesRead = 0;
	bool all_success = true; // 追踪是否所有读取都成功

	// 读取各个属性值 (使用临时变量以防结构体未完全填充)
	int tempVal;

	// 血上限 (Max HP): 最终偏移 0x18
	finalAddr = currentAddr + 0x18;
	if (ReadProcessMemory(process, (LPCVOID)finalAddr, &tempVal, sizeof(int), &bytesRead) && bytesRead == sizeof(int)) { stats.maxHP = tempVal; }
	else { std::cerr << "读取 Max HP 失败 (偏移 0x18), 地址: 0x" << std::hex << finalAddr << std::endl; all_success = false; stats.maxHP = -1; }

	// 血量 (Current HP): 最终偏移 0x1C
	finalAddr = currentAddr + 0x1C;
	if (ReadProcessMemory(process, (LPCVOID)finalAddr, &tempVal, sizeof(int), &bytesRead) && bytesRead == sizeof(int)) { stats.currentHP = tempVal; }
	else { std::cerr << "读取 Current HP 失败 (偏移 0x1C), 地址: 0x" << std::hex << finalAddr << std::endl; all_success = false; stats.currentHP = -1; }

	// 职业 (Class): 最终偏移 0x10
	finalAddr = currentAddr + 0x10;
	if (ReadProcessMemory(process, (LPCVOID)finalAddr, &tempVal, sizeof(int), &bytesRead) && bytesRead == sizeof(int)) { stats.playerClass = tempVal; }
	else { std::cerr << "读取 Class 失败 (偏移 0x10), 地址: 0x" << std::hex << finalAddr << std::endl; all_success = false; stats.playerClass = -1; }

	// 蓝 (Mana): 最终偏移 0x20
	finalAddr = currentAddr + 0x20;
	if (ReadProcessMemory(process, (LPCVOID)finalAddr, &tempVal, sizeof(int), &bytesRead) && bytesRead == sizeof(int)) { stats.mana = tempVal; }
	else { std::cerr << "读取 Mana 失败 (偏移 0x20), 地址: 0x" << std::hex << finalAddr << std::endl; all_success = false; stats.mana = -1; }

	// 经验 (Experience): 最终偏移 0x24
	finalAddr = currentAddr + 0x24;
	if (ReadProcessMemory(process, (LPCVOID)finalAddr, &tempVal, sizeof(int), &bytesRead) && bytesRead == sizeof(int)) { stats.experience = tempVal; }
	else { std::cerr << "读取 Experience 失败 (偏移 0x24), 地址: 0x" << std::hex << finalAddr << std::endl; all_success = false; stats.experience = -1; }

	// 钱 (Money): 最终偏移 0x2C
	finalAddr = currentAddr + 0x2C;
	if (ReadProcessMemory(process, (LPCVOID)finalAddr, &tempVal, sizeof(int), &bytesRead) && bytesRead == sizeof(int)) { stats.money = tempVal; }
	else { std::cerr << "读取 Money 失败 (偏移 0x2C), 地址: 0x" << std::hex << finalAddr << std::endl; all_success = false; stats.money = -1; }

	// 抽卡数 (Card Draws): 最终偏移 0x30
	finalAddr = currentAddr + 0x30;
	if (ReadProcessMemory(process, (LPCVOID)finalAddr, &tempVal, sizeof(int), &bytesRead) && bytesRead == sizeof(int)) { stats.cardDraws = tempVal; }
	else { std::cerr << "读取 Card Draws 失败 (偏移 0x30), 地址: 0x" << std::hex << finalAddr << std::endl; all_success = false; stats.cardDraws = -1; }

	// 等级 (Level): 最终偏移 0x34
	finalAddr = currentAddr + 0x34;
	if (ReadProcessMemory(process, (LPCVOID)finalAddr, &tempVal, sizeof(int), &bytesRead) && bytesRead == sizeof(int)) { stats.level = tempVal; }
	else { std::cerr << "读取 Level 失败 (偏移 0x34), 地址: 0x" << std::hex << finalAddr << std::endl; all_success = false; stats.level = -1; }

	// 行动 (Actions): 最终偏移 0x38
	finalAddr = currentAddr + 0x38;
	if (ReadProcessMemory(process, (LPCVOID)finalAddr, &tempVal, sizeof(int), &bytesRead) && bytesRead == sizeof(int)) { stats.actionPoints = tempVal; }
	else { std::cerr << "读取 Action Points 失败 (偏移 0x38), 地址: 0x" << std::hex << finalAddr << std::endl; all_success = false; stats.actionPoints = -1; }

	// 如果有任何一次读取失败，打印最后的系统错误码
	if (!all_success) {
		PrintLastError();
	}

	// 即使部分失败，也返回 true，让 main 函数可以打印已成功读取的部分
	// 如果要求必须全部成功才算成功，则返回 all_success
	return true;
}


int main()
{
	// SetConsoleOutputCP(CP_UTF8); // 设置控制台输出为 UTF-8
	setvbuf(stdout, nullptr, _IOFBF, 1024);

	HWND hwnd = NULL;
	DWORD pid = 0;
	HANDLE process = NULL;
	uintptr_t gameAssemblyBase = 0;

	const wchar_t* windowClassName = L"UnityWndClass";
	const wchar_t* windowTitle = L"月圆之夜"; // 请确保窗口标题完全匹配
	const wchar_t* moduleName = L"GameAssembly.dll";

	std::cout << "正在查找游戏窗口..." << std::endl;

	while (true) {
		hwnd = FindWindow(windowClassName, windowTitle);
		if (hwnd) {
			std::cout << "找到窗口了" << std::endl;
			GetWindowThreadProcessId(hwnd, &pid);
			if (pid == 0) {
				std::cout << "获取进程ID失败" << std::endl; Sleep(1000); continue;
			}
			std::cout << "进程ID: " << pid << std::endl;

			process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION, FALSE, pid);
			if (process == NULL) {
				std::cout << "打开进程失败，尝试以管理员权限运行？"; PrintLastError(); Sleep(2000); continue;
			}
			std::cout << "进程句柄获取成功: 0x" << std::hex << (uintptr_t)process << std::endl;

			gameAssemblyBase = GetModuleBaseAddress(pid, moduleName);
			if (gameAssemblyBase == 0) {
				std::cout << "获取 " << moduleName << " 基址失败，等待模块加载..." << std::endl;
				CloseHandle(process); process = NULL; Sleep(2000); continue;
			}
			std::cout << moduleName << " 基址: 0x" << std::hex << gameAssemblyBase << std::endl;
			std::cout << "初始化完成，准备读取状态..." << std::endl;
			Sleep(1000);
			break;
		}
		else {
			// std::cout << "没有找到游戏窗口 \"" << windowTitle << "\"" << std::endl; // 减少重复输出
			Sleep(2000);
		}
	}

	if (process != NULL) {
		PlayerStats currentStats;

		while (true) {
			DWORD exitCode;
			if (!GetExitCodeProcess(process, &exitCode) || exitCode != STILL_ACTIVE) {
				std::cout << "游戏进程已关闭。" << std::endl; break;
			}

			if (getPlayerStats(process, gameAssemblyBase, currentStats)) {
				std::cout << "\n------ 玩家状态 (" << std::dec << GetTickCount() << ") ------" << std::endl; // 加个时间戳
				std::cout << "血量:       " << std::dec << currentStats.currentHP << "/" << currentStats.maxHP << std::endl;
				std::cout << "职业:       " << std::dec << currentStats.playerClass << std::endl;
				std::cout << "蓝量:       " << std::dec << currentStats.mana << std::endl;
				std::cout << "经验:       " << std::dec << currentStats.experience << std::endl;
				std::cout << "金钱:       " << std::dec << currentStats.money << std::endl;
				std::cout << "抽卡数:     " << std::dec << currentStats.cardDraws << std::endl;
				std::cout << "等级:       " << std::dec << currentStats.level << std::endl;
				std::cout << "行动点:     " << std::dec << currentStats.actionPoints << std::endl;
				std::cout << "-----------------------------\n" << std::endl;
			}
			else {
				std::cout << "读取玩家状态失败。" << std::endl;
				// 如果 getPlayerStats 返回 false，可能需要重新获取基址或退出
				// gameAssemblyBase = GetModuleBaseAddress(pid, moduleName); // 尝试重新获取基址
				// Sleep(1000);
			}
			Sleep(2000); // 每 2 秒读取一次
		}
		CloseHandle(process);
	}
	else {
		std::cerr << "未能成功获取进程句柄，程序退出。" << std::endl;
	}

	std::cout << "按 Enter 键退出..." << std::endl;
	std::cin.clear(); // 清除可能的错误状态
	// std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // 清空输入缓冲区
	std::cin.get(); // 等待回车

	return 0;
}