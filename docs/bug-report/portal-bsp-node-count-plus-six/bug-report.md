# Portal BSP node 数量关系误判

## 诊断胶囊

| 栏位 | 内容 |
|---|---|
| 现象 | 两张地图的 16 次只读 snapshot 全部 `ready=false`；唯一失败项是 nodes 的两个数量字段不相等。预期是已证明的布局能够就绪。 |
| 证据 | `ssv1` 为 `549/555`，`c1m2_streets` 为 `4165/4171`，始终固定相差 6；其他六张表、root、leaf、cmodel 和 brush API 交叉验证全部通过。 |
| 根因 | `+0x88` 是地图逻辑 `numnodes`，`+0x90` 是 `CRangeValidatedArray` 的实际分配数量。引擎为 box hull 额外分配 6 个 `cnode_t`，因此正确关系是 `allocated == logical + 6`，不是相等。 |
| 诊断策略 | 用纯单元测试复现 `549/555`；验证 `+6` 成功、`+5/+7` 失败，并证明完整可读跨度按分配数量乘以 12 计算。 |
| 超时策略 | 若修正后仍未就绪，停止扩大修改，逐项比较新日志中的 `countRelation`、跨度和唯一失败表。 |
| 预警策略 | 任一非 nodes 表变为失败，或 nodes 在两图不再固定相差 6，说明关系建模错误，禁止放宽校验。 |
| 用户可见修正 | 视觉和碰撞行为不变；日志中的 Stage A snapshot 应由 `ready=false` 变为 `ready=true`。 |
| 验收 | 新增失败测试先红后绿；全部独立测试通过；Debug x86 构建通过；两张地图实机日志中所有 snapshot `ready=true`，且仍然无法穿越。 |

## 静态依据

- Windows `sub_14B0B0` 证明 `cnode_t` 步长为 12，字段为 `plane* + children[2]`。
- 两张目标地图的运行时数量都严格满足 `validated = canonical + 6`。
- 参考引擎源码 `CollisionBSPData_LoadNodes` 明确执行 `map_nodes.Attach(count + 6, ...)`，同时令 `numnodes = count`。
