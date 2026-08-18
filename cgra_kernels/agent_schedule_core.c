/* CGRA kernel slice: dependency-aware CPU/GPU placement benchmark. */
/* Reference slice: reference/agent_workflow_schedule/source_excerpt.md. */
/* Output layout: resource[0..5], finish[6..11], order[12..17], counters[18..21]. */
#define TASK_COUNT 6
#define RESOURCE_CPU 0
#define RESOURCE_GPU 1
#define GPU_MEMORY_CAPACITY 6

int agent_schedule_core(const int *predecessor_count,
                        const int *dependency,
                        const int *cpu_cost,
                        const int *gpu_cost,
                        const int *gpu_memory,
                        int *out)
{
    int predecessor[TASK_COUNT];
    int dependency_ready[TASK_COUNT];
    int ready[2] = {0, 0};
    int done = 0;
    int gpu_ineligible_evaluations = 0;
    int released_edges = 0;
    int deadlock = 0;
    int i;

    for (i = 0; i < TASK_COUNT; ++i) {
        predecessor[i] = predecessor_count[i];
        dependency_ready[i] = 0;
        out[i] = -1;
        out[TASK_COUNT + i] = -1;
        out[2 * TASK_COUNT + i] = -1;
    }
    while (done < TASK_COUNT && deadlock == 0) {
        int task;
        int best = -1;
        int best_resource = RESOURCE_CPU;
        int best_finish = 0x7fffffff;

        for (task = 0; task < TASK_COUNT; ++task) {
            if (out[task] < 0 && predecessor[task] == 0) {
                int gpu_allowed = gpu_memory[task] <= GPU_MEMORY_CAPACITY;
                int cpu_start = ready[RESOURCE_CPU];
                int gpu_start = ready[RESOURCE_GPU];
                int cpu_finish;
                int gpu_finish;
                int selected = RESOURCE_CPU;
                int candidate_finish;

                if (cpu_start < dependency_ready[task]) {
                    cpu_start = dependency_ready[task];
                }
                if (gpu_start < dependency_ready[task]) {
                    gpu_start = dependency_ready[task];
                }
                cpu_finish = cpu_start + cpu_cost[task];
                gpu_finish = gpu_start + gpu_cost[task];
                candidate_finish = cpu_finish;

                if (gpu_allowed != 0 && gpu_finish <= cpu_finish) {
                    selected = RESOURCE_GPU;
                    candidate_finish = gpu_finish;
                } else if (gpu_allowed == 0) {
                    gpu_ineligible_evaluations++;
                }
                if (best < 0 || candidate_finish < best_finish ||
                    (candidate_finish == best_finish && task < best)) {
                    best = task;
                    best_resource = selected;
                    best_finish = candidate_finish;
                }
            }
        }
        if (best < 0) {
            deadlock = 1;
        } else {
            int successor;

            out[best] = best_resource;
            out[TASK_COUNT + best] = best_finish;
            ready[best_resource] = best_finish;
            out[2 * TASK_COUNT + done] = best;
            done++;
            for (successor = 0; successor < TASK_COUNT; ++successor) {
                if (dependency[best * TASK_COUNT + successor] != 0 &&
                    predecessor[successor] > 0) {
                    predecessor[successor]--;
                    if (dependency_ready[successor] < best_finish) {
                        dependency_ready[successor] = best_finish;
                    }
                    released_edges++;
                }
            }
        }
    }
    out[18] = done;
    out[19] = gpu_ineligible_evaluations;
    out[20] = released_edges;
    out[21] = deadlock;
    return done == TASK_COUNT && deadlock == 0;
}
