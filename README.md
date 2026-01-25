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
- **Launcher**: 自动加载器程序（新）

---

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

---

## 快速开始

### 自动加载方案（推荐）✨

**新方案**：项目现已支持自动加载，无需手动注入DLL！

#### 使用步骤

1. **复制文件到游戏目录**
   ```
   left4dead2/
   ├── left4dead2.exe
   ├── L4D2_Portal.exe        ← 复制到这里
   ├── L4D2_Portal.dll        ← 复制到这里
   └── ...
   ```

2. **运行加载器**
   ```
   双击运行 L4D2_Portal.exe
   ```

3. **自动完成**
   - 加载器自动启动L4D2游戏
   - 等待游戏初始化（约10秒）
   - 自动注入Mod DLL
   - 控制台自动最小化到后台
   - 进入游戏即可使用传送门功能

#### 游戏启动参数

加载器默认使用以下启动参数：
- `-insecure` - 禁用VAC反作弊（测试用）
- `-steam` - 启用Steam认证
- `-novid` - 跳过开场视频

**注意**：如果游戏已运行，加载器会直接注入DLL，不会重新启动游戏。

### 手动注入方式（传统方式）

如果您更喜欢使用注入工具：

1. 启动 L4D2 游戏到主菜单
2. 使用 DLL 注入工具注入 `L4D2_Portal.dll`
3. 进入任意地图测试

---

## 构建和编译

### 构建要求
- Visual Studio 2022 (v142 工具集)
- Windows 10 SDK (10.0.26100.0)
- C++17 支持

### 查找 MSBuild 位置

```powershell
powershell.exe -Command "& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products * -requires Microsoft.Component.MSBuild -property installationPath"
```

### 编译命令

**Debug 版本**：
```powershell
powershell.exe -Command "& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'src\l4d2_base.sln' /p:Configuration=Debug /p:Platform=x86"
```

**Release 版本**：
```powershell
powershell.exe -Command "& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'src\l4d2_base.sln' /p:Configuration=Release /p:Platform=x86"
```

**重要提示**：
- 使用 `/p:Platform=x86` **不是** `/p:Platform=Win32`
- 根据你的 VS 安装路径调整 MSBuild 路径
- 根据你的工作区位置调整解决方案路径

### 编译输出

成功编译后将在 `src/Debug/` 或 `src/Release/` 目录生成：

| 文件 | 大小 (Debug) | 说明 |
|------|-------------|------|
| `L4D2_Portal.dll` | 390 KB | Mod DLL |
| `L4D2_Portal.exe` | 58 KB | 加载器程序 |

### 解决方案结构

```
l4d2_base.sln
├── l4d2_base.vcxproj    # Mod DLL项目
└── Launcher.vcxproj      # 加载器项目
```

---

## 控制台输出

### 控制台行为

Debug版本会自动分配控制台窗口，但会自动最小化到后台。您可以在任务栏找到控制台窗口查看调试信息。

### 典型输出示例

**加载器启动**：
```
========================================
L4D2 Internal Base - Launcher
========================================

Launching L4D2...
Game launched. Waiting for initialization...
Injecting DLL...
DLL injected successfully!

Done! You can now close this window.
```

**Mod初始化（主菜单）**：
```
[BaseClient] Initializing BaseClient hooks...
[BaseClient] Hooks installed successfully.
```

**进入地图后**：
```
[BaseClient] LevelInitPostEntity: Initializing portal system...
[Portal] Created portal texture successfully
[Portal] Found portal material successfully
[BaseClient] Portal system initialized.
```

---

## 项目文档

详细的技术文档位于 `docs/` 目录：

| 文档 | 内容 |
|------|------|
| [dll-auto-loading-implementation.md](docs/dll-auto-loading-implementation.md) | DLL自动加载方案实现文档（新） |
| [portal-development-summary.md](docs/portal-development-summary.md) | 传送门功能开发总结 |
| [portal-injection-timing-fix.md](docs/portal-injection-timing-fix.md) | DLL注入时机问题修复 |
| [CLAUDE.md](CLAUDE.md) | Claude Code 开发指南，包含编译命令和架构说明 |

---

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

---

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

### 加载器
| 文件 | 说明 |
|------|------|
| [Launcher/Launcher.cpp](src/Launcher/Launcher.cpp) | 加载器主程序 |
| [Launcher/Launcher.vcxproj](src/Launcher/Launcher.vcxproj) | 加载器项目配置 |

---

## 技术亮点

### 传送门系统实现
- **递归渲染**: 支持最大 5 层递归深度，实现"门中门"效果
- **实时纹理**: 使用自定义 RenderTarget 存储传送门视图
- **智能裁剪**: 使用裁剪平面防止渲染穿帮
- **平滑动画**: 传送门创建/移动时的缩放动画效果
- **生命周期管理**: 支持地图切换时的资源清理和重新初始化

### 自动加载方案
- **进程注入**: 使用 `CreateRemoteThread` 和 `LoadLibrary` 技术
- **智能检测**: 自动检测游戏运行状态
- **参数传递**: 支持自定义游戏启动参数
- **用户友好**: 一键启动，自动完成所有操作

### Hook 技术
- **VMT Hook**: 虚函数表钩子，用于拦截虚函数调用
- **Inline Hook**: 使用 MinHook 进行内联函数钩子
- **Index-based Hook**: 通过虚函数表索引直接挂钩

---

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

### 自定义启动参数

如需修改游戏启动参数，编辑 `Launcher/Launcher.cpp:189`：

```cpp
const char* cmdArgs = "-insecure -steam -novid -console -windowed";
```

常用参数：
- `-insecure` - 禁用VAC
- `-steam` - Steam认证
- `-novid` - 跳过视频
- `-console` - 启用控制台
- `-windowed` - 窗口模式
- `-width 1920 -height 1080` - 设置分辨率

---

## 常见问题

### Q: 加载器启动后游戏没有反应？
A: 请确保加载器和DLL都在游戏目录中，且路径中没有中文或特殊字符。

### Q: 传送门功能不工作？
A: 请确保在主菜单进入地图，不要在游戏运行中注入。

### Q: 控制台窗口太大？
A: Debug版本的控制台会自动最小化。您可以在任务栏找到它。

### Q: 可以在联网游戏中使用吗？
A: 不建议。本项目仅用于离线测试和学习，联网可能导致VAC封禁。

---

## 注意事项

1. **仅限学习研究**: 本项目仅用于技术学习和研究，请勿用于违反游戏服务条款的场景
2. **版本兼容性**: 仅支持特定版本的 L4D2，游戏更新可能需要调整内存偏移
3. **VAC风险**: 即使使用`-insecure`参数，仍有VAC风险，建议在离线模式测试
4. **性能影响**: 递归渲染等高级功能可能影响游戏性能
5. **稳定性**: Hook 系统可能导致游戏不稳定，建议在测试环境使用

---

## 参考资料

项目开发参考了以下技术资源：
- Valve Developer Wiki - Source Engine 文档
- UnknownCheats 论坛 - 游戏修改教程
- DirectX 和游戏图形编程资料

---

## 许可声明

本项目仅供学习和研究使用。请勿用于任何商业用途或违反游戏服务条款的场景。

---

*最后更新: 2026-01-26*
*项目: L4D2 Internal Base*
*开发者: Claude + User*
