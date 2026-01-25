# L4D2 Internal Base 项目文档

## 项目概述

这是一个针对《求生之路2》(Left 4 Dead 2) 的内部修改项目，使用 C++ 和 DirectX 技术开发。该项目主要实现了游戏内的多种功能增强和视觉效果修改，包括**传送门系统**（核心功能）、ESP（透视）、武器修改等功能。

### 主要技术栈
- **编程语言**: C++17
- **开发环境**: Visual Studio 2022 (v142 工具集)
- **目标平台**: Windows (Win32/x86)
- **构建系统**: MSBuild
- **图形API**: DirectX 9 / Source Engine
- **Hook框架**: MinHook

### 项目架构
项目采用模块化设计，主要分为以下几个核心部分：
- **SDK**: 游戏接口和实体定义
- **Hooks**: 游戏函数拦截和修改
- **Features**: 游戏功能实现 (ESP, EnginePrediction, NoSpread)
- **Portal**: 传送门系统实现（核心功能）
- **Util**: 工具类和辅助功能

## 核心功能

### 1. 传送门系统 (Portal) ⭐

本项目实现了完整的传送门系统，支持：

| 功能 | 状态 | 说明 |
|------|------|------|
| 传送门创建 | ✅ | 左键创建蓝色传送门，右键创建橙色传送门 |
| 传送门渲染 | ✅ | 使用递归渲染，最大递归深度 5 层 |
| 平滑缩放动画 | ✅ | 创建/移动时从 0.0f 到 1.0f 的缩放动画 |
| 地图切换支持 | ✅ | 支持 `LevelShutdown` → `LevelInitPostEntity` 生命周期 |
| 实体复用 | ✅ | 避免重复创建，通过 `Teleport()` 移动现有传送门 |
| 传送门武器 | ✅ | `CWeaponPortalgun` 类实现开枪逻辑 |

**技术特点**：
- 使用自定义渲染目标 (RenderTarget) 存储传送门视图
- 支持递归渲染，实现"门中门"效果
- 裁剪平面 (Clip Plane) 防止渲染穿帮
- 模板缓冲 (Stencil Buffer) 实现传送门遮罩

**使用方法**：
1. 在游戏中按 **左键** 创建蓝色传送门
2. 按 **右键** 创建橙色传送门
3. 两个传送门会自动建立链接
4. 可以随时重新开枪移动传送门位置

### 2. ESP 功能
- 实体透视显示
- 3D 方框绘制
- 屏幕外箭头指示
- 武器信息显示

### 3. 游戏修改功能
- 引擎预测 (Engine Prediction)
- 无后坐力 (No Spread)
- 武器修改 (手枪等)

### 4. Hook 系统
- `BaseClient`: 地图生命周期拦截 (`LevelInitPostEntity`, `LevelShutdown`)
- `ModelRender`: 渲染函数拦截，用于传送门渲染和动画
- `Weapon`: 武器开火拦截
- 输入处理和窗口过程拦截

## 构建和运行

### 构建要求
- Visual Studio 2022 (v142 工具集)
- Windows 10 SDK (10.0.26100.0)
- C++17 支持

### 快速编译

**查找 MSBuild 位置**：
```powershell
powershell.exe -Command "& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products * -requires Microsoft.Component.MSBuild -property installationPath"
```

**编译命令**（32位 Debug）：
```powershell
powershell.exe -Command "& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'g:\Code_Project\Visual Studio 2022 Project\3918831\Github\l4d2-internal-base\src\l4d2_base.sln' /p:Configuration=Debug /p:Platform=x86"
```

**重要提示**：
- 使用 `/p:Platform=x86` **不是** `/p:Platform=Win32`
- 根据你的 VS 安装路径调整 MSBuild 路径
- 根据你的工作区位置调整解决方案路径

### 编译输出

成功编译后将在 `src/Debug/` 目录生成：
- `Lak3_l4d2_hack.dll` - 主程序文件
- `Lak3_l4d2_hack.pdb` - 调试符号

### 运行方式

#### DLL 注入时机

⚠️ **重要**：请在 **主菜单** 注入 DLL，以获得最佳体验。

| 注入时机 | 状态 | 说明 |
|----------|------|------|
| 主菜单 | ✅ 推荐 | 系统会在加载地图时自动初始化 |
| 游戏运行中 | ⚠️ 受限 | 传送门功能可能无法正常工作 |
| 地图切换后 | ✅ 正常 | 系统会自动重新初始化 |

#### 注入步骤

1. 启动 L4D2 游戏到主菜单
2. 使用 DLL 注入工具将 `Lak3_l4d2_hack.dll` 注入到游戏进程
3. 进入任意地图
4. 控制台应显示初始化成功信息
5. 使用手枪开枪测试传送门功能

#### 控制台输出

注入成功后会看到类似输出：
```
[BaseClient] Initializing BaseClient hooks...
[BaseClient] Hooks installed successfully.
[BaseClient] Not in game. Portal system will be initialized when map loads.
```

加载地图后：
```
[BaseClient] LevelInitPostEntity: Initializing portal system...
[Portal] Created portal texture successfully
[Portal] Found portal material successfully
[BaseClient] Portal system initialized.
```

## 项目文档

详细的技术文档位于 `docs/` 目录：

| 文档 | 内容 |
|------|------|
| [portal-development-summary.md](docs/portal-development-summary.md) | 传送门功能开发总结（重复创建修复、动画实现） |
| [portal-injection-timing-fix.md](docs/portal-injection-timing-fix.md) | DLL注入时机问题修复（地图切换支持） |
| [CLAUDE.md](CLAUDE.md) | Claude Code 开发指南，包含编译命令和架构说明 |

## 开发约定

### 代码风格
- C++17 标准
- Microsoft C++ 代码规范
- 类名：PascalCase (`CGlobal_ModuleEntry`)
- 函数名：PascalCase (`Func_TraceRay_Test`)
- 变量名：camelCase 或匈牙利命名法

### 命名空间约定
| 命名空间 | 用途 | 示例 |
|----------|------|------|
| `G::` | 全局单例 | `G::ModuleEntry`, `G::G_L4D2Portal` |
| `I::` | 游戏接口 | `I::EngineClient`, `I::MaterialSystem` |
| `U::` | 工具类 | `U::Interface`, `U::Pattern`, `U::Math` |
| `F::` | 功能模块 | `F::ESP`, `F::EnginePrediction` |
| `Hooks::` | Hook 相关 | `Hooks::BaseClient`, `Hooks::ModelRender` |

## 关键文件说明

### 传送门系统
| 文件 | 说明 |
|------|------|
| [Portal/L4D2_Portal.h](src/Portal/L4D2_Portal.h) | 传送门系统主类定义 |
| [Portal/L4D2_Portal.cpp](src/Portal/L4D2_Portal.cpp) | 传送门系统核心实现 |
| [Portal/client/weapon_portalgun.cpp](src/Portal/client/weapon_portalgun.cpp) | 传送门武器开枪逻辑 |
| [Portal/server/prop_portal.cpp](src/Portal/server/prop_portal.cpp) | 传送门实体管理 |
| [Portal/CustomRender.h](src/Portal/CustomRender.h) | 自定义渲染工具 |

### Hook 系统
| 文件 | 说明 |
|------|------|
| [Hooks/BaseClient/BaseClient.cpp](src/Hooks/BaseClient/BaseClient.cpp) | 地图生命周期 Hook |
| [Hooks/ModelRender/ModelRender.cpp](src/Hooks/ModelRender/ModelRender.cpp) | 渲染拦截，传送门动画 |
| [Hooks/C_Weapon/Weapon_Pistol.cpp](src/Hooks/C_Weapon/Weapon_Pistol.cpp) | 手枪开火拦截 |

### SDK 和工具
| 文件 | 说明 |
|------|------|
| [SDK/SDK.cpp](src/SDK/SDK.cpp) | SDK 初始化和接口获取 |
| [Util/Interface/Interface.cpp](src/Util/Interface/Interface.cpp) | 接口获取工具 |
| [Util/Pattern/Pattern.cpp](src/Util/Pattern/Pattern.cpp) | 内存模式搜索 |
| [Util/Math/Math.cpp](src/Util/Math/Math.cpp) | 数学工具函数 |

## 注意事项

1. **仅限学习研究**: 本项目仅用于技术学习和研究，请勿用于违反游戏服务条款的场景
2. **版本兼容性**: 仅支持特定版本的 L4D2，游戏更新可能需要调整内存偏移
3. **性能影响**: 递归渲染等高级功能可能影响游戏性能
4. **稳定性**: Hook 系统可能导致游戏不稳定，建议在测试环境使用

## 技术亮点

### 传送门系统实现
- **递归渲染**: 支持最大 5 层递归深度，实现"门中门"效果
- **实时纹理**: 使用自定义 RenderTarget 存储传送门视图
- **智能裁剪**: 使用裁剪平面防止渲染穿帮
- **平滑动画**: 传送门创建/移动时的缩放动画效果
- **生命周期管理**: 支持地图切换时的资源清理和重新初始化

### Hook 技术
- **VMT Hook**: 虚函数表钩子，用于拦截虚函数调用
- **Inline Hook**: 使用 MinHook 进行内联函数钩子
- **Index-based Hook**: 通过虚函数表索引直接挂钩

## 扩展开发

### 添加新功能
1. 在 `Features/` 目录创建新模块
2. 在 `Hooks/` 添加必要的拦截点
3. 在 `Entry/Entry.cpp::Initialize()` 注册新功能
4. 更新项目文件并编译测试

### 修改传送门功能
传送门系统的主要逻辑位于：
- 传送门创建: `Portal/client/weapon_portalgun.cpp`
- 传送门渲染: `Hooks/ModelRender/ModelRender.cpp`
- 系统初始化: `Portal/L4D2_Portal.cpp`
- 地图生命周期: `Hooks/BaseClient/BaseClient.cpp`

## 参考资料

项目开发参考了以下技术资源：
- Valve Developer Wiki - Source Engine 文档
- UnknownCheats 论坛 - 游戏修改教程
- DirectX 和游戏图形编程资料

## 许可声明

本项目仅供学习和研究使用。请勿用于任何商业用途或违反游戏服务条款的场景。

---

*最后更新: 2026-01-26*
*项目: L4D2 Internal Base*
*开发者: Claude + User*
