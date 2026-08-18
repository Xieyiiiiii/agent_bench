# Scheduling 与 Robotics 代码实现总结

## 1. 实现层次

两类 workload 均保留两份 C 实现：

- `src/` 是可读的 host reference，用具名函数定义算法阶段，负责初始化、运行、打印和
  checksum；
- `cgra_kernels/` 是受当前编译器和容量约束的单函数版本，只接收数组输入并写回结果。

论文与公开代码确定 workload 在真实系统中的位置；`reference/*/source_excerpt.md` 定义
本项目的确定行为；host reference 和 CGRA kernel 都必须匹配该行为契约。

## 2. Agent workflow scheduling

### 2.1 固定测试数据

测试包含 6 个任务，依赖关系为：

```text
0 -> 1, 2
1 -> 3, 4
2 -> 4
3, 4 -> 5
```

| 任务 | CPU cost | GPU cost | GPU memory | 初始前驱数 |
| --- | ---: | ---: | ---: | ---: |
| 0 | 4 | 3 | 2 | 0 |
| 1 | 7 | 4 | 3 | 1 |
| 2 | 3 | 5 | 5 | 1 |
| 3 | 6 | 3 | 4 | 1 |
| 4 | 5 | 2 | 7 | 2 |
| 5 | 4 | 3 | 2 | 2 |

GPU memory capacity 固定为 6，因此任务 4 不能使用 GPU。cost 和 memory 是无单位的
确定性测试值，不是论文 profile 的复制。

### 2.2 调度规则

任务只有在所有前驱都已被调度后才进入 ready 集合。对 ready 任务 `t`，先计算其依赖
允许的最早开始时刻：

```text
dependency_ready(t) = max(finish(p))，p 为 t 的前驱
start(t, r) = max(dependency_ready(t), resource_ready(r))
finish(t, r) = start(t, r) + cost(t, r)
```

GPU memory 超过容量时，GPU 候选无效；否则在 CPU 与 GPU 完成时刻相同时选择 GPU。
全部 ready 候选中选择完成时刻最早者，仍相同时选择 task id 较小者。选中任务后更新
资源时钟，并向后继传播该任务的完成时刻。

`GPU_INELIGIBLE_EVALUATIONS` 统计 ready 候选评估中显存资格失败的次数。它不表示任务数，
也不表示 GPU 排队次数。

### 2.3 Host 函数链

```text
main
  -> init_data
  -> reset_schedule
  -> run_schedule
       -> choose_next_task
            -> evaluate_candidate
       -> record_schedule
       -> release_successors
  -> print_result
  -> checksum_result
```

数据结构只分为 `ScheduleInput`、`ScheduleState` 和 `ScheduleCounters`。其中
`ScheduleState` 同时保存运行所需的前驱状态、资源时钟和输出结果，不再使用重复的
task-state 数组。

### 2.4 预期结果

```text
SCHEDULE_ORDER=0,2,1,3,4,5
RESOURCE=1,1,0,1,0,1
FINISH=3,7,6,10,12,15
GPU_INELIGIBLE_EVALUATIONS=2
RELEASED_EDGES=7
DEADLOCK=0
```

这里的 `SCHEDULE_ORDER` 是确定性 list scheduler 选中任务的顺序，不应称为真实并发系统
中的完成事件顺序。

## 3. Robot motion collision

### 3.1 固定测试数据与判定规则

测试包含 6 条二维运动边、每条边 5 个等间隔采样点和 3 个闭区间矩形障碍物。工作空间
边界为 `[-8, 8] x [-8, 8]`。采样使用 C 整数运算：

```text
x = start_x + (goal_x - start_x) * sample / 4
y = start_y + (goal_y - start_y) * sample / 4
```

每个采样点先检查工作空间边界，再检查障碍物。矩形端点属于障碍物，即四个比较使用
`>=` 和 `<=`。首次越界或命中障碍物后，该 edge 被标记为 invalid，后续 sample 不再
更新该 edge 的结果。

### 3.2 Host 函数链

```text
main
  -> init_data
  -> reset_result
  -> run_kernel
       -> check_edge
            -> point_in_obstacle
  -> print_result
  -> checksum_result
```

数据结构只分为 `CollisionInput`、`CollisionResult` 和 `CollisionCounters`。它们分别保存
固定几何输入、逐 edge 结果和汇总分支计数。

### 3.3 预期结果

```text
VALID=0,1,1,0,1,1
COLLISION=1,0,0,1,0,0
SAMPLES=1,5,5,4,5,5
VALID_EDGES=4
INVALID_EDGES=2
OUT_OF_BOUNDS=1
OBSTACLE_HITS=1
EARLY_EXIT_EDGES=2
```

## 4. 单函数版本的简化过程

当前编译器前端不能处理函数调用、`continue` 或 `break`；加速器程序也不使用标准 I/O
或动态内存。项目把 150 条反汇编指令作为当前 practical target，并要求 call-like
instruction 为 0。这些是当前工具链和项目的实现约束，不是 CGRA 架构的一般性质。

### 4.1 Scheduling 的拆分与压缩

1. 保留的子任务：ready 判断、前驱最晚完成时刻传播、CPU/GPU 资格与完成时刻比较、
   task tie-break、资源时钟更新和后继释放。
2. 展开的函数：`evaluate_candidate`、`choose_next_task`、`record_schedule` 和
   `release_successors` 在单函数中按相同顺序展开。
3. 删除的系统内容：异步队列、线程、锁、真实 GPU 调用、KV cache、在线 profile 和
   多 workflow admission。
4. 数据搬运压缩：正确性修正后的直接版本为 155 条指令；随后取消本地 `resource`、
   `finish`、`schedule_order` 三组副本，直接写入输出数组；同时将 deadlock 时未生成的
   三段结果统一初始化为 `-1`。最终为 133 条。该修改不删除调度分支，也不改变正常结果。

当前无需拆成多个文件，因为完整的确定行为已低于 150 条目标。

### 4.2 Collision 的拆分与压缩

1. 保留的子任务：edge 遍历、sample 插值、边界分支、obstacle 遍历、命中失效和结果汇总。
2. 展开的函数：`point_in_obstacle` 和 `check_edge` 在单函数中展开。
3. 删除的系统内容：forward kinematics、机器人连杆几何、BVH/GJK、planner 外层、ROS 和
   控制器。
4. 控制流改写：用 `active` 状态阻止 invalid edge 的后续更新，以满足当前前端不支持
   `break`/`continue` 的限制；固定 sample 循环仍会运行。

该单函数当前为 98 条指令，无需继续缩减或拆分。

## 5. 行为一致性与验证

`tests/check_outputs.sh` 对两份 host reference 检查完整的逐项结果；
`tests/check_cgra_behavior.sh` 使用相同自拟数据集调用两个单函数版本，并逐项检查 resource、
finish、schedule order、valid、collision 和 samples；它还使用无 ready task 的输入检查
deadlock 防护。`scripts/count_instructions.sh` 负责反汇编指令数与 call-like instruction
审查。

当前契约只证明固定 benchmark 输入上的算法结果一致，不证明已经实现 MARS、论文中的
Agentic Scheduler、VAMP 或完整机器人运动规划器，也不把单函数上的加速比直接外推为
端到端系统加速比。
