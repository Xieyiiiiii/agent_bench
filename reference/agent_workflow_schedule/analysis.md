# Agent Workflow Scheduling Analysis

The cited systems establish heterogeneous Agent scheduling as a real CPU-side
control-plane workload. They do not establish the scheduler itself as the
dominant CPU hotspot in every Agent application. This benchmark therefore
models a bottleneck candidate whose end-to-end importance is established but
whose CPU share still requires system-level profiling.

The reference is a deterministic dependency-aware list scheduler. A candidate
cannot start before both its selected resource and all predecessors are ready:
`start=max(resource_ready, dependency_ready)`. GPU eligibility, finish-time
selection, task-id tie-breaking, resource-clock updates, and successor release
are part of the behavior contract.

The host call chain is `init_data -> reset_schedule -> run_schedule`, with
`choose_next_task -> evaluate_candidate`, `record_schedule`, and
`release_successors` as named process steps. The CGRA implementation expands
the same steps into one function and writes resource, finish, schedule order,
and counters directly to the output array.

The fixed six-task graph, matrix representation, ready-candidate scan, local
mutable predecessor state, and flat CGRA interface are benchmark design
choices. None is claimed to be an inherent CGRA property.
