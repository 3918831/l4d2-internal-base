# Portal SDK 2013 传送门穿越逻辑研究

## 研究目标

本文记录对 `Reference Code/portal-sdk-2013` 中 Portal 官方工程的穿越逻辑研究结论，重点关注 `src/game/server/portal`、`src/game/client/portal`、`src/game/shared` 以及相关 public 接口的协作方式。

后续目标是在当前 L4D2 注入式 DLL 中参考该工程，复刻玩家穿越传送门的核心逻辑。当前约束如下：

- 优先只考虑单人线下游戏与 localserver。
- 优先只考虑玩家穿越，其他实体穿越后续扩展。
- 当前主工程已有视觉效果，但穿越逻辑仍需按官方主体方案补齐。
- 不寻找替代方案，先以 Portal SDK 2013 官方实现为事实源，再讨论可精简部分。

## 工程完整性判断

`Reference Code/portal-sdk-2013` 看起来确实是一个完整 Portal SDK 2013 工程。目录中存在 `src/game/server/Release_portal`、`src/game/client/Release_portal` 等编译产物痕迹，例如 `server.lib`、`server.pdb` 和大量 `.sbr` 文件，因此“该工程可以完整编译出 Portal 游戏核心 DLL”的说法基本可信。

本次没有执行编译验证。

另外，当前参考工程中没有看到 `src/game/public` 目录。与 `public` 相关的接口更多来自 `src/public`，以及 `server/client/shared` 中包含的 Source SDK 公共接口头。

## 总体结论

Portal 官方穿越逻辑不是简单的“碰到门面后瞬移”。它由三层系统共同完成：

1. `prop_portal` 管理 portal 的 linked/active 状态、触碰事件、实体归属与最终传送。
2. `PortalSimulation` 在门洞附近生成一套特殊碰撞环境，使门洞区域不再按普通墙体处理。
3. `portal_gamemovement` 改写玩家 movement trace，使玩家可以嵌入门洞、停留、继续移动，并在中心越过 portal plane 后被传送。

官方主体流程如下：

```text
玩家接触 portal trigger
  -> portal 检查 active/link/teleportable
  -> PortalSimulator 接管玩家
  -> 玩家进入 portal environment
  -> movement trace 改用 portal-aware collision
  -> 玩家中心越过 portal plane 且 hull 位于 portal hole
  -> 用 this-to-linked matrix 转换 origin/angles/velocity
  -> Teleport 到 linked portal 出口
  -> linked PortalSimulator 接管玩家
  -> 强制标记玩家与出口 portal 正在 touch
  -> 客户端收到 EntityPortalled，修正视觉与插值
```

这意味着后续在 L4D2 中复刻时，不能只做 `Touch -> Teleport`。如果没有 portal-aware movement trace，玩家会被原墙体碰撞挡住，无法稳定进入门洞，更无法实现半截嵌入、停留和无缝穿越。

## 关键文件索引

### Server 侧

- `src/game/server/portal/prop_portal.cpp`
  - `CProp_Portal::StartTouch`
  - `CProp_Portal::Touch`
  - `CProp_Portal::EndTouch`
  - `CProp_Portal::ShouldTeleportTouchingEntity`
  - `CProp_Portal::TeleportTouchingEntity`
  - `CProp_Portal::UpdatePortalTeleportMatrix`
  - `CProp_Portal::PortalSimulator_TookOwnershipOfEntity`
  - `CProp_Portal::PortalSimulator_ReleasedOwnershipOfEntity`
  - `CProp_Portal::SharedEnvironmentCheck`

- `src/game/server/portal/PortalSimulation.cpp`
  - `CPortalSimulator::MoveTo`
  - `CPortalSimulator::AttachTo`
  - `CPortalSimulator::TakeOwnershipOfEntity`
  - `CPortalSimulator::ReleaseOwnershipOfEntity`
  - `CPortalSimulator::CreateAllCollision`
  - `CPortalSimulator::CreateLocalCollision`
  - `CPortalSimulator::CreatePolyhedrons`
  - `CPortalSimulator::EntityIsInPortalHole`
  - `CPortalSimulator::RayIsInPortalHole`
  - `CPortalSimulator::PrePhysFrame`
  - `CPortalSimulator::PostPhysFrame`

- `src/game/server/portal/portal_gamemovement.cpp`
  - `CPortalGameMovement::ProcessMovement`
  - `CPortalGameMovement::TracePlayerBBox`
  - `CPortalGameMovement::TestPlayerPosition`
  - `CPortalGameMovement::CategorizePosition`
  - `CPortalGameMovement::FunnelIntoPortal`

- `src/game/server/portal/portal_util_shared.cpp`
  - `UTIL_Portal_TraceRay_With`
  - `UTIL_Portal_TraceRay`
  - `UTIL_PortalLinked_TraceRay`
  - `UTIL_Portal_TraceEntity`
  - `UTIL_DidTraceTouchPortals`
  - `UTIL_Portal_PointTransform`
  - `UTIL_Portal_VectorTransform`
  - `UTIL_Portal_AngleTransform`
  - `UTIL_Portal_RayTransform`
  - `UTIL_Portal_EntityIsInPortalHole`

- `src/game/server/portal/prop_portal_shared.cpp`
  - `CProp_Portal_Shared::UpdatePortalTransformationMatrix`
  - `CProp_Portal_Shared::IsEntityTeleportable`
  - `CProp_Portal_Shared::AllPortals`
  - portal local bounds: `vLocalMins` / `vLocalMaxs`

### Client 侧

- `src/game/client/portal/c_portal_player.cpp`
  - `C_Portal_Player::PlayerPortalled`
  - `OnDataChanged` 中的 portal environment 修正
  - 客户端插值与视角连续性处理

- `src/game/client/portal/c_prop_portal.cpp`
  - portal 网络状态与客户端实体表现

- `src/game/client/portal/PortalRender.cpp`
  - 视觉递归渲染相关，不是本次穿越逻辑核心，但与无缝视觉体验配合。

### Shared / Public 相关

- `src/game/shared/gamemovement.cpp`
  - 普通 Source 玩家移动基类逻辑。
  - Portal 版本通过 `portal_gamemovement.cpp` 替换或扩展关键 movement trace。

- `src/game/shared/gamemovement.h`
  - `ProcessMovement`
  - `TracePlayerBBox`
  - `TryPlayerMove`
  - `CheckStuck`
  - `CategorizePosition`

- `src/public/engine/IEngineTrace.h`
  - `TraceRay`
  - `ClipRayToEntity`
  - `ClipRayToCollideable`
  - `SweepCollideable`
  - `GetBrushesInAABB`

## `prop_portal` 的职责

`CProp_Portal` 是穿越逻辑的入口。它承担以下职责：

- 保存 linked portal：`m_hLinkedPortal`
- 保存 this-to-linked 传送矩阵：`m_matrixThisToLinked`
- 保存 portal 平面：`m_plane_Origin`
- 持有 `CPortalSimulator m_PortalSimulator`
- 作为 `CPortalSimulatorEventCallbacks` 的实现者，在 simulator 接管或释放玩家时更新玩家 portal environment
- 负责最终调用 `Teleport`

### 触碰阶段

`StartTouch` 和 `Touch` 会在 portal 激活且 linked 后检查触碰实体。

关键逻辑：

```text
if entity is teleportable:
  if entity center is in front of portal plane:
    if SharedEnvironmentCheck(entity):
      release from previous simulator if needed
      current simulator TakeOwnershipOfEntity(entity)
```

这个阶段不会立即传送。它只是把玩家纳入 portal environment，让后续 movement trace 使用特殊碰撞环境。

### 传送判定

核心函数是 `CProp_Portal::ShouldTeleportTouchingEntity`。

必须满足：

- 当前 portal simulator 拥有该实体。
- 实体可传送。
- portal 有 linked partner。
- 实体中心点已经越过 portal plane。
- 实体仍然处于 portal hole 内。

伪代码：

```cpp
if (!portalSimulator.OwnsEntity(entity))
    return false;

if (!IsEntityTeleportable(entity))
    return false;

if (!linkedPortal)
    return false;

if (Dot(portalPlane.normal, entityCenter) < portalPlane.dist)
{
    if (portalSimulator.EntityIsInPortalHole(entity))
        return true;
}

return false;
```

这一点非常关键：Portal 允许玩家半截进入门洞。真正传送发生在“中心越过平面”之后，而不是刚触碰 portal trigger 时。

### 真正传送

核心函数是 `CProp_Portal::TeleportTouchingEntity`。

玩家分支做了以下事情：

1. 记录玩家传送前 eye angles。
2. 设置 `m_qPrePortalledViewAngles`、`m_bFixEyeAnglesFromPortalling`、`m_matLastPortalled`。
3. 如遇地面/天花板 portal，可能强制 duck，规避玩家 AABB 无法旋转的问题。
4. 使用 `m_matrixThisToLinked` 转换玩家位置、角度和速度。
5. 对出口朝上的 portal 设置最小出口速度。
6. 限制最大出口速度。
7. 释放入口 simulator 的实体归属。
8. 出口 simulator 接管实体。
9. 清空 ground entity。
10. 更新玩家 physics position。
11. 调用 `Teleport(&newOrigin, &newAngles, &newVelocity)`。
12. 设置 `pl.v_angle` 和 `pl.fixangle = FIXANGLE_ABSOLUTE`。
13. 强制标记玩家与出口 portal 正在 touch。
14. 发送 `EntityPortalled` usermessage。
15. 强制 network state update。

玩家 origin 的处理不是直接转换 `GetAbsOrigin()`，而是先转换 `WorldSpaceCenter()`，再把 origin-center 偏移加回去：

```cpp
ptNewOrigin = m_matrixThisToLinked * ptOtherCenter;
ptNewOrigin += ptOtherOrigin - ptOtherCenter;
```

这样做可以降低玩家 hull/AABB 与 portal 平面交错时的错位。

## `PortalSimulation` 的职责

`CPortalSimulator` 是官方穿越逻辑里最复杂也最重要的一层。它不是单纯关闭墙体碰撞，而是为 portal 附近创建特殊碰撞环境。

### 数据结构

`PortalSimulation.h` 中的关键数据：

- `PS_PlacementData_t`
  - portal 中心、角度、forward/right/up
  - portal plane
  - `matThisToLinked`
  - `matLinkedToThis`
  - `pHoleShapeCollideable`

- `PS_SD_Static_World_t`
  - 门前世界 brush 碰撞
  - 门前 static props 碰撞

- `PS_SD_Static_Wall_t`
  - 门后 holy wall
  - 门洞 tube
  - linked portal 的远端碰撞，变换到本地

- `PS_SD_Dynamic_t`
  - 每个实体的 portal simulation flags
  - owned entities
  - shadow clones

关键 flag：

```cpp
PSEF_OWNS_ENTITY
PSEF_OWNS_PHYSICS
PSEF_IS_IN_PORTAL_HOLE
PSEF_CLONES_ENTITY_FROM_MAIN
```

对玩家-only localserver 移植，`PSEF_OWNS_ENTITY` 和 `PSEF_IS_IN_PORTAL_HOLE` 是最核心的两个状态。

### 门洞碰撞构造

`CreatePolyhedrons` 与 `CreateLocalCollision` 会围绕 portal 生成几类碰撞：

- `World.Brushes`
  - portal 前方附近的世界 brush。

- `World.StaticProps`
  - portal 前方附近的 static prop。

- `Wall.Local.Brushes`
  - 门洞周围的墙体。

- `Wall.Local.Tube`
  - 一个最小 tube，用于约束实体是否能够合法穿过门洞。

- linked portal 的 world collision
  - 通过 portal transform 映射到当前 portal 空间。

这就是官方“在传送门所在位置关闭碰撞”的具体实现：不是直接把原墙体设成 non-solid，而是构造一套裁剪后的替代碰撞环境，让玩家在门洞中看见“洞”，同时仍然被洞边缘和远端世界正确约束。

### 实体归属

`TakeOwnershipOfEntity` 会：

- 标记 simulator 拥有该实体。
- 判断实体是否在 portal hole 内。
- 调用 callback 更新游戏层状态。
- 通知实体 `CollisionRulesChanged`。
- 唤醒 physics object 并重算 contact points。

玩家被接管后，`CProp_Portal::PortalSimulator_TookOwnershipOfEntity` 会设置：

```cpp
player->m_hPortalEnvironment = this;
```

释放时则清空：

```cpp
player->m_hPortalEnvironment = NULL;
```

这就是 `portal_gamemovement` 判断玩家是否处于 portal environment 的依据。

## `portal_gamemovement` 的职责

`portal_gamemovement.cpp` 是玩家“能不能真的走进门洞”的关键。

### ProcessMovement

`CPortalGameMovement::ProcessMovement` 每帧读取：

```cpp
m_bInPortalEnv = player->m_hPortalEnvironment != NULL;
g_bAllowForcePortalTrace = m_bInPortalEnv;
g_bForcePortalTrace = m_bInPortalEnv;
```

然后执行 `PlayerMove()`。

也就是说，只要玩家被 portal simulator 接管，后续 movement trace 就会进入 portal-aware 模式。

### TracePlayerBBox

`CPortalGameMovement::TracePlayerBBox` 构造玩家 hull ray，然后调用：

```cpp
UTIL_Portal_TraceRay_With(player->m_hPortalEnvironment, ray, mask, filter, trace);
```

如果普通 bbox trace 没撞到东西，但 trace 触到 portal，则进一步调用：

```cpp
UTIL_Portal_TraceEntity(player, start, end, mask, filter, tempTrace);
```

这个机制是穿越流畅性的核心。如果 L4D2 里只在 `FinishMove` 或 tick 末尾瞬移，而不改 `TracePlayerBBox`，玩家在进入门洞时仍会被原墙体卡住。

### CategorizePosition / ground trace

Portal 还改写了 ground check。`TracePlayerBBoxForGround2` 会把玩家脚下四个子 hull 分别 trace，并在玩家处于 portal environment 时使用 `UTIL_Portal_TraceRay`。

这解决了玩家半截在 portal 中时 ground entity、坡面、站立状态判断错误的问题。

### FunnelIntoPortal

`FunnelIntoPortal` 只针对 floor portal 做轻微自动修正：

- 玩家看向下方。
- 玩家正在下落。
- 玩家水平位置接近 portal 矩形。
- 玩家横向输入不大。

它会将玩家 wishdir 轻微拉向 portal 中心，或者在非常接近时清零水平速度，使 fling 更稳定。

该功能不是穿越必需项，但对手感很有帮助。

## `portal_util_shared` 的职责

`portal_util_shared.cpp` 提供 portal-aware trace 和 transform 工具。

### Transform 工具

核心函数：

- `UTIL_Portal_PointTransform`
- `UTIL_Portal_VectorTransform`
- `UTIL_Portal_AngleTransform`
- `UTIL_Portal_RayTransform`
- `UTIL_Portal_PlaneTransform`

传送时：

- 位置使用 matrix 乘点。
- 速度使用 matrix 旋转。
- 视角使用 matrix 变换角度。
- trace ray 可以被转换到 linked portal 空间。

### Trace 工具

核心函数：

- `UTIL_Portal_TraceRay_With`
  - 如果没有 portal environment，走普通 `enginetrace->TraceRay`。
  - 如果有 portal environment，比较 real trace 和 portal trace。

- `UTIL_Portal_TraceRay`
  - 只 trace 指定 portal 的特殊碰撞环境。

- `UTIL_PortalLinked_TraceRay`
  - 把 ray 变换到 linked portal 空间，trace 后再把结果变换回来。

- `UTIL_Portal_TraceEntity`
  - 专门处理带 hull 的实体跨 portal trace。

- `UTIL_DidTraceTouchPortals`
  - 判断 ray 或 box trace 是否触到 portal 表面。

对玩家-only 移植，最关键的是：

1. 能判断玩家 hull 是否与 portal aperture 相交。
2. 能在玩家处于 portal environment 时，让 movement trace 不被原 portal 所在墙面挡住。
3. 能在必要时 trace linked portal 后方的碰撞，避免玩家穿出后直接卡入远端墙体。

## Client 侧连续性

服务器真正传送后会发送 `EntityPortalled` usermessage。

客户端 `C_Portal_Player::PlayerPortalled` 以及后续 `OnDataChanged` 会处理：

- 传送消息延迟处理。
- portal environment 变化。
- eye position interpolation。
- 视角/模型/插值状态修正。

对当前 L4D2 注入式 DLL，若只做 localserver 玩家传送，最小版本可以先在服务端修正玩家 origin/velocity/viewangles，并在客户端本地预测或渲染侧同步处理视觉连续性。但如果要达到官方“无缝”体验，仍需要处理客户端插值帧、相机变换和 view angle 突变。

## 与当前 L4D2 主工程的对照

当前主工程已有一些基础：

- 已有 `GameMovement001` 接口。
- 已有 `CCSGameMovement::TracePlayerBBox` hook。
- 已有 `IPhysicsCollision` 接口获取。
- 已经尝试通过 vtable index 118 调用 `CBaseEntity::Teleport`。
- 已有 `CMoveData`、`IPrediction::SetupMove/FinishMove` hook。
- 已有 portal transform / transition 相关文件。

但要复刻官方主体方案，还需要补齐或确认以下能力。

### 必须补齐

- 玩家 portal environment 状态。
  - 等价于 Portal 的 `m_hPortalEnvironment`。
  - 可以先只维护本地玩家与两个 portal 的状态，不要求 NetVar。

- portal ownership 管理。
  - 等价于 `CPortalSimulator::TakeOwnershipOfEntity` / `ReleaseOwnershipOfEntity`。
  - 玩家-only 版本只需要记录“当前玩家属于哪个 portal environment”。

- portal hole 判断。
  - 等价于 `EntityIsInPortalHole`。
  - 玩家-only 版本可先用 portal basis 向量 + player hull extents 做 OBB/矩形体积测试。

- movement trace 改写。
  - 必须在 `TracePlayerBBox` hook 中引入 portal-aware trace。
  - 目标是玩家在 portal environment 中不被门所在墙体阻挡。

- 传送判定。
  - 玩家中心越过 portal plane。
  - 玩家 hull 仍在 portal aperture/tube 内。

- 传送执行。
  - origin、viewangles、velocity 使用 `thisToLinked` 变换。
  - 清空 ground entity。
  - 修正 duck/AABB。
  - 修正 velocity。

- 视角连续性。
  - 等价于 `pl.v_angle`、`fixangle`、`m_bFixEyeAnglesFromPortalling`。

### 需要签名或接口确认

- `CBaseEntity::Teleport`
- `CBaseEntity::SetAbsOrigin`
- `CBaseEntity::SetAbsAngles`
- `CBaseEntity::SetAbsVelocity`
- `CBaseEntity::SetGroundEntity`
- `CBaseEntity::CollisionRulesChanged`
- `CBaseEntity::PhysicsMarkEntitiesAsTouching`
- `CBasePlayer::pl.v_angle`
- `CBasePlayer::pl.fixangle`
- `CBasePlayer::ForceDuckThisFrame` 或等价 duck 状态控制
- `CBasePlayer::UpdateVPhysicsPosition`
- `CBaseEntity::WorldSpaceCenter`
- `CBaseEntity::CollisionProp`
- `CCollisionProperty::WorldSpaceAABB`
- `CCollisionProperty::OBBMins`
- `CCollisionProperty::OBBMaxs`
- `enginetrace->SweepCollideable`
- `enginetrace->GetCollideable`
- `enginetrace->ClipRayToCollideable`
- `physcollision->TraceBox`

### 完整官方碰撞方案需要的额外接口

如果后续要进一步贴近官方 `PortalSimulation` 的完整碰撞裁剪方案，还需要：

- `enginetrace->GetBrushesInAABB`
- brush 转 polyhedron 的能力
- `physcollision->ConvertConvexToCollide`
- `physcollision->DestroyCollide`
- `staticpropmgr->GetAllStaticPropsInAABB`
- static prop collideable/polyhedron 缓存
- `partition->EnumerateElementsAlongRay`
- 自定义 portal simulator collision entity
- physics collision event filter
- VPhysics shadow clone

玩家-only localserver 第一阶段可以不实现 static prop 裁剪、VPhysics shadow clone 和多实体物理克隆。

## 推荐移植分层

### 第一阶段：玩家-only 几何版

目标：玩家能穿过两个 portal，不追求所有实体和完整物理。

实现内容：

1. 建立两个 portal 的几何状态：
   - origin
   - angles
   - forward/right/up
   - plane
   - linked pointer
   - this-to-linked matrix

2. 建立玩家 portal environment：
   - 当前是否触碰/进入 portal。
   - 当前属于哪个 portal。
   - 是否位于 portal hole。

3. 在 `TracePlayerBBox` hook 中：
   - 判断玩家是否处于 portal environment。
   - 对门洞区域跳过或替换原墙体碰撞。
   - 保留洞边缘 tube 约束。

4. 在移动后或 touch 检测中：
   - 如果玩家中心越过 portal plane 且 hull 在 aperture 内，则执行传送。

5. 传送时：
   - 变换 origin。
   - 变换 velocity。
   - 变换 viewangles。
   - 清 ground。
   - 修正 duck。
   - 设置最小出口速度。

### 第二阶段：官方 holy wall / tube 方案

目标：门洞附近碰撞更接近官方。

实现内容：

- 用 portal basis 构造简化 tube。
- 对玩家 hull 做几何 trace，不一定立即引入 polyhedron。
- 处理玩家卡入 portal collision object 时的 unstuck。

### 第三阶段：完整碰撞裁剪

目标：接近 Portal SDK 2013 的 `PortalSimulation`。

实现内容：

- world brush 裁剪。
- static prop 裁剪。
- linked world collision 变换到 local。
- portal simulator collision entity。
- physics collision filter。

### 第四阶段：其他实体与 VPhysics

目标：支持 props、投掷物、特殊实体穿越。

实现内容：

- VPhysics ownership。
- shadow clone。
- held object 处理。
- physics contact 更新。
- 多实体 collision rules。

## 可精简但不可改变的部分

可以精简：

- 暂不支持非玩家实体。
- 暂不支持 static props 精确裁剪。
- 暂不支持 VPhysics shadow clone。
- 暂不支持 multiplayer 预测同步。
- 暂不支持复杂 grabbed object / physcannon 逻辑。

不建议改变：

- 玩家必须能进入 portal environment。
- 传送必须由“中心越过 portal plane + hull 在 portal hole 内”触发。
- movement trace 必须 portal-aware。
- 位置、角度、速度必须用同一个 this-to-linked matrix 变换。
- 出口必须接管玩家，避免刚传送后立刻脱离 portal touch。
- 视角必须做连续性修正。

## 最小玩家穿越伪代码

```cpp
void PortalThinkOrMoveHook(Player* player)
{
    Portal* portal = FindTouchedOrNearbyPortal(player);
    if (!portal || !portal->IsActiveAndLinked())
    {
        ReleasePortalEnvironment(player);
        return;
    }

    if (PlayerCanSharePortalEnvironment(player, portal))
    {
        TakePortalEnvironment(player, portal);
    }

    if (ShouldTeleportPlayer(player, portal))
    {
        TeleportPlayerThroughPortal(player, portal, portal->linked);
    }
}

bool ShouldTeleportPlayer(Player* player, Portal* portal)
{
    if (player->portalEnvironment != portal)
        return false;

    Vector center = WorldSpaceCenter(player);
    float dist = Dot(portal->plane.normal, center) - portal->plane.dist;

    if (dist >= 0.0f)
        return false;

    return PlayerHullIsInsidePortalHole(player, portal);
}

void TeleportPlayerThroughPortal(Player* player, Portal* entry, Portal* exit)
{
    Matrix mat = entry->thisToLinked;

    Vector oldOrigin = player->GetAbsOrigin();
    Vector oldCenter = WorldSpaceCenter(player);
    Vector oldVelocity = player->GetAbsVelocity();
    QAngle oldEyeAngles = player->EyeAngles();

    Vector newOrigin = mat * oldCenter;
    newOrigin += oldOrigin - oldCenter;

    Vector newVelocity = mat.ApplyRotation(oldVelocity);
    QAngle newEyeAngles = TransformAnglesToWorldSpace(oldEyeAngles, mat);

    ApplyPortalVelocityRules(entry, exit, newVelocity);

    ReleasePortalEnvironment(player, entry);
    TakePortalEnvironment(player, exit);

    player->SetGroundEntity(NULL);
    player->Teleport(&newOrigin, &newEyeAngles, &newVelocity);
    player->SetViewAngles(newEyeAngles);
}
```

## 当前最关键的下一步

下一步不应先做完整 polyhedron 裁剪，而应先验证玩家-only 主链：

1. 在 L4D2 中确认 `TracePlayerBBox` hook 参数、调用时机和稳定性。
2. 确认可安全设置玩家服务端 origin、velocity、viewangles。
3. 做一个最小 portal environment 状态机。
4. 实现玩家 hull 与 portal aperture/tube 的几何判定。
5. 在门洞内让 movement trace 不撞原墙体。
6. 实现中心越过 plane 后的 matrix teleport。
7. 用 localserver 进入地图验证：
   - 玩家能半截进入门洞。
   - 玩家能停在门洞附近。
   - 玩家能穿过并从 linked portal 出来。
   - 出口视角和速度连续。
   - 不出现卡墙、抖动、反复传送。

## 结论

官方 Portal 的穿越逻辑本质是：

```text
portal environment + special collision + movement trace rewrite + plane crossing teleport
```

其中 `Teleport` 只是最后一步。真正让“穿越”成立的是玩家在传送前已经进入一套特殊碰撞环境，movement trace 能把门洞当作可进入空间处理。

因此，L4D2 注入式 DLL 的移植重点应从 `TeleportPlayer` 扩展为完整的玩家 portal transition pipeline。第一阶段只做玩家-only 是合理的，但主体方案仍应保留官方的核心结构。
