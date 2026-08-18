# Robot Motion Collision Analysis

The benchmark checks fixed-sample two-dimensional motion edges against a small
set of axis-aligned obstacles. It preserves the representative CPU control
flow of state validity checking: sample traversal, boundary rejection,
obstacle loops, invalid-state early exit, and per-edge counters.

The host call chain is `init_data -> reset_result -> run_kernel`, with
`check_edge` and `point_in_obstacle` as process steps. The CGRA slice expands
the obstacle predicate inside one function and returns per-edge validity,
collision, sample counts, and branch counters. It is not a complete forward
kinematics, BVH, sampling planner, or controller implementation.
