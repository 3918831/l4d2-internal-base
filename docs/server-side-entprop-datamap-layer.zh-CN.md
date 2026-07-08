# Server-side EntProp / Datamap 能力层设计

## 背景

当前 Portal 穿越连续性问题暴露出一个更底层的能力缺口：工程缺少统一的 server-side entity property 读写层。以 `MoveType` 为例，现有代码只能稳定访问 client-side `C_BaseEntity::m_MoveType()`，而实测日志显示：

- `clientMoveType=2` 时，server-side 直接用 `0x144` 读出的值仍为 `0`。
- `CServerTools::IsInNoClipMode` 仍为 `false`。
- 当前穿越路径在 `ExitingPortal` 阶段出现 server/client 不同步：server 已在出口，client 晚到 `WalkMove Exit` 才跳到出口。

Tesla 对 SourceMod 的研究确认：`GetEntityMoveType` / `SetEntityMoveType` 最终通过 `GetEntProp/SetEntProp(entity, Prop_Data, "m_MoveType")` 访问 server `CBaseEntity` datamap 字段，而不是 sendprop、input、SDKCall 或固定 offset。

因此，本工程需要构建一个可复用的 server-side EntProp/datamap 能力层。Portal 的 `MoveType` 需求只是第一个消费者，不应把这套能力写死在 PortalTransition 中。

## 目标

1. 为 server-side `CBaseEntity` 提供统一、可诊断的 datamap 属性读写能力。
2. 所有后续 server-side EntProp 读写都通过该能力层，不在业务模块散落固定 offset。
3. 将 SourceMod 的可靠思路迁移为 C++ 注入式 DLL 可用的最小实现：
   - server entity resolver
   - `GetDataDescMap` vfunc
   - datamap recursive lookup
   - typed read/write
   - offset/type cache
4. 先诊断、再 mutation；每个新属性先验证 offset/type/value，再允许写入。

## 非目标

- 不复制 SourceMod runtime 或 SourcePawn VM。
- 不实现完整 `GetEntProp/SetEntProp` API 面。
- 不在第一阶段支持所有 field type。
- 不替代现有 `Teleport(vtable[118])`、`SetAbs*`、trace hook 等能力。
- 不把多玩家、bot、非玩家实体作为首批功能目标。

## 架构边界

建议新增独立模块，例如：

```text
src/SDK/SourceServer/
  ServerEntityResolver.h/cpp
  ServerDataMap.h/cpp
  ServerEntProp.h/cpp
```

如果为了贴近现有项目结构，也可以放在：

```text
src/SDK/L4D2/Server/
```

关键原则：Portal 模块只能调用公开接口，不直接读取 datamap 结构或写 raw offset。

## 分层

### 1. ServerEntityResolver

职责：把 local player 或 entity index 解析成 server-side `CBaseEntity*`。

输入：

- local client entity pointer
- entity index
- edict fallback

候选链路：

```text
I::EngineClient->GetLocalPlayer()
-> I::ClientEntityList->GetClientEntity(index)
-> I::CServerTools->GetIServerEntity(clientEntity)
-> IServerEntity::GetBaseEntity()
```

fallback：

```text
I::PlayerInfoManager->GetGlobalVars()->pEdicts[index]
-> edict_t::GetUnknown()
-> IServerUnknown::GetBaseEntity()
```

输出：

- chosen server entity pointer
- resolver source: `CServerTools` / `edict` / none
- resolver agreement: both paths exist and match

首批消费者：

- local player server entity
- later: arbitrary server entity by index/ref

### 2. ServerDataMap

职责：获取并解析 server-side datamap。

核心接口：

```cpp
using FnGetDataDescMap = datamap_t* (__thiscall*)(void*);

datamap_t* GetDataDescMap(void* serverEntity);
bool FindDataMapField(datamap_t* map, const char* name, DataMapFieldInfo* out);
```

L4D2 Windows 初始假设：

- `CBaseEntity::GetDataDescMap` vtable index: `11`
- 来源：SourceMod `core.games/common.games.txt`

必须验证：

- vfunc pointer 是否落在 `server.dll` 可读/可执行区间。
- 返回的 `datamap_t*` 是否可读。
- datamap `dataClassName`、`dataNumFields` 是否合理。
- 查找 `m_MoveType` 是否返回稳定 offset/type。

### 3. ServerEntProp

职责：提供业务模块可调用的 typed property API。

建议接口：

```cpp
enum class ServerPropDomain
{
    DataMap,
};

struct ServerEntPropInfo
{
    const char* name;
    int offset;
    int fieldType;
    int fieldSizeBytes;
    const char* dataClassName;
};

bool GetServerEntPropInt(void* serverEntity, const char* name, int* value, ServerEntPropInfo* info = nullptr);
bool SetServerEntPropInt(void* serverEntity, const char* name, int value, ServerEntPropInfo* info = nullptr);
```

首批 field type 支持：

- 1-byte integer / character
- 2-byte integer / short
- 4-byte integer
- bool

不支持的类型必须失败并打印诊断，不允许猜写。

### 4. Portal Consumer

Portal 侧只看高层语义：

```cpp
bool GetLocalPlayerServerMoveType(MoveType_t* moveType);
bool SetLocalPlayerServerMoveType(MoveType_t moveType);
```

Portal 不应知道：

- `GetDataDescMap` vtable index
- datamap struct layout
- `m_MoveType` raw offset
- 字段大小

## 数据流

```text
Portal traversal controller
  -> ServerEntProp facade
    -> ServerEntityResolver
      -> server CBaseEntity*
    -> ServerDataMap cache
      -> GetDataDescMap vfunc
      -> recursive typedescription lookup
    -> typed read/write
  -> focused diagnostics
```

## 缓存策略

按 `(datamap_t*, fieldName)` 缓存：

- offset
- field type
- field size
- declaring datamap class name

不要按 entity pointer 缓存 offset。不同 entity class 可能有不同 datamap。

缓存必须支持诊断输出：

- cache miss: 打印完整 lookup 结果
- cache hit: 低频打印或只在显式 probe 时打印

## 日志设计

新增 focused tags：

- `PortalServerEntProp`
- `PortalDataMapProbe`
- `PortalServerMoveType`

建议首批日志：

```text
[PortalDataMapProbe] entity=%p vtable=%p getDataDescMap=%p map=%p class=%s fields=%d base=%p.
[PortalServerEntProp] field=m_MoveType offset=%d type=%d size=%d class=%s cache=%s value=%d.
[PortalServerMoveType] phase=%s client=%d server=%d toolsNoClip=%s resolver=%s.
```

旧 `PortalMoveTypeProbe` 可以保留，但应把 server-side 读数从 hardcoded `0x144` 切换到 datamap 读法，并暂时保留 `raw144` 作为对照字段。

## 风险

### Datamap 结构布局风险

Source SDK / SourceMod 的 `datamap_t` 和 `typedescription_t` 布局需要在 L4D2 Windows 运行时验证。错误布局可能导致读错字段或崩溃。

缓解：

- 所有 pointer dereference 前使用可读内存检查。
- 先只读，不写。
- 打印 datamap class name、field count、字段名样本。
- 使用 `m_MoveType`、`m_iHealth` 等已知字段做 sanity check。

### Vtable index 风险

SourceMod gamedata 指向 `GetDataDescMap` Windows index 11，但本工程仍需实测验证。

缓解：

- 打印 vtable[11] 地址和模块相对地址。
- 验证返回 map 可读且字段数量合理。
- 若失败，禁止写入并输出明确错误。

### Typed write 风险

字段大小或类型判断错误会破坏 server entity。

缓解：

- 首轮只实现诊断读取。
- 写入前必须有一次成功 `FindDataMapField("m_MoveType")`。
- 写入后立即读回并比较。
- 写入失败或类型未知时不 fallback 到固定 offset。

### 架构扩散风险

如果业务模块绕过能力层直接写 offset，后续会重新变成散乱 patch。

缓解：

- 在文档中明确约束：所有 server-side EntProp 读写必须通过 `ServerEntProp`。
- Portal 只允许调用 `Get/SetLocalPlayerServerMoveType` 这类 facade。
- 后续 code review 把 raw server offset 写入视为架构违规。

## 验证步骤

### Phase 0: Read-only datamap probe

1. 解析 local player server entity。
2. 调 vtable[11] 获取 datamap。
3. 打印 datamap 概览。
4. 查找 `m_MoveType`。
5. 读取 datamap offset 的值。
6. 与旧 `raw144`、client `m_MoveType()`、`CServerTools::IsInNoClipMode` 对照。

成功标准：

- resolver 可稳定拿到 server player。
- `m_MoveType` field 被找到。
- datamap read 值与 `IsInNoClipMode`/实际移动状态更一致，而不是固定为 `raw144=0`。

### Phase 1: Controlled write dry run

1. 在测试命令或受控阶段写 `MOVETYPE_NOCLIP`。
2. 立即读回 server datamap value。
3. 观察 `CServerTools::IsInNoClipMode` 是否变化。
4. 不接入 Portal 穿越，只做手动验证。

成功标准：

- 写入后 datamap readback 匹配目标值。
- 不崩溃。
- 退出时能恢复原值。

### Phase 2: Portal traversal integration

1. 在 GMod-style traversal enter 时设置 server/client `MOVETYPE_NOCLIP`。
2. 在 controlled movement window 中由 Portal solver 管理位置/速度。
3. exit 后恢复原 MoveType。
4. 日志确认 server/client 不再出现 ExitingPortal 阶段一帧大跳错位。

成功标准：

- 偏移/角度/快慢速进门时，server/client movement origin 在关键阶段一致。
- 无卡墙、无入口/出口一帧错位。
- MoveType 总能恢复。

## 与 SourceMod 的关系

直接借鉴：

- `Prop_Data` 通过 datamap 查找字段。
- `GetDataDescMap` + recursive datamap lookup。
- typed read/write，不固定 offset。
- gamedata 作为 vtable/index 线索来源。

不直接复制：

- SourcePawn VM。
- SourceMod entity ref 系统。
- 完整 native registry。
- 完整 sendprop/datamap API 面。

## Tesla Skill 固化

本工程新增 `.agents/skills/tesla-sourcemod-researcher/SKILL.md`。后续遇到类似问题，应让 Tesla 先研究参考实现，再决定工程能力边界。

典型任务：

- SourceMod 某个 stock/native 如何实现。
- 某个 server-side field 是否 datamap/sendprop。
- 某个 CBaseEntity vfunc index 从何而来。
- gamedata 如何为 L4D2 提供差异化 offset/signature。

## 下一步建议

下一步不要直接写 Portal 穿越控制逻辑。建议先做一个 read-only datamap probe：

1. 新建 server-side datamap 最小结构和 resolver facade。
2. 只读 `m_MoveType`，打印 offset/type/value。
3. 编译、复制、实测。
4. 根据日志决定是否进入 controlled write dry run。
