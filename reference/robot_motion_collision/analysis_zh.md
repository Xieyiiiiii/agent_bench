# Robot Motion Collision 分析

## CPU 瓶颈分析

论文在 MPNet 场景中给出 CPU collision detection 占总时间 95% 的直接 profile，因而
该 workload 有明确的 CPU 瓶颈依据。当前二维 benchmark 保留 edge×sample×obstacle 三层循环、
边界 reject、obstacle hit、invalid 后 active flag 屏蔽和 valid/invalid 统计。完整
机器人系统还会增加 FK、broad/narrow phase 和高维状态访存；当前结果不把这些未实现内容
算入结果。

## 面向过程实现形态

调用链为：

```text
main
  init_data
  reset_result
  run_kernel
    check_edge
      point_in_obstacle
  checksum_result
  print_result
```

固定数组让每条路径可复现，计数器用于确认 obstacle hit 和 early-exit 分支被执行。

## 行为匹配检查

host 与 CGRA 在相同 edge、采样数、边界和 obstacle 输入下，必须对每条 edge 给出相同
的 valid/collision 和 samples_checked。CGRA 使用整数线性插值，避免 trig、sqrt 和
外部库调用。固定数据同时包含一条起点越界 edge 和一条命中 obstacle 的 edge，确保
boundary reject 与 obstacle hit 两类分支均被执行。

## 硬件边界

CGRA 版本是单函数、flat array、无 I/O、无 helper call 的 configuration-space
collision benchmark。二维矩形模型是确定测试数据的选择，不是 CGRA 架构特性，也不是
对完整 RRT 或机器人控制器的实现。
