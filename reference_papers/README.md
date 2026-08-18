# Scheduling 与 Robotics 参考论文清单

本目录记录 workload 选择与瓶颈分析所依据的论文。论文 PDF 由研究者在本地保存，
`reference_papers/**/*.pdf` 默认不纳入 Git；远程仓库只保留来源、建议本地路径和用途。
这样既避免仓库膨胀和论文再分发问题，也保留可复查的证据入口。

## 核心论文

| 类别 | 建议本地路径 | 论文与来源 | 在本项目中的用途 |
| --- | --- | --- | --- |
| Agent scheduling | `scheduling/agentic_cpu_gpu_scheduling.pdf` | [Agentic CPU-GPU Scheduling for Heterogeneous AI Workloads](https://arxiv.org/abs/2607.22242) | tool DAG、CPU/GPU placement、VRAM 与 contention 约束 |
| Agent scheduling | `scheduling/mars_co_scheduling.pdf` | [MARS: Efficient, Adaptive Co-Scheduling for Heterogeneous Agentic Systems](https://arxiv.org/abs/2604.26963) | LLM-tool loop、admission、continuation priority 和 CPU/GPU 协同 |
| Motion planning | `robotics/energy_efficient_realtime_motion_planning.pdf` | [Energy-Efficient Realtime Motion Planning](https://doi.org/10.1145/3579371.3589092) | collision detection CPU profile、early exit 和冗余查询 |
| Motion planning | `robotics/vamp.pdf` | [Motions in Microseconds via Vectorized Sampling-Based Planning](https://arxiv.org/abs/2309.14545) | CPU SIMD、forward kinematics、motion validation 和 collision checking |

## 场景与后续扩展资料

| 类别 | 建议本地路径 | 论文与来源 | 用途 |
| --- | --- | --- | --- |
| Agent execution | `agent_context/cpu_centric_agentic_ai.pdf` | [Agentic AI Execution: A CPU-Centric Perspective](https://arxiv.org/abs/2511.00739) | Agent orchestrator、CPU-side tool execution 和测量边界 |
| Embodied Agent | `agent_context/embodiedbench.pdf` | [EmbodiedBench](https://arxiv.org/abs/2502.09560) | 高层任务到 navigation/manipulation 的执行层次 |
| Embodied Agent | `agent_context/safeagentbench.pdf` | [SafeAgentBench](https://arxiv.org/abs/2412.13178) | task planning、high-level action 和 low-level controller 分层 |
| Dynamic MRTA | `mrta/mrta_sim.pdf` | [MRTA-Sim](https://arxiv.org/abs/2504.15418) | allocation、导航和冲突消解的跨层边界 |
| Dynamic MRTA | `mrta/space_mrta.pdf` | [SPACE](https://arxiv.org/abs/2409.04230) | decentralized allocation、bid、message 和 behavior-tree workload |

## 本地使用

审核时可从上表链接获取对应版本，并按“建议本地路径”放置。PDF 是否存在不影响源码
构建和测试；文档中的页码与结论以项目调研时使用的版本为准。可用以下命令检查本地材料：

```bash
find reference_papers -type f -name '*.pdf' -print
file reference_papers/**/*.pdf
```

提交前运行 `make test`，并使用 `git status` 确认本地 PDF 和生成文件没有进入待提交列表。
