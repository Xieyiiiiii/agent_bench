# Scheduling 与 Robotics Workload 选择依据

## 摘要

Agent 应用的执行过程并不止于一次模型推理。模型生成工具调用或机器人高层动作后，
CPU 仍需维护任务依赖、执行工具、协调计算资源，并在机器人动作执行前完成运动规划与
碰撞检查。本项目据此选择两类 workload：Agent 异构工作流调度和机器人运动碰撞检查。

两者的证据强度并不相同。公开论文已经证明异构调度会显著影响 Agent workflow 的端到端
完成时间，但尚不足以证明调度器计算在所有 Agent 系统中都是主要 CPU 时间热点。因此，
调度 workload 的定位是重要的 CPU 控制环节和待测瓶颈。碰撞检查则有直接的 CPU profile
数据：在论文测量的 MPNet 场景中，CPU collision detection 占总时间 95%，因而具备更强的
CPU 瓶颈证据。

## 1. 选择条件

候选 workload 需要满足三个基本条件：

1. 位于 Agent 或具身 Agent 的真实执行链中，并由 CPU 参与执行；
2. 分别覆盖 scheduling 与 robotics，而不是人为拼接的控制流示例；
3. 优先选择已有 CPU 瓶颈证据，或对端到端执行具有关键影响且可进一步 profile 的环节。

为了形成可复现 benchmark，候选 workload 还应具有明确输入、输出和判定规则。这个要求
只决定后续测试形式，不构成 workload 在真实系统中重要性的证据。

## 2. Agent 异构工作流调度

### 2.1 Agent 执行包含持续的 CPU 工作

MARS 将 Agent workload 概括为多轮 LLM-tool loop：模型在 GPU 上生成工具调用，会话随后
转入 CPU 工具或外部服务，工具结果返回后再恢复模型执行。论文将这种变化分别称为
temporal shift 和 spatial shift，并通过外部控制面协调 GPU inference、CPU tool execution、
admission 和会话恢复。该执行方式说明，Agent runtime 必须在 CPU 上持续维护任务状态和
资源状态，而不是在启动一次 GPU 推理后结束工作。

这一结论由 MARS 的 Figure 1、Section 2.1 和系统设计部分支持。论文还在 OpenHands
部署中验证了 CPU-GPU 协同对完整 Agent 任务完成时间的影响。相关描述见本地论文
`reference_papers/scheduling/mars_co_scheduling.pdf`。

### 2.2 设备选择不能简化为“优先使用 GPU”

《Agentic CPU-GPU Scheduling for Heterogeneous AI Workloads》把工具调用表示为具有前驱
约束的任务图，并为每个工具提供 `cpu`、`gpu_now` 和 `gpu_queue` 三种 placement。Table 1
对 19 个工具的 CPU/GPU 执行时间和显存需求进行测量，结果同时包含 GPU 优先、CPU 优先、
设备差异不明显以及没有 GPU 实现的工具。这说明统一的 GPU-first 策略不能正确覆盖异构
工具集合。

Table 2 的 S5 场景进一步展示了任务间干扰：四个工具全部并发放到 GPU 时，端到端时间为
988 ms；将 `vector_search_x15` 放到 CPU 后，该工具自身变慢，但其余 GPU 工具的竞争减轻，
端到端时间降至 812 ms。最优选择由整个任务图、资源竞争和关键路径共同决定，不能只比较
单个工具的隔离执行时间。

同一论文还把显存容量写入可行性约束。显存不足时，all-GPU placement 不是较慢方案，
而是不可执行方案；调度器必须在立即使用 GPU、排队使用 GPU 和转移到 CPU 之间选择。
这些证据见 `reference_papers/scheduling/agentic_cpu_gpu_scheduling.pdf` 的 Section 2、
Table 1 和 Table 2（PDF 第 2-3 页）。

### 2.3 CPU 端承担的算法过程

上述系统需要 CPU 完成以下状态推进：

```text
任务完成事件
  -> 更新后继任务的依赖状态
  -> 找出依赖已经满足的任务
  -> 检查设备资格、资源占用和预计完成时间
  -> 决定任务及执行位置
  -> 更新资源状态并等待新的完成事件
```

这些操作属于 Agent workflow 的控制面。其数据通常是任务图、资源 profile、队列和状态表，
计算特征包括依赖判断、资格判断、候选比较和状态更新，明显不同于模型推理的张量计算。

现有证据可以支持两项结论：异构调度是 Agent 正确执行所需的 CPU 环节；调度决策能够显著
改变端到端时间。现有证据不能支持“调度器自身始终是主要 CPU hotspot”。因此选择该
workload 的理由是它在 Agent 执行链中的必要性和性能影响，其 CPU 时间占比需要在后续
CPU-CGRA 协同系统中单独测量。

## 3. 机器人运动碰撞检查

### 3.1 具身动作需要运动有效性判断

具身 Agent 输出“移动到目标位置”或“抓取物体”等高层动作后，机器人系统仍需生成可行
轨迹。采样式运动规划器会产生大量候选状态和候选运动边；只有在边界范围内且不与环境
碰撞的状态与运动，才能进入最终路径。因而碰撞检查位于高层决策和低层控制器之间，是
机器人动作落地过程中的必要计算。

EmbodiedBench 和 SafeAgentBench 对高层任务规划、导航或操作以及低层执行的分层，支持
这一 pipeline 位置判断。它们不用于证明碰撞检查的时间占比；时间占比采用专门的运动规划
论文证据。

### 3.2 CPU 瓶颈证据

《Energy-Efficient Realtime Motion Planning》报告，采样式运动规划中约 90% 的执行时间
用于机器人与环境之间的碰撞检测。在该论文进一步测量的 MPNet CPU-GPU 系统中，GPU 上的
神经网络推理占总时间 2%，CPU collision detection 占 95%。这一结果直接表明：即使学习
模型负责生成候选轨迹，主要计算时间仍可能落在 CPU 的几何有效性检查上。

论文同时分析了两类与控制流有关的冗余：已经可以判定碰撞或无碰撞的几何测试若继续执行，
会产生细粒度冗余；某条运动已失效后继续提交相关碰撞查询，会产生粗粒度冗余。因此，
逐层判定和尽早结束无效路径既影响计算量，也决定执行分支。

上述数据适用于论文使用的采样式规划和 MPNet 测例，不能外推为所有机器人系统都具有固定
95% 比例。证据位置为 `reference_papers/robotics/energy_efficient_realtime_motion_planning.pdf`
的 Introduction 和 Section 2（关键 profile 位于 PDF 第 3 页）。

### 3.3 公开实现与公开数据集依据

VAMP 是公开的 CPU SIMD sampling-based motion planning 实现。其代码仓库把 collision
checking、forward kinematics 和 motion validation 分成明确模块，并提供 RRT-Connect、
PRM 等规划器。仓库还提供 MotionBenchMaker 场景资源和多种机器人模型，用于运行可复现
的运动规划实验。VAMP 论文在单 CPU core 上比较这些规划流程，证明碰撞检查和运动验证
并非脱离应用的独立算术示例，而是公开机器人规划软件中的实际执行路径。

公开代码入口：<https://github.com/KavrakiLab/vamp>。本地论文为
`reference_papers/robotics/vamp.pdf`，其方法概述和 MotionBenchMaker 实验分别见 PDF
第 1 页和第 6-7 页。

### 3.4 CPU 端承担的算法过程

运动碰撞检查可概括为：

```text
规划器产生候选状态或运动边
  -> 沿运动边生成待检查状态
  -> 检查关节或空间边界
  -> 计算机器人几何与环境几何的相交关系
  -> 首次确认无效后停止该候选的后续检查
  -> 把 valid/invalid 结果返回规划器
```

这一过程包含重复采样、几何对象遍历、输入相关分支和无效状态传播。与 scheduling 相比，
collision checking 不仅是必要 CPU 环节，而且已有直接的时间占比证据，因此更符合优先
选择 CPU bottleneck 的目标。

## 4. 最终选择

| 选择维度 | Agent workflow scheduling | Motion collision checking |
| --- | --- | --- |
| 系统位置 | LLM/tool 执行之间的任务与资源协调 | 高层机器人动作与轨迹控制之间的运动有效性判断 |
| CPU 工作 | 依赖维护、设备资格、资源状态、placement | 状态采样、边界检查、几何相交、无效传播 |
| 公开依据 | MARS；Agentic CPU-GPU Scheduling | Energy-Efficient Realtime Motion Planning；VAMP 论文与代码 |
| 性能证据 | placement 显著改变端到端时间 | MPNet 测例中 CPU collision detection 占总时间 95% |
| 瓶颈结论 | 必要 CPU 控制环节，CPU 占比待测 | 有直接 profile 支持的 CPU 瓶颈 |

这两个 workload 分别覆盖 Agent scheduling 和 embodied robotics，也代表两种不同的 CPU
控制流：前者通过依赖和资源状态决定下一项任务，后者通过有效性判定决定候选运动能否继续。
它们共同满足“CPU 参与、属于 Agent/robotics 重要流程、具备性能研究价值”的选择目标。

本结论只确定研究对象，不等同于当前 C 程序已经复现论文系统。当前代码实现范围、固定
测试数据、简化项和硬件约束单独记录在 `SCHEDULING_ROBOTICS_CODE_SUMMARY_ZH.md`。
