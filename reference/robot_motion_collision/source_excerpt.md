# Robot Motion Collision Reference

## 来源与边界

- [Energy-Efficient Realtime Motion Planning](https://doi.org/10.1145/3579371.3589092)：
  提供 collision detection 的 CPU 时间占比、重复查询和 early-exit 动机。
- [VAMP](https://arxiv.org/abs/2309.14545) 及其
  [公开实现](https://github.com/KavrakiLab/vamp)：提供 CPU motion validation、collision
  checking 和公开 MotionBenchMaker 场景的实现依据。

来源证明碰撞检查是机器人运动规划中的真实 CPU 路径。二维坐标、矩形障碍物和固定采样数
是本 benchmark 的确定测试模型，不是对 VAMP 或论文几何算法的复制。

## 输入与判定

- 6 条 start-to-goal edge；每条 edge 采样 5 个点；
- 工作空间为闭区间 `[-8,8] x [-8,8]`；
- 3 个 axis-aligned rectangle obstacle，矩形边界计为碰撞；
- sample 坐标使用 C 整数线性插值。

每个 sample 先检查工作空间边界，再扫描 obstacle。首次越界或命中 obstacle 后，该 edge
立即变为 invalid，后续固定循环不再更新它。输出每条 edge 的 valid、collision、
samples_checked，以及 valid/invalid 和分支汇总计数。

## 不在契约内

Forward kinematics、机器人连杆几何、BVH/GJK、RRT/PRM、nearest-neighbor、trajectory
smoothing、ROS/Nav2、控制器和执行器均不在当前 reference 行为内。
