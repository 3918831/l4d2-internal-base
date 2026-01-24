# L4D2 Internal Base 项目文档

## 项目概述

这是一个针对《求生之路2》(Left 4 Dead 2) 的内部修改项目，使用 C++ 和 DirectX 技术开发。该项目主要实现了游戏内的多种功能增强和视觉效果修改，包括传送门系统、ESP（透视）、武器修改等功能。

### 主要技术栈
- **编程语言**: C++17
- **开发环境**: Visual Studio 2022
- **目标平台**: Windows (Win32/x64)
- **构建系统**: MSBuild
- **图形API**: DirectX
- **Hook框架**: MinHook

### 项目架构
项目采用模块化设计，主要分为以下几个核心部分：
- **SDK**: 游戏接口和实体定义
- **Hooks**: 游戏函数拦截和修改
- **Features**: 游戏功能实现
- **Portal**: 传送门系统实现
- **Util**: 工具类和辅助功能

## 核心功能

### 1. 传送门系统 (Portal)
- 实现了类似 Portal 游戏的传送门机制
- 支持递归渲染，最大递归深度为 5 层
- 包含传送门材质和纹理管理
- 提供传送门武器 (Portal Gun) 功能

### 2. ESP 功能
- 实体透视显示
- 3D 方框绘制
- 屏幕外箭头指示
- 武器信息显示

### 3. 游戏修改功能
- 引擎预测 (Engine Prediction)
- 无后坐力 (No Spread)
- 武器修改 (手枪等)
- 材质替换和修改

### 4. Hook 系统
- 客户端函数拦截
- 渲染函数修改
- 输入处理拦截
- 窗口过程拦截

## 构建和运行

### 构建要求
- Visual Studio 2022 (v142 工具集) 或兼容的 MSBuild 版本
- Windows 10 SDK (10.0.26100.0 或更高)
- C++17 支持（如果使用较新工具集）

### 编译过程详解
- 编译指令举例：powershell.exe -Command "& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'g:\Code_Project\Visual Studio 2022 Project\3918831\Github\l4d2-internal-base\src\l4d2_base.sln' /p:Configuration=Debug /p:Platform=x86"


#### 环境兼容性处理
项目原始配置使用 v142 工具集（VS2019），但在某些环境中可能只有旧版本的 MSBuild。以下是环境适配的编译方法：

#### 方法1：使用命令行编译（推荐）

1. **检查 MSBuild 版本**
   ```powershell
   # 查找系统中可用的 MSBuild
   Get-ChildItem "C:\Program Files*" -Recurse -Filter "MSBuild.exe" -ErrorAction SilentlyContinue
   
   # 查看版本
   & "C:\Program Files (x86)\MSBuild\12.0\Bin\MSBuild.exe" -version
   ```

2. **项目配置调整（如需要）**
   如果系统中只有较旧的 MSBuild（如 v12.0），需要调整项目配置：
   - 将 `PlatformToolset` 从 `v142` 改为 `v120`
   - 移除 `LanguageStandard` 和 `ConformanceMode` 设置（v120 不支持 C++17）
   - 使用 `x86` 参数代替 `Win32`

3. **编译命令**
   ```powershell
   cd "src"
   & "C:\Program Files (x86)\MSBuild\12.0\Bin\MSBuild.exe" l4d2_base.sln /p:Configuration=Debug /p:Platform=x86
   ```

#### 方法2：使用 Visual Studio IDE

1. 打开 `src/l4d2_base.sln`
2. 如果出现工具集不匹配错误：
   - 右键项目 → 属性 → 配置属性 → 常规
   - 将"平台工具集"改为可用的版本（如 v120）
   - 在 C/C++ → 语言设置中移除 C++17 标准选项
3. 选择配置：Debug | x86
4. 生成解决方案

### 编译输出

成功编译后将在 `src/Debug/` 目录生成：
- `Lak3_l4d2_hack.dll` (主程序文件)
- `Lak3_l4d2_hack.pdb` (调试符号)
- `Lak3_l4d2_hack.ilk` (增量链接文件)

### 常见问题与解决方案

#### 1. 工具集版本不匹配
**问题**: `error MSB4126: 指定的解决方案配置无效`
**解决**: 
- 使用 `x86` 代替 `Win32` 作为平台参数
- 或安装对应的 Visual Studio 版本

#### 2. C++17 标准不支持
**问题**: 编译器不支持 `stdcpp17`
**解决**: 
- 在项目文件中移除 `<LanguageStandard>stdcpp17</LanguageStandard>`
- 移除 `<ConformanceMode>true</ConformanceMode>`

#### 3. 字符编码警告
**问题**: `warning C4819: 该文件包含不能在当前代码页表示的字符`
**解决**: 
- 将相关文件保存为 UTF-8 with BOM 格式
- 或在编译选项中添加 `/source-charset:utf-8`

#### 4. 链接错误
**问题**: 找不到相关库文件
**解决**: 
- 确保安装了完整的 Windows SDK
- 检查项目链接器设置中的附加库目录

### 运行方式
1. 将生成的 `Lak3_l4d2_hack.dll` 注入到 L4D2 游戏进程
2. 游戏启动后会自动加载 DLL 功能
3. 控制台输出可通过 `CONOUT$` 查看
4. 建议在测试环境中使用，避免影响在线游戏体验

## 开发约定

### 代码风格
- 使用 C++17 标准
- 遵循 Microsoft C++ 代码规范
- 类名使用 PascalCase (如 `CGlobal_ModuleEntry`)
- 函数名使用 PascalCase (如 `Func_TraceRay_Test`)
- 变量名使用 camelCase 或下划线分隔

### 文件组织
- 头文件和源文件分离
- 每个模块有独立的目录
- 接口定义放在 SDK 目录
- 实现细节按功能模块分类

### 命名空间
- 全局对象使用 `G` 命名空间 (如 `G::ModuleEntry`)
- 功能模块使用 `F` 命名空间 (如 `F::ESP`)
- 接口使用 `I` 命名空间 (如 `I::EngineClient`)
- 工具类使用 `U` 命名空间 (如 `U::Interface`)

## 关键文件说明

### 入口点
- `DllMain.cpp`: DLL 主入口点
- `Entry/Entry.cpp`: 模块初始化和主要逻辑

### 核心功能
- `Portal/L4D2_Portal.cpp`: 传送门系统核心实现
- `Features/ESP/ESP.cpp`: 透视功能实现
- `Features/NoSpread/NoSpread.cpp`: 无后坐力功能

### Hook 系统
- `Hooks/Hooks.cpp`: Hook 系统初始化
- `Hooks/ModelRender/ModelRender.cpp`: 渲染拦截
- `Hooks/WndProc/WndProc.cpp`: 窗口消息拦截

### SDK 和工具
- `SDK/SDK.cpp`: SDK 核心实现
- `Util/Interface/Interface.cpp`: 接口获取工具
- `Util/Pattern/Pattern.cpp`: 内存模式搜索

## 调试和测试

### 调试参考
项目 README.md 中包含多个调试参考链接，涵盖：
- 自定义材质修改
- RTTI 支持
- VPhysics 弹道模拟
- L4D2 光晕效果

### 测试功能
项目包含多个测试函数：
- `Func_TraceRay_Test`: 射线追踪测试
- `Func_IPhysicsEnvironment_Test`: 物理环境测试
- `Func_Pistol_Fire_Test`: 手枪开火测试
- `Func_CServerTools_Test`: 服务器工具测试

## 注意事项

1. **安全警告**: 此项目仅用于学习和研究目的，请勿用于在线游戏或违反游戏服务条款
2. **兼容性**: 仅支持特定版本的 L4D2，可能需要根据游戏更新调整偏移量
3. **性能**: 递归渲染等高级功能可能影响游戏性能
4. **稳定性**: Hook 系统可能导致游戏不稳定，建议在测试环境中使用

## 扩展开发

### 添加新功能
1. 在 `Features` 目录下创建新模块
2. 在 `Hooks` 目录下添加必要的拦截点
3. 在 `Entry/Entry.cpp` 中注册新功能
4. 更新项目文件和构建配置

### 修改现有功能
1. 定位到对应的功能文件
2. 修改实现逻辑
3. 确保不影响其他模块
4. 进行充分测试

## 参考资料

项目开发参考了多个技术资源，包括：
- UnknownCheats 论坛的相关教程
- Valve Developer Wiki
- Source Engine 文档
- DirectX 和游戏图形编程资料

更多详细信息和链接请参考 `src/README.md` 文件。