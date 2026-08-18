# Agent Workflow Scheduling Reference

## 来源与边界

- [Agentic CPU-GPU Scheduling for Heterogeneous AI Workloads](https://arxiv.org/abs/2607.22242)：
  提供 task DAG、异构 placement、GPU memory constraint 和端到端目标的问题定义。
- [MARS](https://arxiv.org/abs/2604.26963)：提供多轮 LLM/tool execution 和 CPU-GPU
  协同调度的系统背景。

论文定义研究问题，不定义本 benchmark 的固定 DAG、cost 或 list-scheduling policy。
下述规则是本项目用于 reference 与 CGRA 逐项对照的确定行为。

## 输入

- 6 个任务；依赖关系为 `0 -> {1,2}`、`1 -> {3,4}`、`2 -> 4`、`{3,4} -> 5`；
- 每个任务各有 CPU cost、GPU cost 和 GPU memory requirement；
- CPU 和 GPU 各自只有一个资源时钟；GPU memory capacity 为 6。

固定 profile 为：

```text
cpu_cost  = 4,7,3,6,5,4
gpu_cost  = 3,4,5,3,2,3
gpu_memory= 2,3,5,4,7,2
```

## 确定调度规则

1. 未调度且剩余前驱数为 0 的任务是 ready candidate。
2. `dependency_ready[t]` 是任务 `t` 所有前驱完成时刻的最大值。
3. 在资源 `r` 上，`start=max(dependency_ready[t], resource_ready[r])`，
   `finish=start+cost[t][r]`。
4. GPU memory 超过容量时 GPU 候选无效；CPU/GPU finish 相同时选择 GPU。
5. 全部 ready candidate 中选择 finish 最小者；仍相同时选择 task id 最小者。
6. 记录任务、资源和 finish，更新对应资源时钟，再减少后继的剩余前驱数，并把当前
   finish 传播到后继的 `dependency_ready`。
7. 若任务未全部调度且不存在 ready candidate，则报告 deadlock。

输出中的 order 是 scheduler 选中任务的顺序，不是并发执行中的真实 completion-event
顺序。`GPU_INELIGIBLE_EVALUATIONS` 统计候选评估期间的 GPU 资格失败次数。

## 不在契约内

线程、异步事件队列、锁、真实 GPU runtime、LLM、KV cache、在线 profile、网络和
多 workflow admission 均不在当前 reference 行为内。
