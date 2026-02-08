# VMT Hook 索引检测方法

## 概述

在使用 VMT（Virtual Method Table）Hook 时，最大的挑战之一是确定目标函数的正确索引值。错误的索引会导致：
- Hook 到错误的函数（参数不匹配）
- 游戏崩溃（访问无效内存或调用不兼容的函数）
- 功能异常（参数永远无效）

本文档介绍了一种**双重验证机制**来安全地检测和确认正确的 VMT 索引。

## 问题背景

### 症状

当 VMT 索引不正确时，常见症状包括：

1. **参数永远无效**：Detour 函数被调用，但关键参数（如指针类型）永远是 `nullptr`
2. **参数值异常**：整型参数有异常大的值（如 `numorigins = 0x7FFFFFFF`）
3. **调用原函数崩溃**：即使有参数保护，调用原函数也会导致崩溃
4. **游戏无法启动**：Hook 后游戏立即崩溃

### 原因分析

VMT 索引不正确的原因：
- SDK 头文件中的函数声明与实际游戏版本不匹配
- 游戏更新改变了 VMT 布局
- 手动数索引时出现偏差

## 解决方案：双重验证机制

### 1. 静态检查（函数指针有效性）

在 Init 阶段，使用 Windows SEH（结构化异常处理）检查 VMT 中指定索引的函数指针是否指向有效代码内存。

```cpp
static bool IsValidCodePtr(uintptr_t ptr)
{
    if (ptr == 0 || ptr == 0xFFFFFFFF || ptr < 0x10000)
        return false;

    __try
    {
        BYTE opcode = *(BYTE*)ptr;  // 尝试读取内存
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;  // 访问异常
    }
}

static bool IsIndexValid(uintptr_t* vmt, uint32_t index)
{
    if (vmt == nullptr)
        return false;
    uintptr_t funcPtr = vmt[index];
    return IsValidCodePtr(funcPtr);
}
```

**作用**：
- 避免在无效索引上执行 Hook（防止游戏启动时崩溃）
- 打印函数指针地址供参考

### 2. 动态检查（参数有效性统计）

在 Detour 函数中记录每次调用时的参数有效性，统计并打印报告。

```cpp
#ifdef DETECT_VMT_INDEX
if (Detail::s_EnableDetection)
{
    Detail::s_TotalCallCount[currentIdx]++;

    if (origin != nullptr)
        Detail::s_ValidCallCount[currentIdx]++;
    else
        Detail::s_InvalidCallCount[currentIdx]++;

    // 每 100 次调用打印报告
    static int callCounter = 0;
    if (++callCounter >= Detail::s_ReportInterval)
    {
        Detail::PrintReport();
        callCounter = 0;
    }
}
#endif
```

**作用**：
- 判断 Hook 到的函数是否是目标函数
- 通过参数有效率确认索引正确性

## 实现步骤

### 第一步：添加检测代码

在 Hook 实现文件中添加：

```cpp
// #define DETECT_VMT_INDEX  // 取消此注释以启用索引检测模式

namespace Detail
{
#ifdef DETECT_VMT_INDEX
    static std::map<uint32_t, int> s_ValidCallCount;
    static std::map<uint32_t, int> s_InvalidCallCount;
    static std::map<uint32_t, int> s_TotalCallCount;
    static bool s_EnableDetection = true;
    static int s_ReportInterval = 100;
#endif

    static bool IsValidCodePtr(uintptr_t ptr) { /* ... */ }
    static bool IsIndexValid(uintptr_t* vmt, uint32_t index) { /* ... */ }

#ifdef DETECT_VMT_INDEX
    static void PrintReport() { /* ... */ }
#endif
}
```

### 第二步：在 Init 中使用静态检查

```cpp
void Init()
{
    uintptr_t* vmt = *(uintptr_t**)I::Interface;

#ifdef DETECT_VMT_INDEX
    // 打印检测信息
    if (Detail::IsIndexValid(vmt, TargetIndex))
    {
        std::cout << "Index " << TargetIndex << " function pointer: VALID (0x"
            << std::hex << vmt[TargetIndex] << std::dec << ")" << std::endl;
    }
    else
    {
        std::cerr << "Index " << TargetIndex << " has INVALID function pointer!" << std::endl;
        return;  // 跳过此索引的 Hook
    }
#endif

    // 只有索引有效时才执行 Hook
    if (Table.Hook(&Detour, TargetIndex) == false)
    {
        std::cerr << "Failed to hook!" << std::endl;
    }
}
```

### 第三步：在 Detour 中添加动态检查

```cpp
void __fastcall Detour(void* ecx, void* edx, /* 参数 */)
{
#ifdef DETECT_VMT_INDEX
    // 记录参数有效性
    if (keyParam != nullptr)
        Detail::s_ValidCallCount[Index]++;
    else
        Detail::s_InvalidCallCount[Index]++;

    // 定期打印报告
    static int callCounter = 0;
    if (++callCounter >= Detail::s_ReportInterval)
    {
        Detail::PrintReport();
        callCounter = 0;
    }
#endif

    // 参数保护
    if (keyParam == nullptr && shouldHaveValue)
        return;  // 避免崩溃

    // 正常逻辑...
}
```

### 第四步：测试不同索引

1. 在头文件中设置要测试的索引值：
   ```cpp
   constexpr uint32_t Index = 44u;  // 尝试不同的值
   ```

2. 取消 `DETECT_VMT_INDEX` 的注释

3. 编译并运行游戏

4. 查看控制台输出：
   ```
   [RenderView] ===== Index Detection Report =====
   [RenderView] Testing Index: 44
   [RenderView] Total calls: 100
   [RenderView] Valid calls (origin != nullptr): 0
   [RenderView] Invalid calls (origin == nullptr): 100
   [RenderView] Valid rate: 0%
   [RenderView] *** INCORRECT INDEX *** (Low valid rate)
   ```

5. 如果有效率始终为 0% 或很低，尝试下一个索引值

6. 重复直到找到有效率 > 50% 的索引

### 第五步：确认并清理

找到正确索引后：

1. 更新头文件中的索引值
2. 重新注释 `DETECT_VMT_INDEX`
3. 编译最终版本
4. （可选）删除调试代码

## 判断标准

### 正确索引的特征

1. **静态检查通过**：函数指针有效
2. **参数有效率 > 50%**：大多数调用时参数有效
3. **游戏运行正常**：没有崩溃或异常
4. **功能正常工作**：Hook 的预期效果生效

### 错误索引的特征

1. **静态检查失败**：函数指针无效（访问异常）
2. **参数有效率为 0%**：关键参数永远为空
3. **调用原函数崩溃**：即使有参数保护
4. **游戏无法启动**：Hook 后立即崩溃

## 实际案例：ViewSetupVisEx 索引检测

### 背景

需要 Hook `IVRenderView::ViewSetupVisEx` 函数，SDK 头文件显示索引可能是 44，但不确定。

### 过程

1. **尝试索引 44-40**：
   - Index 44-41：游戏可以启动，但 `origin` 参数永远为 `nullptr`
   - Index 40：游戏崩溃

2. **继续尝试**：
   - Index 45：游戏正常启动，`origin` 参数有效

3. **确认结果**：
   - Index 45 是正确的索引
   - 更新头文件并注释调试代码

### 结果

```cpp
// RenderView.h
namespace ViewSetupVisEx
{
    // Index confirmed: 45 (verified via runtime detection)
    constexpr uint32_t Index = 45u;
}
```

## 代码示例

完整实现参考：`src/Hooks/RenderView/RenderView.cpp`

关键部分：
- 第 12-29 行：启用说明和 `DETECT_VMT_INDEX` 宏定义
- 第 31-106 行：`Detail` 命名空间中的检测函数
- 第 126-151 行：Detour 中的动态检测代码
- 第 190-211 行：Init 中的静态检测代码

## 注意事项

1. **性能影响**：动态检测会轻微影响性能，确认索引后应禁用
2. **编译器优化**：使用 `#ifdef` 确保调试代码在发布版本中完全被移除
3. **参数选择**：选择一个最能代表函数特征的关键参数进行验证
4. **报告频率**：`s_ReportInterval` 控制报告打印频率，可根据需要调整
5. **保护机制**：始终在 Detour 中保留参数保护，防止意外崩溃

## 扩展应用

此方法不仅适用于 `ViewSetupVisEx`，还可用于任何 VMT Hook 的索引检测：

- **EngineTrace::GetLeafContainingPoint**
- **EngineServer::GetArea**
- **其他虚函数接口**

只需根据具体函数选择合适的关键参数进行验证即可。

## 总结

VMT 索引检测的双重验证机制提供了一种安全、可靠的方法来确定正确的函数索引：

1. **静态检查**防止游戏启动时崩溃
2. **动态检查**确认 Hook 到了正确的函数

通过这种方法，可以快速定位正确的 VMT 索引，避免盲目尝试导致的崩溃和浪费时间。
