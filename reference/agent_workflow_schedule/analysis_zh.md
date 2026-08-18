# Agent Workflow Scheduling 分析

## CPU 瓶颈分析

公开论文证明了两件事：Agent 的多轮 LLM/tool 执行需要 CPU 侧控制面；异构 placement、
GPU contention 和显存约束会显著改变端到端完成时间。论文尚未证明 scheduler 计算本身
在所有 Agent 系统中都是主要 CPU hotspot。因此，该 workload 的准确定位是“重要的
CPU 控制环节和待 profile 的瓶颈候选”，不能写成已经确认的普遍主瓶颈。

当前 benchmark 保留 ready 判断、前驱完成时刻传播、task×resource finish 比较、GPU
资格分支、资源时钟更新和后继释放。这些状态转移共同决定后续调度，不是普通排序。

## 面向过程实现形态

```text
main
  init_data
  reset_schedule
  run_schedule
    choose_next_task
      evaluate_candidate
    record_schedule
    release_successors
  print_result
  checksum_result
```

`ScheduleInput` 只保存固定 profile 和依赖；`ScheduleState` 保存前驱运行状态、资源时钟和
结果；`ScheduleCounters` 保存可验证分支计数。没有重复的 task-state 数组。

## 行为匹配检查

host 与 CGRA 必须在相同输入上逐项匹配：

- `SCHEDULE_ORDER=0,2,1,3,4,5`；
- `RESOURCE=1,1,0,1,0,1`；
- `FINISH=3,7,6,10,12,15`；
- GPU 资格失败评估次数为 2，释放边数为 7，deadlock 为 0。

完成时刻必须满足 `finish >= max(resource_ready, predecessor_finish) + cost`。旧实现只使用
资源时钟，可能让后继早于前驱完成而开始；该错误已由逐项测试覆盖。输出 order 是
list scheduler 的选择顺序，不是实际并发 completion order。

## 硬件边界

CGRA 版本是单文件单函数，无 I/O、helper call、动态分配、`continue` 和 `break`。固定
6 任务、36 元素依赖矩阵、每轮候选扫描、函数内可变前驱状态和 flat-array 接口都属于
本 benchmark 或当前编译器接口的选择，不是 CGRA 架构特性。

单函数直接写入输出数组以消除本地结果副本，并把 deadlock 时未生成的结果初始化为
`-1`；最终从正确性修正后的 155 条指令降为 133 条，同时保留全部调度分支。150 条是
本项目当前 practical target，不是硬件理论容量
可以直接用于程序指令的保证。
