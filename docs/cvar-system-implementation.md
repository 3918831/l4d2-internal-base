# L4D2 控制台变量(CVar)系统实现文档

## 目录

1. [概述](#概述)
2. [技术背景](#技术背景)
3. [架构设计](#架构设计)
4. [实现过程](#实现过程)
5. [核心问题与解决方案](#核心问题与解决方案)
6. [使用指南](#使用指南)
7. [后续开发指南](#后续开发指南)
8. [常见问题](#常见问题)

---

## 概述

本文档详细记录了在L4D2内部mod中实现自定义控制台变量(ConVar)和控制台命令(ConCommand)系统的完整过程。该系统允许mod开发者：

- **创建自定义控制台变量**：可在游戏控制台中通过命令行查询和修改
- **创建自定义控制台命令**：可在游戏控制台中执行的函数
- **持久化配置**：支持`FCVAR_ARCHIVE`标志，将cvar保存到配置文件

### 实现效果

```
] test_var
"test_var" = "0"
 client archive
 - Generic test variable for cvar system - can be used for testing

] test_var 42
] test_var
"test_var" = "42"

] test_print
=== Test CVar Values ===
test_var (string) = 42
test_var (int)    = 42
test_var_float    = 1.500000
test_var_bool     = 0
========================
```

---

## 技术背景

### Source Engine CVar系统

Source引擎使用一套完整的控制台变量系统来管理游戏配置：

```
ICvar (VEngineCvar007 interface)
    │
    ├── ConCommandBase (基类)
    │   ├── ConVar (控制台变量)
    │   └── ConCommand (控制台命令)
    │
    └── 全局链表: s_pConCommandBases
```

### 关键接口

**ICvar接口** (`materialsystem.dll`, VEngineCvar007):
```cpp
class ICvar {
public:
    virtual void RegisterConCommand(ConCommandBase *pCommandBase) = 0;
    virtual void UnregisterConCommand(ConCommandBase *pCommandBase) = 0;
    virtual ConCommandBase *FindCommandBase(const char *name) = 0;
    virtual void ConsolePrintf(const char *fmt, ...) = 0;
    // ... 更多方法
};
```

### FCVAR标志

| 标志 | 值 | 说明 |
|------|-----|------|
| `FCVAR_CLIENTDLL` | 0x08 | 客户端DLL cvar |
| `FCVAR_ARCHIVE` | 0x80 | 保存到config.cfg |
| `FCVAR_CHEAT` | 0x400 | 作弊标志 |
| `FCVAR_USERINFO` | 0x10000 | 用户信息 |

---

## ConVar vs ConCommand

### 核心区别

| 维度 | ConVar（控制台变量） | ConCommand（控制台命令） |
|------|---------------------|---------------------|
| **本质** | 存储数据的对象 | 执行函数的对象 |
| **类比** | 像变量 `x = 5` | 像函数 `print()` |
| **用途** | 配置选项、开关、数值参数 | 动作、操作、打印信息 |
| **存储** | 内部存储值（m_fValue, m_nValue, m_pszString） | 存储函数指针（m_pCommandCallback） |
| **控制台行为** | 输入名字 → 显示当前值；输入名字+参数 → 修改值 | 输入名字 → 执行函数；参数由函数内部解析 |
| **代码访问** | `GetFloat()`, `GetInt()`, `GetString()`, `SetValue()` | 通过Dispatch()调用，无法直接"访问" |

### 生命周期对比

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    DLL加载阶段（静态初始化）                      │
└─────────────────────────────────────────────────────────────────────────┘
                         │
         ┌───────────────┴──────────────┐
         │                                │
         ▼                                ▼
    ┌─────────────────┐          ┌──────────────────┐
    │   ConVar构造   │          │  ConCommand构造  │
    │                │          │                │
    │ ConVar对象创建  │          │ CON_COMMAND宏   │
    │ - 分配内存     │          │ - 函数声明     │
    │ - 初始值       │          │ - 对象创建     │
    │ - 添加到链表   │          │ - 添加到链表   │
    └─────────────────┘          └──────────────────┘
         │                                │
         └──────────────┬───────────────────┘
                      │
                      ▼
              所有对象都在 s_pConCommandBases 链表中



┌─────────────────────────────────────────────────────────────────────────┐
│                    DLL初始化阶段（注册）                              │
└─────────────────────────────────────────────────────────────────────────┘
                         │
                      ▼
         ┌─────────────────────────────────┐
         │  ConVar_Register() 遍历链表  │
         │                                │
         │  对每个对象调用 Init()           │
         │                                │
         │  I::Cvar->RegisterConCommand()  │
         │                                │
         └─────────────────────────────────┘
                         │
                      ▼
         ┌─────────────────────────────────────┐
         │  游戏引擎内部建立映射表         │
         │  "名字" → ConVar/ConCommand对象  │
         └─────────────────────────────────────┘



┌─────────────────────────────────────────────────────────────────────────┐
│                    游戏运行阶段（玩家触发）                            │
└─────────────────────────────────────────────────────────────────────────┘

玩家输入: test_var
                         │
                         ▼
            ┌────────────────────────┐
            │  游戏引擎检查:      │
            │  "这是ConVar，      │
            │   返回存储的值"     │
            └────────────────────────┘
                         │
                         ▼
                输出: "test_var" = "42"

玩家输入: test_var 999
                         │
                         ▼
            ┌────────────────────────┐
            │  游戏引擎检查:      │
            │  "这是ConVar，      │
            │   修改存储的值"     │
            └────────────────────────┘
                         │
                         ▼
                test_var的内部值从"42"变为"999"

玩家输入: test_print
                         │
                         ▼
            ┌────────────────────────┐
            │  游戏引擎检查:      │
            │  "这是ConCommand，    │
            │   执行它的函数!"     │
            └────────────────────────┘
                         │
                         ▼
              调用 Dispatch(const CCommand& command)
                         │
                         ▼
              执行 CC_test_print 函数体
              (打印所有cvar的值)
```

### IsCommand()区分方法

```cpp
// ConVar实现
class ConVar : public ConCommandBase {
public:
    virtual bool IsCommand() const override {
        return false;  // ← ConVar返回false
    }
};

// ConCommand实现
class ConCommand : public ConCommandBase {
public:
    virtual bool IsCommand() const override {
        return true;   // ← ConCommand返回true
    }
};
```

游戏引擎通过`IsCommand()`判断如何处理：
- `false` → 作为变量，返回/修改存储的值
- `true` → 作为命令，执行函数

### 实际代码示例对比

**ConVar示例（存储配置）**：
```cpp
// 定义
namespace {
    ConVar esp_enabled("esp_enabled", "1",
        FCVAR_CLIENTDLL | FCVAR_ARCHIVE,
        "Enable ESP feature");
}

// 使用
if (F::ESP::enabled->GetBool()) {
    // ESP功能启用
    DrawESP();
}

// 控制台
] esp_enabled
"esp_enabled" = "1"
] esp_enabled 0
] esp_enabled
"esp_enabled" = "0"
```

**ConCommand示例（执行动作）**：
```cpp
// 定义
CON_COMMAND(esp_reset, "Reset ESP settings to default")
{
    if (!I::Cvar) return;

    // 执行重置逻辑
    F::ESP::ResetColors();
    F::ESP::ResetDistance();

    I::Cvar->ConsolePrintf("ESP settings reset to defaults\n");
}

// 控制台
] esp_reset
ESP settings reset to defaults
```

### 对比总结表

| 特性 | ConVar | ConCommand |
|------|---------|-----------|
| **创建方式** | `ConVar name("name", "default", flags, "help")` | `CON_COMMAND(name, "help") { ... }` |
| **控制台查询** | `] name` → 显示当前值 | `] name` → 执行函数 |
| **控制台修改** | `] name value` → 修改值 | `] name arg1 arg2` → 传递参数给函数 |
| **值持久化** | 支持`FCVAR_ARCHIVE`，保存到config.cfg | 不存储值，无法持久化 |
| **参数访问** | `args.Arg(1)`在命令函数中使用 | 在变量中无意义 |
| **值类型** | `int`, `float`, `const char*` | 无返回值，执行逻辑 |
| **典型用途** | 配置开关、数值范围、颜色值 | 打印信息、执行操作、触发事件 |
| **IsCommand()** | 返回`false` | 返回`true` |

---

## 架构设计

### 文件结构

```
src/
├── SDK/L4D2/Includes/
│   ├── convar.h          # ConVar/ConCommand类声明
│   └── convar.cpp        # 实现
├── Util/CVar/
│   ├── CVarManager.h     # CVar管理器
│   └── CVarManager.cpp   # 实现
├── Features/TestCVar/
│   ├── TestCVar.h        # 测试cvar声明
│   ├── TestCVar.cpp      # 测试cvar定义
│   └── TestCommands.cpp  # 测试命令
```

### 类设计

```
┌─────────────────────────────────────────────────────────────┐
│                    ConCommandBase                            │
├─────────────────────────────────────────────────────────────┤
│ + m_pszName: const char*                                    │
│ + m_pszHelpString: const char*                               │
│ + m_nFlags: int                                              │
│ + m_pNext: ConCommandBase* (链表指针)                         │
│ + m_bRegistered: bool                                        │
│                                                              │
│ + Init(): void        # 注册到游戏引擎                        │
│ + IsCommand(): bool   # 返回true/false区分命令/变量           │
└─────────────────────────────────────────────────────────────┘
           ▲                           ▲
           │                           │
    ┌──────┴──────┐            ┌───────┴───────┐
    │   ConVar    │            │  ConCommand   │
    ├─────────────┤            ├───────────────┤
    │ + m_pParent │            │ + 回调函数     │
    │ + m_fValue  │            │   (多种形式)   │
    │ + m_nValue  │            │               │
    │ + m_pszString│           └───────────────┘
    │              │
    │ + GetFloat()│
    │ + GetInt()  │
    │ + GetString()│
    │ + SetValue()│
    └─────────────┘
```

### 静态初始化链表

```cpp
// 全局静态链表头
ConCommandBase* ConCommandBase::s_pConCommandBases = nullptr;

// 构造函数中添加到链表
ConVar::ConVar(...) {
    m_pNext = s_pConCommandBases;  // 保存旧头
    s_pConCommandBases = this;     // 成为新头
}
```

链表顺序：**后构造的在前，先构造的在后**

---

## 实现过程

### 阶段1：创建基础类结构

**1.1 创建convar.h**

定义核心类和宏：

```cpp
// 基类
class ConCommandBase {
protected:
    const char* m_pszName;
    const char* m_pszHelpString;
    int m_nFlags;
    ConCommandBase* m_pNext;
    static ConCommandBase* s_pConCommandBases;

public:
    virtual bool IsCommand() const = 0;
    void Init();
};

// 控制台变量
class ConVar : public ConCommandBase {
    float m_fValue;
    int m_nValue;
    const char* m_pszString;

public:
    ConVar(const char* pName, const char* pDefaultValue, int flags,
           const char* pHelpString = "");

    float GetFloat() const { return m_fValue; }
    int GetInt() const { return m_nValue; }
    const char* GetString() const { return m_pszString; }
    void SetValue(const char* value);
    void SetValue(float value);
    void SetValue(int value);
};

// CON_COMMAND宏
#define CON_COMMAND(name, description) \
    static void CC_##name(const CCommand& command); \
    namespace { ConCommand g_##name##_command(#name, CC_##name, description); } \
    static void CC_##name(const CCommand& command)
```

**1.2 创建utlvector.h和utlstring.h**

Source引擎风格的基础容器，用于`CCommand`类：

```cpp
template< class T >
class CUtlVector {
    T* m_pMemory;
    int m_Size;
    // ... 基础实现
};

class CUtlString {
    std::string m_String;
public:
    const char* Get() const { return m_String.c_str(); }
};
```

### 阶段2：实现核心功能

**2.1 实现构造函数**

关键：**构造时必须添加到链表**

```cpp
ConVar::ConVar(const char* pName, const char* pDefaultValue, int flags,
               const char* pHelpString)
    : ConCommandBase(pName, pHelpString, flags)
{
    // 初始化值
    m_StringLength = (int)strlen(pDefaultValue) + 1;
    m_pszString = new char[m_StringLength];
    strcpy_s(const_cast<char*>(m_pszString), m_StringLength, pDefaultValue);
    m_fValue = (float)atof(pDefaultValue);
    m_nValue = atoi(pDefaultValue);

    // 添加到全局链表
    m_pNext = s_pConCommandBases;
    s_pConCommandBases = this;
}
```

**2.2 实现Init()方法**

**关键发现**：游戏引擎的`RegisterConCommand()`会修改我们的链表！

```cpp
void ConCommandBase::Init() {
    // 注册到游戏引擎
    I::Cvar->RegisterConCommand(this);
    m_bRegistered = true;
}
```

**2.3 实现ConVar_Register函数**

**关键修复**：在调用Init()之前保存pNext指针

```cpp
void ConVar_Register(int nCVarFlag, IConCommandBaseAccessor* pAccessor) {
    if (!pAccessor)
        ConCommandBase::s_pAccessor = &s_DefaultAccessor;
    else
        ConCommandBase::s_pAccessor = pAccessor;

    // 遍历链表注册所有cvar
    ConCommandBase* pCur = ConCommandBase::s_pConCommandBases;
    while (pCur) {
        ConCommandBase* pNext = pCur->m_pNext;  // 关键：提前保存！
        pCur->Init();
        pCur = pNext;  // 使用保存的指针
    }
}
```

### 阶段3：创建管理器和测试

**3.1 CVarManager**

```cpp
// CVarManager.h
namespace U {
    class CVarManager {
    public:
        static void Initialize();
    };
}

// CVarManager.cpp
namespace U {
    bool CVarManager::m_bInitialized = false;

    void CVarManager::Initialize() {
        if (m_bInitialized) return;

        ConVar_Register(FCVAR_CLIENTDLL, nullptr);
        m_bInitialized = true;
    }
}
```

**3.2 测试CVars**

```cpp
// TestCVar.cpp
namespace {
    // 匿名命名空间中的实际实例
    ConVar _test_var("test_var", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE,
        "Generic test variable for cvar system");

    ConVar _test_var_float("test_var_float", "1.5", FCVAR_CLIENTDLL,
        "Float test variable for cvar system");
}

namespace F {
    // 指针指向实际实例
    ConVar* TestCVar::test_var = &_test_var;
    ConVar* TestCVar::test_var_float = &_test_var_float;

    void TestCVar::Initialize() {
        // 打印初始值
        if (I::Cvar) {
            I::Cvar->ConsolePrintf("[CVar] test_var = %s\n",
                test_var->GetString());
        }
    }
}
```

**3.3 测试命令**

```cpp
// TestCommands.cpp
CON_COMMAND(test_print, "Print all test cvar values to console")
{
    if (!I::Cvar) return;

    I::Cvar->ConsolePrintf("=== Test CVar Values ===\n");
    I::Cvar->ConsolePrintf("test_var = %s\n",
        F::TestCVar::test_var->GetString());
    I::Cvar->ConsolePrintf("test_var_float = %f\n",
        F::TestCVar::test_var_float->GetFloat());
}

CON_COMMAND(test_set, "Set test_var to a specified value")
{
    if (!I::Cvar) return;
    if (args.ArgC() < 2) {
        I::Cvar->ConsolePrintf("Usage: test_set <value>\n");
        return;
    }
    F::TestCVar::test_var->SetValue(args.Arg(1));
}
```

### 阶段4：集成到项目

**4.1 初始化顺序**

```cpp
// Entry/Entry.cpp

void CGlobal_ModuleEntry::Initialize() {
    // 1. 初始化SDK接口
    I::Cvar = U::Interface.Get<ICvar*>("materialsystem.dll", "VEngineCvar007");

    // 2. 初始化CVar管理器（注册所有cvar）
    U::CVarManager::Initialize();

    // 3. 初始化功能模块
    F::TestCVar::Initialize();
}
```

**4.2 项目文件**

添加到`l4d2_base.vcxproj`:
```xml
<ClInclude Include="SDK\L4D2\Includes\convar.h" />
<ClInclude Include="SDK\L4D2\Includes\utlvector.h" />
<ClInclude Include="SDK\L4D2\Includes\utlstring.h" />
<ClInclude Include="Util\CVar\CVarManager.h" />
<ClInclude Include="Features\TestCVar\TestCVar.h" />

<ClCompile Include="SDK\L4D2\Includes\convar.cpp" />
<ClCompile Include="Util\CVar\CVarManager.cpp" />
<ClCompile Include="Features\TestCVar\TestCVar.cpp" />
<ClCompile Include="Features\TestCVar\TestCommands.cpp" />
```

---

## 核心问题与解决方案

### 问题1：静态初始化顺序问题

**现象**：ConVar构造时I::Cvar为nullptr，无法使用ConsolePrintf打印日志

**原因**：C++静态初始化顺序不确定，我们的ConVar可能在DLL加载时构造，此时I::Cvar还未初始化

**解决方案**：
1. 构造函数中不依赖I::Cvar
2. 使用printf替代ConsolePrintf进行关键日志
3. Init()在I::Cvar准备好后才调用

### 问题2：链表遍历中断

**现象**：只有第一个cvar调用了Init()，其他cvar的Init()都没执行

**原因**：游戏引擎的`RegisterConCommand()`会修改我们的链表结构，将cvar加入内部链表，改变了`m_pNext`指针

**证据**：
```
[CVar DEBUG] After Init(), pCur->m_pNext = 388158C8 (was 7AE20E80)
```

**解决方案**：在调用Init()之前保存pNext
```cpp
ConCommandBase* pNext = pCur->m_pNext;  // Save BEFORE Init()
pCur->Init();
pCur = pNext;  // Use saved pointer
```

### 问题3：构造函数日志缺失

**现象**：看不到ConVar构造函数的日志输出

**原因**：构造时I::Cvar为nullptr，if条件跳过

**解决方案**：使用printf作为兜底日志
```cpp
if (I::Cvar) {
    I::Cvar->ConsolePrintf("[DEBUG] ConVar constructed: %s\n", pName);
} else {
    printf("[DEBUG] ConVar constructed (I::Cvar null): %s\n", pName);
}
```

### 问题4：匿名命名空间与静态指针

**目的**：避免命名冲突，同时提供全局访问

```cpp
// 匿名命名空间：内部链接，不会和其他翻译单元冲突
namespace {
    ConVar _test_var("test_var", "0", FCVAR_CLIENTDLL, "...");
}

// F命名空间：外部可见的指针
namespace F {
    ConVar* TestCVar::test_var = &_test_var;  // 指向匿名空间的实例
}
```

---

## 使用指南

### 创建ConVar

**基础用法**：
```cpp
// 在任意cpp文件中
namespace {
    ConVar my_cvar("my_cvar", "0", FCVAR_CLIENTDLL,
        "Description of my cvar");
}
```

**带归档**：
```cpp
ConVar my_cvar("my_cvar", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE,
    "Description - will be saved to config.cfg");
```

**带范围限制**：
```cpp
ConVar my_cvar("my_cvar", "50", FCVAR_CLIENTDLL, "Description",
    true, 0.0f,   // hasMin, minValue
    true, 100.0f); // hasMax, maxValue
```

**带变化回调**：
```cpp
void OnMyCvarChanged(ConVar* pCvar, const char* pOldValue, float flOldValue) {
    printf("my_cvar changed from %s to %s\n", pOldValue, pCvar->GetString());
}

ConVar my_cvar("my_cvar", "0", FCVAR_CLIENTDLL, "Description",
    OnMyCvarChanged);
```

### 创建ConCommand

**基础命令**：
```cpp
CON_COMMAND(my_command, "Description of my command")
{
    if (!I::Cvar) return;

    I::Cvar->ConsolePrintf("My command executed!\n");
}
```

**带参数的命令**：
```cpp
CON_COMMAND(my_command, "Usage: my_command <arg1> <arg2>")
{
    if (!I::Cvar) return;

    if (args.ArgC() < 2) {
        I::Cvar->ConsolePrintf("Usage: my_command <arg1>\n");
        return;
    }

    const char* arg1 = args.Arg(1);
    I::Cvar->ConsolePrintf("Arg1: %s\n", arg1);
}
```

### 代码中访问CVar

**方法1：直接使用全局变量**（如果定义在同一cpp）
```cpp
extern ConVar my_cvar;
my_cvar.SetFloat(1.5f);
```

**方法2：通过指针**（推荐）
```cpp
// MyFeature.h
namespace F {
    class MyFeature {
    public:
        static ConVar* my_cvar;
    };
}

// MyFeature.cpp
namespace {
    ConVar _my_cvar("my_cvar", "0", FCVAR_CLIENTDLL, "...");
}
namespace F {
    ConVar* MyFeature::my_cvar = &_my_cvar;
}

// 使用
float value = F::MyFeature::my_cvar->GetFloat();
```

**方法3：通过游戏引擎查找**（动态）
```cpp
ConVar* pCvar = I::Cvar->FindVar("my_cvar");
if (pCvar) {
    float value = pCvar->GetFloat();
}
```

---

## 后续开发指南

### 添加新的ConVar

**步骤1：决定定义位置**

| 场景 | 推荐位置 | 示例 |
|------|---------|------|
| 功能内部使用 | 功能目录下 | `Features/ESP/ESP.cpp` |
| 全局配置 | 新建CVar文件 | `Features/CVar/ConfigVars.cpp` |
| 测试用途 | `TestCVar.cpp` | 已有 |

**步骤2：定义ConVar**

```cpp
// Features/MyFeature/MyFeature.cpp
namespace {
    // 功能专用cvar
    ConVar my_feature_enabled("my_feature_enabled", "1",
        FCVAR_CLIENTDLL | FCVAR_ARCHIVE,
        "Enable or disable my feature");

    ConVar my_feature_value("my_feature_value", "100",
        FCVAR_CLIENTDLL,
        "Value for my feature", true, 0.0f, true, 200.0f);
}
```

**步骤3：添加外部访问（可选）**

```cpp
// MyFeature.h
namespace F {
    class MyFeature {
    public:
        static ConVar* enabled;
        static ConVar* value;
        static void Initialize();
    };
}

// MyFeature.cpp
namespace F {
    ConVar* MyFeature::enabled = &::my_feature_enabled;
    ConVar* MyFeature::value = &::my_feature_value;

    void MyFeature::Initialize() {
        // 可选的初始化逻辑
    }
}
```

**步骤4：在代码中使用**

```cpp
// 检查cvar
if (F::MyFeature::enabled->GetBool()) {
    // 功能启用
    float val = F::MyFeature::value->GetFloat();
}
```

### 添加新的ConCommand

**步骤1：选择位置**

建议与ConVar放在同一文件，或单独的Commands文件

**步骤2：使用CON_COMMAND宏**

```cpp
CON_COMMAND(my_feature_action, "Perform my feature action")
{
    if (!I::Cvar) return;

    // 执行功能
    I::Cvar->ConsolePrintf("Action performed\n");
}
```

**步骤3：带参数的命令**

```cpp
CON_COMMAND(my_feature_set, "Set my feature value - Usage: my_feature_set <value>")
{
    if (!I::Cvar) return;

    if (args.ArgC() < 2) {
        I::Cvar->ConsolePrintf("Usage: my_feature_set <value>\n");
        return;
    }

    float value = (float)atof(args.Arg(1));
    F::MyFeature::value->SetValue(value);
    I::Cvar->ConsolePrintf("Value set to %f\n", value);
}
```

### 功能模块CVar最佳实践

**推荐结构**：

```
Features/MyFeature/
├── MyFeature.h       # 声明cvar指针
├── MyFeature.cpp     # 定义cvar + 主要逻辑
└── MyFeatureCommands.cpp  # 命令实现
```

**MyFeature.h**：
```cpp
#pragma once
#include "../../SDK/L4D2/Includes/convar.h"

namespace F {
    class MyFeature {
    public:
        // CVars
        static ConVar* enabled;
        static ConVar* intensity;
        static ConVar* color;

        // 初始化
        static void Initialize();

        // 功能方法
        static void Run();
    };
}
```

**MyFeature.cpp**：
```cpp
#include "MyFeature.h"

namespace {
    // CVar定义（匿名命名空间）
    ConVar _enabled("my_feature_enabled", "1",
        FCVAR_CLIENTDLL | FCVAR_ARCHIVE,
        "Enable my feature");

    ConVar _intensity("my_feature_intensity", "50",
        FCVAR_CLIENTDLL,
        "Feature intensity", true, 0.0f, true, 100.0f);

    ConVar _color("my_feature_color", "255 0 0",
        FCVAR_CLIENTDLL,
        "Feature color (R G B)");
}

namespace F {
    // 指针赋值
    ConVar* MyFeature::enabled = &_enabled;
    ConVar* MyFeature::intensity = &_intensity;
    ConVar* MyFeature::color = &_color;

    void MyFeature::Initialize() {
        // 初始化逻辑（可选）
    }

    void MyFeature::Run() {
        if (!enabled->GetBool()) return;

        float i = intensity->GetFloat();
        const char* c = color->GetString();

        // 使用cvar值执行功能
    }
}
```

### CVar命名规范

| 类型 | 前缀 | 示例 |
|------|------|------|
| 功能开关 | `{feature}_enabled` | `esp_enabled` |
| 功能值 | `{feature}_{value}` | `esp_max_distance` |
| 调试 | `debug_{feature}` | `debug_esp_boxes` |
| 测试 | `test_*` | `test_var` |

### 常用FCVAR标志组合

```cpp
// 普通客户端cvar
FCVAR_CLIENTDLL

// 持久化配置
FCVAR_CLIENTDLL | FCVAR_ARCHIVE

// 作弊功能
FCVAR_CLIENTDLL | FCVAR_CHEAT

// 开发调试
FCVAR_CLIENTDLL | FCVAR_DEVELOPMENTONLY
```

---

## 常见问题

### Q1: CVar显示"Unknown command"

**检查清单**：
1. 确认构造函数中添加到链表
2. 确认调用了`ConVar_Register()`
3. 确认使用了正确的pNext保存方式
4. 确认I::Cvar在Register时非空

### Q2: SetValue()后值没变

**检查**：
```cpp
// 确认不是在错误的作用域访问
namespace {
    ConVar _test("test", "0", FCVAR_CLIENTDLL, "");
}
// 错误：extern ConVar test;  // _test是匿名空间的
// 正确：使用指针或FindVar
```

### Q3: CON_COMMAND不工作

**检查**：
1. 确认I::Cvar已初始化
2. 确认文件被编译链接
3. 确认命令在ConVar_Register之后定义

### Q4: CVar值被重置

**原因**：可能被游戏config.cfg覆盖

**解决**：
```cpp
// 使用FCVAR_ARCHIVE让引擎记住你的值
ConVar my_var("my_var", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "");
```

### Q5: 多个文件定义同名CVar

**错误**：
```cpp
// File1.cpp
ConVar test("test", "0", FCVAR_CLIENTDLL, "");

// File2.cpp
ConVar test("test", "1", FCVAR_CLIENTDLL, "");  // 冲突！
```

**正确**：只在一个地方定义，其他地方用指针或FindVar

---

## 附录

### A. 完整的ConVar构造函数签名

```cpp
// 1. 名称 + 默认值 + 标志
ConVar(const char* pName, const char* pDefaultValue, int flags);

// 2. 名称 + 默认值 + 标志 + 帮助文本
ConVar(const char* pName, const char* pDefaultValue, int flags,
       const char* pHelpString);

// 3. 名称 + 默认值 + 标志 + 帮助文本 + 最小值 + 最大值
ConVar(const char* pName, const char* pDefaultValue, int flags,
       const char* pHelpString, bool bMin, float fMin, bool bMax, float fMax);

// 4. 名称 + 默认值 + 标志 + 帮助文本 + 变化回调
ConVar(const char* pName, const char* pDefaultValue, int flags,
       const char* pHelpString, FnChangeCallback_t callback);

// 5. 名称 + 默认值 + 标志 + 帮助文本 + 最小值 + 最大值 + 回调
ConVar(const char* pName, const char* pDefaultValue, int flags,
       const char* pHelpString, bool bMin, float fMin, bool bMax, float fMax,
       FnChangeCallback_t callback);
```

### B. ICvar常用方法

```cpp
// 查找cvar
ConVar* FindVar(const char* name);

// 查找命令
ConCommandBase* FindCommandBase(const char* name);

// 控制台打印
void ConsolePrintf(const char* fmt, ...);

// 注册/注销
void RegisterConCommand(ConCommandBase* pCommandBase);
void UnregisterConCommand(ConCommandBase* pCommandBase);
```

### C. CCommand类

```cpp
class CCommand {
    int m_nArgc;                    // 参数个数
    char m_pArgvBuffer[512];         // 参数字符串缓冲区
    char m_pArgSBuffer[512];         // 参数后字符串
    const char* m_ppArgv[64];        // 参数指针数组

public:
    int ArgC() const;                        // 获取参数个数
    const char* Arg(int nIndex) const;       // 获取第n个参数
    const char* ArgS() const;                // 获取所有参数(除0th)

    // 便利方法
    int FindArg(const char* pName) const;    // 查找参数
    int FindArgInt(const char* pName, int nDefaultVal) const;
};
```

---

## 总结

本次CVar系统实现的核心收获：

1. **理解Source引擎的cvar架构**：ICvar接口、ConCommandBase链表、注册机制
2. **静态初始化的坑**：构造顺序、链表修改、nullptr检查
3. **关键修复**：提前保存pNext指针避免链表遍历中断
4. **良好的组织方式**：匿名命名空间定义 + 外部指针访问

这套系统现在完全可用，可以作为后续功能开发的基础设施。
