# L4D2 传送门功能开发总结

## 项目概述

本文档记录了 Left 4 Dead 2 (L4D2) 传送门功能的开发过程，包括问题的发现、解决方案的实现以及技术细节的优化。

---

## 问题一：传送门重复创建

### 问题描述

最初实现中，每次开枪（左键或右键）都会创建一个新的传送门实体，导致：
- 连续左键开枪会创建多个蓝色传送门
- 连续右键开枪会创建多个橙色传送门
- 游戏中存在大量冗余的传送门实体

### 原因分析

通过代码分析发现，`CProp_Portal::FindPortal()` 函数每次都创建新实体：

```cpp
// 原始代码（有问题）
CProp_Portal* CProp_Portal::FindPortal(bool bPortal2, bool bCreateIfNothingFound)
{
    if (bCreateIfNothingFound)
    {
        // 每次都创建新实体！
        CProp_Portal* pPortal = (CProp_Portal*)I::CServerTools->CreateEntityByName("prop_dynamic");
        return pPortal;
    }
    return nullptr;
}
```

### 解决方案

#### 1. 修改 `PortalInfo_t` 结构体 (Portal/L4D2_Portal.h:20-33)

添加实体指针来追踪已创建的传送门：

```cpp
struct PortalInfo_t
{
    bool bIsActive = false;
    Vector origin;
    QAngle angles;
    CProp_Portal* pPortalEntity = nullptr;  // 新增：实体指针

    // 平滑缩放动画相关（下一阶段添加）
    Vector lastOrigin = Vector(0, 0, 0);
    float currentScale = 1.0f;
    bool isAnimating = false;
    float lastTime = 0.0f;
    const float SCALE_SPEED = 2.0f;
};
```

#### 2. 修改 `FindPortal()` 函数 (Portal/server/prop_portal.cpp:5-30)

检查是否已存在传送门实体，存在则复用：

```cpp
CProp_Portal* CProp_Portal::FindPortal(bool bPortal2, bool bCreateIfNothingFound)
{
    PortalInfo_t& portalInfo = bPortal2 ? G::G_L4D2Portal.g_OrangePortal : G::G_L4D2Portal.g_BluePortal;

    // 如果传送门实体已存在，直接返回
    if (portalInfo.pPortalEntity != nullptr)
    {
        return portalInfo.pPortalEntity;
    }

    // 传送门不存在，需要创建新的
    if (bCreateIfNothingFound)
    {
        CProp_Portal* pPortal = (CProp_Portal*)I::CServerTools->CreateEntityByName("prop_dynamic");
        if (pPortal)
        {
            portalInfo.pPortalEntity = pPortal;  // 保存指针
        }
        return pPortal;
    }
    return nullptr;
}
```

#### 3. 修改开枪逻辑 (Portal/client/weapon_portalgun.cpp:135-175)

只在第一次创建时设置模型和生成实体：

```cpp
PortalInfo_t& portalInfo = bPortal2 ? G::G_L4D2Portal.g_OrangePortal : G::G_L4D2Portal.g_BluePortal;
bool bAlreadyExists = (portalInfo.pPortalEntity != nullptr);

CProp_Portal* pPortal = CProp_Portal::FindPortal(bPortal2, true);
if (pPortal) {
    // 只在第一次创建时设置模型和生成实体
    if (!bAlreadyExists)
    {
        pPortal->SetModel(bPortal2 ? "models/blackops/portal_og.mdl" : "models/blackops/portal.mdl");
        I::CServerTools->DispatchSpawn(pPortal);
    }

    // 总是更新传送门位置（无论新旧）
    pPortal->Teleport(&vFinalPosition, &qFinalAngles, nullptr);
}
```

### 实现效果

| 操作 | 之前 | 之后 |
|------|------|------|
| 第一次左键开枪 | 创建蓝色传送门 | 创建蓝色传送门 |
| 第二次左键开枪 | 创建新的蓝色传送门（重复） | 移动现有蓝色传送门 |
| 第一次右键开枪 | 创建橙色传送门 | 创建橙色传送门 |
| 第二次右键开枪 | 创建新的橙色传送门（重复） | 移动现有橙色传送门 |

---

## 问题二：传送门缩放动画

### 需求描述

实现传送门在创建或移动时，模型从小到大平滑缩放的动画效果（0.0f → 1.0f）。

### 技术方案

利用 `ModelRender::DrawModelExecute::Detour` Hook，每帧执行渲染时：
1. 检测传送门位置是否变化
2. 如果变化，重置缩放动画
3. 每帧更新缩放比例
4. 通过修改 `m_flModelScale` (偏移 0x728) 应用缩放

### 实现代码 (Hooks/ModelRender/ModelRender.cpp:453-541)

```cpp
if (isBluePortal) {
    // 1. 检测位置变化
    float flDistToLast = pInfo.origin.DistTo(G::G_L4D2Portal.g_BluePortal.lastOrigin);

    if (flDistToLast > 1.0f) {
        G::G_L4D2Portal.g_BluePortal.isAnimating = true;
        G::G_L4D2Portal.g_BluePortal.currentScale = 0.0f;
        G::G_L4D2Portal.g_BluePortal.lastTime = I::EngineClient->OBSOLETE_Time();
    }

    // 2. 更新缩放动画
    if (G::G_L4D2Portal.g_BluePortal.isAnimating) {
        float flCurrentTime = I::EngineClient->OBSOLETE_Time();
        float flFrameTime = flCurrentTime - G::G_LD2Portal.g_BluePortal.lastTime;
        G::G_L4D2Portal.g_BluePortal.lastTime = flCurrentTime;

        G::G_L4D2Portal.g_BluePortal.currentScale += G::G_L4D2Portal.g_BluePortal.SCALE_SPEED * flFrameTime;

        // 3. 检查动画完成
        if (G::G_L4D2Portal.g_BluePortal.currentScale >= 1.0f) {
            G::G_L4D2Portal.g_BluePortal.currentScale = 1.0f;
            G::G_L4D2Portal.g_BluePortal.isAnimating = false;
        }
    }

    // 4. 应用缩放到模型
    C_BaseAnimating* pEntity = reinterpret_cast<C_BaseAnimating*>(I::ClientEntityList->GetClientEntity(model_index));
    if (pEntity) {
        float* pScale = (float*)((uintptr_t)pEntity + 0x728); // m_flModelScale
        if (pScale) {
            *pScale = G::G_L4D2Portal.g_BluePortal.currentScale;
        }
    }
}
```

橙色传送门使用相同逻辑。

---

## 问题三：动画速度不稳定

### 问题描述

动画速度每次都不一致，有时快有时慢。

### 原因分析

| 时间来源 | 问题 |
|---------|------|
| `I::GlobalVars->frametime` | 游戏模拟帧时间，受游戏时间缩放影响（如子弹时间、暂停） |
| `I::GlobalVars->absoluteframetime` | 绝对帧时间，但仍然可能不稳定 |

### 解决方案

改用 `I::EngineClient->OBSOLETE_Time()` 获取真实时间戳，通过时间差计算实际帧时间：

```cpp
// 初始化时间戳
lastTime = I::EngineClient->OBSOLETE_Time();

// 每帧计算真实时间差
float flCurrentTime = I::EngineClient->OBSOLETE_Time();
float flFrameTime = flCurrentTime - lastTime;
lastTime = flCurrentTime;

currentScale += SCALE_SPEED * flFrameTime;
```

### 参考来源

参考 Portal 官方代码的实现：
```cpp
m_fOpenAmount += gpGlobals->absoluteframetime * 2.0f;
```

但我们进一步优化为使用 `OBSOLETE_Time()` 来获得更稳定的时间计算。

---

## 修改文件清单

| 文件 | 修改内容 |
|------|----------|
| [Portal/L4D2_Portal.h](src/Portal/L4D2_Portal.h) | 添加 `pPortalEntity`、`lastTime` 等成员变量 |
| [Portal/server/prop_portal.cpp](src/Portal/server/prop_portal.cpp) | 修改 `FindPortal()` 复用现有实体 |
| [Portal/client/weapon_portalgun.cpp](src/Portal/client/weapon_portalgun.cpp) | 修改开枪逻辑，区分创建和移动 |
| [Hooks/ModelRender/ModelRender.cpp](src/Hooks/ModelRender/ModelRender.cpp) | 实现平滑缩放动画 |

---

## 技术要点总结

### 1. 实体管理
- 使用全局指针追踪已创建的实体
- 在创建前检查 `pPortalEntity` 是否为 `nullptr`
- 复用实体时只调用 `Teleport()` 更新位置

### 2. 动画实现
- 利用 `DrawModelExecute` Hook 每帧执行特性
- 通过位置变化触发动画
- 使用真实时间戳计算帧时间差

### 3. 内存偏移
- `C_BaseAnimating + 0x728` = `m_flModelScale`
- 通过逆向工程获取，适用于 L4D2 client.dll

### 4. 代码组织
- 使用 `PortalInfo_t` 结构体集中管理传送门状态
- 通过 `G::G_L4D2Portal` 全局单例访问
- 蓝色和橙色传送门使用相同逻辑

---

## 开发时间线

| 阶段 | 任务 | 状态 |
|------|------|------|
| 1 | 分析传送门创建流程 | ✅ |
| 2 | 修复重复创建问题 | ✅ |
| 3 | 实现平滑缩放动画 | ✅ |
| 4 | 修复动画速度不稳定 (frametime → absoluteframetime) | ✅ |
| 5 | 最终优化 (absoluteframetime → OBSOLETE_Time) | ✅ |

---

## 编译方法

### 命令行编译

```powershell
# 查找 MSBuild 位置
powershell.exe -Command "& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products * -requires Microsoft.Component.MSBuild -property installationPath"

# 编译 32 位 Debug
powershell.exe -Command "& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'g:\Code_Project\Visual Studio 2022 Project\3918813\Github\l4d2-internal-base\src\l4d2_base.sln' /p:Configuration=Debug /p:Platform=x86"
```

### 重要提示
- 使用 `/p:Platform=x86` NOT `/p:Platform=Win32`
- 需要 Visual Studio 2022 + v142 工具集
- 输出：`src/Debug/Lak3_l4d2_hack.dll`

---

## 测试结果

### 传送门唯一性
- ✅ 蓝色传送门在游戏中保持唯一
- ✅ 橙色传送门在游戏中保持唯一
- ✅ 移动传送门时正确更新位置而非创建新的

### 缩放动画
- ✅ 创建时从 0.0f 平滑缩放到 1.0f
- ✅ 移动时重新播放缩放动画
- ✅ 动画速度稳定，不再受游戏时间缩放影响

---

## 后续优化建议

1. **缩放速度调整**
   - 当前 `SCALE_SPEED = 2.0f`，约 0.5 秒完成动画
   - 可根据需要调整此值（更大 = 更快）

2. **传送门链接验证**
   - 添加蓝色和橙色传送门是否同时存在的检查
   - 只有两个传送门都激活时才能工作

3. **声音效果**
   - 创建传送门时添加音效
   - 移动传送门时添加音效

4. **粒子效果**
   - 创建时添加粒子发射效果
   - 增强视觉反馈

---

## 附录：关键代码位置

| 功能 | 文件 | 行号 |
|------|------|------|
| PortalInfo_t 定义 | [Portal/L4D2_Portal.h](src/Portal/L4D2_Portal.h:20-33) | 20-33 |
| FindPortal | [Portal/server/prop_portal.cpp](src/Portal/server/prop_portal.cpp:5-30) | 5-30 |
| 开枪逻辑 | [Portal/client/weapon_portalgun.cpp](src/Portal/client/weapon_portalgun.cpp:135-175) | 135-175 |
| 缩放动画 | [Hooks/ModelRender/ModelRender.cpp](src/Hooks/ModelRender/ModelRender.cpp:453-541) | 453-541 |
| Hook 入口 | [Hooks/C_Weapon/Weapon_Pistol.cpp](src/Hooks/C_Weapon/Weapon_Pistol.cpp:54-63) | 54-63 |

---

*文档生成时间: 2026-01-25*
*项目: L4D2 Internal Base*
*开发者: Claude + User*
