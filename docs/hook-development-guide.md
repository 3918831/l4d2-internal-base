# Hook 开发指南

本文档描述如何在 L4D2 Internal Base 项目中添加新的游戏 Hook。

---

## 目录

- [概述](#概述)
- [Hook 类型](#hook-类型)
- [目录结构](#目录结构)
- [创建新 Hook 的步骤](#创建新-hook-的步骤)
- [编码规范](#编码规范)
- [初始化流程](#初始化流程)
- [常见问题](#常见问题)

---

## 概述

本项目使用三种 Hook 类型：

| 类型 | 用途 | 实现方式 |
|------|------|----------|
| `CFunction` | 内联函数 Hook | MinHook 库 |
| `CVMTable` | 虚方法表 Hook | VMT 复制 + 重定向 |
| `CTable` | 索引式 VMT Hook | 直接索引操作 |

**大部分接口 Hook 使用 `CTable` 类型**（VMT 表索引方式）。

---

## Hook 类型

### CTable (VMT 索引 Hook)

用于 Hook 虚函数表的指定索引位置：

```cpp
inline Hook::CTable Table;

// 初始化 VMT 表
Table.Init(I::InterfacePointer);

// Hook 指定索引
Table.Hook(&DetourFunction, Index);

// 调用原始函数
Table.Original<FN>(Index)(args...);
```

---

## 目录结构

```
src/
├── Hooks/
│   ├── YourHook/              # 新建 Hook 目录
│   │   ├── YourHook.h         # 头文件
│   │   └── YourHook.cpp       # 实现文件
│   ├── Hooks.h                # Hook 总头文件（需添加 include）
│   └── Hooks.cpp              # Hook 初始化（需添加 Init 调用）
├── SDK/
│   └── L4D2/
│       └── Interfaces/        # 接口定义
└── l4d2_base.vcxproj          # 项目文件（需添加文件引用）
```

---

## 创建新 Hook 的步骤

### 步骤 1: 确定接口和函数索引

在 `SDK/L4D2/Interfaces/` 目录下找到目标接口定义，确认：
- 函数签名
- VMT 索引位置（从 0 开始计数）

例如 `IVEngineServer.h`:

```cpp
class IVEngineServer
{
public:
    virtual void Function0() = 0;  // Index 0
    virtual void Function1() = 0;  // Index 1
    // ...
    virtual int GetArea(const Vector& origin) = 0;  // Index 65
};
```

### 步骤 2: 创建 Hook 目录

```bash
mkdir -p src/Hooks/YourHook
```

### 步骤 3: 编写头文件 (YourHook.h)

```cpp
#pragma once

#include "../../SDK/SDK.h"

namespace Hooks
{
	namespace YourHook
	{
		inline Hook::CTable Table;

		namespace TargetFunction
		{
			// 函数签名必须与原始函数匹配
			// virtual int TargetFunction(const Vector& origin) = 0; // Index X
			using FN = int(__fastcall*)(void*, void*, const Vector&);
			constexpr uint32_t Index = X;

			int __fastcall Detour(void* ecx, void* edx, const Vector& origin);
		}

		void Init();
	}
}
```

**头文件模板说明：**

| 元素 | 说明 |
|------|------|
| `#pragma once` | 防止头文件重复包含 |
| `namespace Hooks::YourHook` | Hook 命名空间，避免冲突 |
| `inline Hook::CTable Table` | VMT 表对象 |
| `namespace TargetFunction` | 目标函数命名空间（建议使用函数名） |
| `using FN` | 函数指针类型定义 |
| `constexpr uint32_t Index` | VMT 索引常量 |
| `Detour` | Hook 函数（fastcall 调用约定） |
| `Init()` | 初始化函数 |

### 步骤 4: 编写实现文件 (YourHook.cpp)

```cpp
#include "YourHook.h"

#include <iostream>
#include "../../SDK/L4D2/Interfaces/YourInterface.h"

using namespace Hooks;

// Detour 函数实现
int __fastcall YourHook::TargetFunction::Detour(void* ecx, void* edx, const Vector& origin)
{
	// 参数验证（可选，仅在异常时输出日志）
	if (ecx == nullptr)
	{
		std::cerr << "[YourHook::TargetFunction] ERROR: ecx is null!" << std::endl;
		return -1;
	}

	// 调用原始函数
	int result = Table.Original<FN>(Index)(ecx, edx, origin);

	// 自定义逻辑（不要在此添加正常流程的日志输出）
	// ...

	return result;
}

// 初始化函数
void YourHook::Init()
{
	// 验证接口已初始化
	if (I::YourInterface == nullptr)
	{
		std::cerr << "[YourHook::Init] ERROR: Interface is null!" << std::endl;
		return;
	}

	// 初始化 VMT 表
	if (Table.Init(I::YourInterface) == false)
	{
		std::cerr << "[YourHook::Init] ERROR: Failed to initialize VMT table!" << std::endl;
		return;
	}

	// 安装 Hook
	if (Table.Hook(&TargetFunction::Detour, TargetFunction::Index) == false)
	{
		std::cerr << "[YourHook::Init] ERROR: Failed to hook function!" << std::endl;
		return;
	}

	std::cout << "[YourHook::Init] Successfully hooked (Index " << TargetFunction::Index << ")" << std::endl;
}
```

### 步骤 5: 注册 Hook

#### 5.1 修改 `Hooks/Hooks.h`

在 `Hooks.h` 中添加头文件引用：

```cpp
// ... 其他 includes ...
#include "EngineServer/EngineServer.h"
#include "YourHook/YourHook.h"  // 添加这一行

class CGlobal_Hooks
{
	// ...
};
```

#### 5.2 修改 `Hooks/Hooks.cpp`

在 `Hooks.cpp::Init()` 中添加初始化调用：

```cpp
void CGlobal_Hooks::Init()
{
	// ... 其他 Hook 初始化 ...

	//增加对YourHook的Hook
	YourHook::Init();
}
```

### 步骤 6: 更新项目文件

在 `l4d2_base.vcxproj` 中添加文件引用：

```xml
<!-- 在 <ClCompile> 节中添加 -->
<ClCompile Include="Hooks\YourHook\YourHook.cpp" />

<!-- 在 <ClInclude> 节中添加 -->
<ClInclude Include="Hooks\YourHook\YourHook.h" />
```

### 步骤 7: 编译验证

```bash
# 使用 MSBuild 编译
msbuild l4d2_base.sln /p:Configuration=Debug /p:Platform=x86
```

---

## 编码规范

### 命名约定

| 类型 | 规范 | 示例 |
|------|------|------|
| 目录名 | 与接口/功能相关，大驼峰 | `EngineServer`, `ModelRender` |
| 头文件 | 与目录名相同 | `EngineServer.h` |
| 命名空间 | 与目录名相同 | `namespace Hooks::EngineServer` |
| 函数命名空间 | 与目标函数名相同 | `namespace GetArea` |
| Detour 函数 | 固定名为 `Detour` | `GetArea::Detour` |

### 文件格式

- **缩进**: 使用 Tab
- **大括号**: K&R 风格
- **注释**: 函数说明使用 `//`，复杂逻辑添加注释

### 日志格式

```cpp
// 错误日志
std::cerr << "[ClassName::FunctionName] ERROR: Description" << std::endl;

// 成功日志
std::cout << "[ClassName::Init] Successfully hooked (Index X)" << std::endl;
```

### Detour 函数规范

**重要：** Detour 函数中不要添加非异常处理的日志信息。

```cpp
// ❌ 错误示例 - 正常流程中打印日志
int __fastcall YourHook::TargetFunction::Detour(void* ecx, void* edx, const Vector& origin)
{
    // 不要这样做！如果这个函数被高频调用，会严重影响性能
    std::cout << "[Detour] Called with origin: " << origin.x << std::endl;

    int result = Table.Original<FN>(Index)(ecx, edx, origin);
    return result;
}

// ✅ 正确示例 - 只在异常情况下打印
int __fastcall YourHook::TargetFunction::Detour(void* ecx, void* edx, const Vector& origin)
{
    // 仅在异常情况下打印日志
    if (ecx == nullptr)
    {
        std::cerr << "[YourHook::TargetFunction] ERROR: ecx is null!" << std::endl;
        return -1;
    }

    int result = Table.Original<FN>(Index)(ecx, edx, origin);
    return result;
}
```

**原因：**
- 被Hook的函数可能是**高频调用**函数（如每帧调用多次）
- 正常流程的日志输出会**严重影响性能**
- 大量日志会**淹没真正重要的错误信息**
- 如果需要调试，使用**条件日志**或**计数器**（调试后移除）

---

## 初始化流程

### SDK 接口初始化顺序

在 `Entry/Entry.cpp::Initialize()` 中：

```cpp
// 1. 先初始化所有 SDK 接口
I::EngineClient  = U::Interface.Get<IVEngineClient*>("engine.dll", "VEngineClient013");
I::EngineTrace   = U::Interface.Get<IEngineTrace*>("engine.dll", "EngineTraceClient003");
I::EngineServer  = U::Interface.Get<IVEngineServer*>("engine.dll", "VEngineServer022");
// ...

// 2. 再初始化所有 Hook
G::Hooks.Init();
```

### Hook 初始化顺序

在 `Hooks/Hooks.cpp::Init()` 中（顺序可调整）：

```cpp
void CGlobal_Hooks::Init()
{
    MH_Initialize();

    // 基础 Hook
    BaseClient::Init();
    EngineVGui::Init();

    // 功能 Hook
    EngineServer::Init();
    EngineTrace::Init();

    // ...
}
```

---

## 常见问题

### Q1: Detour 函数没有被调用？

**可能原因：**

1. **游戏没有调用该函数** - 最常见原因
   - 验证方法：使用条件计数器（调试后移除）
   ```cpp
   static int callCount = 0;
   if (++callCount <= 10)  // 只记录前10次
       std::cout << "[Detour] Called!" << std::endl;
   ```
   - 解决：尝试 Hook 其他相关函数

2. **VMT 索引错误**
   - 验证方法：检查接口定义，确认索引从 0 开始正确计数

3. **接口未初始化**
   - 验证方法：在 `Init()` 中检查接口指针是否为 nullptr

### Q2: 编译时出现 LNK2019 错误？

**原因：** 新文件没有添加到项目文件 (`.vcxproj`)

**解决：** 按照 [步骤 6](#步骤-6-更新项目文件) 添加文件引用

### Q3: 如何确定函数的 VMT 索引？

从接口定义文件中从 0 开始数起：

```cpp
class IExample
{
    virtual void Func0() = 0;  // Index 0
    virtual void Func1() = 0;  // Index 1
    virtual int  Func2() = 0;  // Index 2  <- 这是你要 Hook 的
    virtual void Func3() = 0;  // Index 3
};
```

### Q4: __fastcall 调用约定的参数？

```cpp
// C++ 成员函数: int MyClass::MyFunction(int a, int b)
// __fastcall Detour:
int __fastcall Detour(void* ecx, void* edx, int a, int b)
//              ^^^^    ^^^^   ^^^^^^^^^^^^^^^^^^^^
//              this    保留   原始函数参数
```

---

## 示例项目

### 完整示例：Hook IVEngineServer::GetArea

**接口定义 (IVEngineServer.h):**
```cpp
class IVEngineServer
{
    // ... 前 65 个虚函数 ...
    virtual int GetArea(const Vector& origin) = 0;  // Index 65
};
```

**Hook 头文件 (EngineServer.h):**
```cpp
#pragma once

#include "../../SDK/SDK.h"

namespace Hooks
{
    namespace EngineServer
    {
        inline Hook::CTable Table;

        namespace GetArea
        {
            using FN = int(__fastcall*)(void*, void*, const Vector&);
            constexpr uint32_t Index = 65u;

            int __fastcall Detour(void* ecx, void* edx, const Vector& origin);
        }

        void Init();
    }
}
```

**Hook 实现 (EngineServer.cpp):**
```cpp
#include "EngineServer.h"

#include <iostream>
#include "../../SDK/L4D2/Interfaces/IVEngineServer.h"

using namespace Hooks;

int __fastcall EngineServer::GetArea::Detour(void* ecx, void* edx, const Vector& origin)
{
    int nArea = Table.Original<FN>(Index)(ecx, edx, origin);
    return nArea;
}

void EngineServer::Init()
{
    if (I::EngineServer == nullptr)
    {
        std::cerr << "[EngineServer::Init] ERROR: I::EngineServer is null!" << std::endl;
        return;
    }

    if (Table.Init(I::EngineServer) == false)
    {
        std::cerr << "[EngineServer::Init] ERROR: Failed to init VMT table!" << std::endl;
        return;
    }

    if (Table.Hook(&GetArea::Detour, GetArea::Index) == false)
    {
        std::cerr << "[EngineServer::Init] ERROR: Failed to hook!" << std::endl;
        return;
    }

    std::cout << "[EngineServer::Init] Successfully hooked (Index 65)" << std::endl;
}
```

---

## 附录

### 相关文件

| 文件 | 说明 |
|------|------|
| `Util/Hook/Hook.h` | Hook 类定义 |
| `Entry/Entry.cpp` | SDK 接口初始化 |
| `Hooks/Hooks.cpp` | Hook 初始化入口 |
| `SDK/L4D2/Interfaces/` | 游戏接口定义 |

### 参考资料

- [dll-auto-loading-implementation.md](dll-auto-loading-implementation.md) - DLL 自动加载实现
- [portal-development-summary.md](portal-development-summary.md) - 传送门功能开发
