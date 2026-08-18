/*
 * Benchmark: agent_workflow_schedule
 *
 * Reference archive: reference/agent_workflow_schedule/.
 *
 * This deterministic list scheduler models dependency release, CPU/GPU
 * placement, GPU-memory eligibility, and resource availability. It is a
 * standalone reference for those state transitions, not a complete Agent
 * runtime or an implementation of either cited scheduling system.
 *
 * Benchmark-only extensions: fixed six-task DAG and deterministic profiles.
 * Simplifications: static integer costs, one CPU clock, one GPU clock, and no
 * runtime contention model beyond GPU-memory eligibility.
 * Not implemented: LLM execution, asynchronous queues, a GPU runtime,
 * admission control, or multi-workflow concurrency.
 */

#include <stdint.h>
#include <stdio.h>

#include "checksum.h"

#define TASK_COUNT 6
#define RESOURCE_COUNT 2
#define RESOURCE_CPU 0
#define RESOURCE_GPU 1
#define GPU_MEMORY_CAPACITY 6

typedef struct {
    int cpu_cost[TASK_COUNT];
    int gpu_cost[TASK_COUNT];
    int gpu_memory[TASK_COUNT];
    int dependency[TASK_COUNT * TASK_COUNT];
    int predecessor_count[TASK_COUNT];
} ScheduleInput;

typedef struct {
    int predecessor_remaining[TASK_COUNT];
    int dependency_ready[TASK_COUNT];
    int resource[TASK_COUNT];
    int finish[TASK_COUNT];
    int resource_ready[RESOURCE_COUNT];
    int schedule_order[TASK_COUNT];
    int scheduled_count;
} ScheduleState;

typedef struct {
    int gpu_ineligible_evaluations;
    int cpu_assignments;
    int gpu_assignments;
    int ready_candidates;
    int released_edges;
    int deadlock;
} ScheduleCounters;

static void init_data(ScheduleInput *input)
{
    const int dependency[TASK_COUNT * TASK_COUNT] = {
        0, 1, 1, 0, 0, 0,
        0, 0, 0, 1, 1, 0,
        0, 0, 0, 0, 1, 0,
        0, 0, 0, 0, 0, 1,
        0, 0, 0, 0, 0, 1,
        0, 0, 0, 0, 0, 0
    };
    const int predecessor_count[TASK_COUNT] = {0, 1, 1, 1, 2, 2};
    const int cpu_cost[TASK_COUNT] = {4, 7, 3, 6, 5, 4};
    const int gpu_cost[TASK_COUNT] = {3, 4, 5, 3, 2, 3};
    const int gpu_memory[TASK_COUNT] = {2, 3, 5, 4, 7, 2};
    int task;
    int successor;

    for (task = 0; task < TASK_COUNT; ++task) {
        input->cpu_cost[task] = cpu_cost[task];
        input->gpu_cost[task] = gpu_cost[task];
        input->gpu_memory[task] = gpu_memory[task];
        input->predecessor_count[task] = predecessor_count[task];
        for (successor = 0; successor < TASK_COUNT; ++successor) {
            input->dependency[task * TASK_COUNT + successor] =
                dependency[task * TASK_COUNT + successor];
        }
    }
}

static void reset_schedule(const ScheduleInput *input, ScheduleState *state,
                           ScheduleCounters *counters)
{
    int task;

    for (task = 0; task < TASK_COUNT; ++task) {
        state->predecessor_remaining[task] = input->predecessor_count[task];
        state->dependency_ready[task] = 0;
        state->resource[task] = -1;
        state->finish[task] = -1;
        state->schedule_order[task] = -1;
    }
    state->resource_ready[RESOURCE_CPU] = 0;
    state->resource_ready[RESOURCE_GPU] = 0;
    state->scheduled_count = 0;
    counters->gpu_ineligible_evaluations = 0;
    counters->cpu_assignments = 0;
    counters->gpu_assignments = 0;
    counters->ready_candidates = 0;
    counters->released_edges = 0;
    counters->deadlock = 0;
}

static void evaluate_candidate(const ScheduleInput *input,
                               const ScheduleState *state,
                               ScheduleCounters *counters, int task,
                               int *resource, int *finish)
{
    int cpu_start = state->resource_ready[RESOURCE_CPU];
    int gpu_start = state->resource_ready[RESOURCE_GPU];
    int cpu_finish;
    int gpu_finish;

    if (cpu_start < state->dependency_ready[task]) {
        cpu_start = state->dependency_ready[task];
    }
    if (gpu_start < state->dependency_ready[task]) {
        gpu_start = state->dependency_ready[task];
    }
    cpu_finish = cpu_start + input->cpu_cost[task];
    gpu_finish = gpu_start + input->gpu_cost[task];
    *resource = RESOURCE_CPU;
    *finish = cpu_finish;
    if (input->gpu_memory[task] <= GPU_MEMORY_CAPACITY) {
        if (gpu_finish <= cpu_finish) {
            *resource = RESOURCE_GPU;
            *finish = gpu_finish;
        }
    } else {
        counters->gpu_ineligible_evaluations++;
    }
}

static int choose_next_task(const ScheduleInput *input,
                            const ScheduleState *state,
                            ScheduleCounters *counters,
                            int *selected_resource, int *selected_finish)
{
    int task;
    int best_task = -1;
    int best_resource = RESOURCE_CPU;
    int best_finish = 0x7fffffff;

    for (task = 0; task < TASK_COUNT; ++task) {
        if (state->resource[task] < 0 &&
            state->predecessor_remaining[task] == 0) {
            int resource;
            int finish;

            counters->ready_candidates++;
            evaluate_candidate(input, state, counters, task, &resource,
                               &finish);
            if (best_task < 0 || finish < best_finish ||
                (finish == best_finish && task < best_task)) {
                best_task = task;
                best_resource = resource;
                best_finish = finish;
            }
        }
    }
    *selected_resource = best_resource;
    *selected_finish = best_finish;
    return best_task;
}

static void record_schedule(ScheduleState *state, ScheduleCounters *counters,
                            int task, int resource, int finish)
{
    state->resource[task] = resource;
    state->finish[task] = finish;
    state->resource_ready[resource] = finish;
    state->schedule_order[state->scheduled_count] = task;
    state->scheduled_count++;
    if (resource == RESOURCE_GPU) {
        counters->gpu_assignments++;
    } else {
        counters->cpu_assignments++;
    }
}

static void release_successors(const ScheduleInput *input,
                               ScheduleState *state,
                               ScheduleCounters *counters, int task)
{
    int successor;

    for (successor = 0; successor < TASK_COUNT; ++successor) {
        if (input->dependency[task * TASK_COUNT + successor] != 0 &&
            state->predecessor_remaining[successor] > 0) {
            state->predecessor_remaining[successor]--;
            if (state->dependency_ready[successor] < state->finish[task]) {
                state->dependency_ready[successor] = state->finish[task];
            }
            counters->released_edges++;
        }
    }
}

static void run_schedule(const ScheduleInput *input, ScheduleState *state,
                         ScheduleCounters *counters)
{
    while (state->scheduled_count < TASK_COUNT && counters->deadlock == 0) {
        int resource;
        int finish;
        int task = choose_next_task(input, state, counters, &resource,
                                    &finish);

        if (task < 0) {
            counters->deadlock = 1;
        } else {
            record_schedule(state, counters, task, resource, finish);
            release_successors(input, state, counters, task);
        }
    }
}

static uint32_t checksum_result(const ScheduleState *state,
                                const ScheduleCounters *counters)
{
    uint32_t checksum = 2166136261u;
    int task;

    for (task = 0; task < TASK_COUNT; ++task) {
        checksum = checksum_mix(checksum, state->schedule_order[task]);
        checksum = checksum_mix(checksum, state->resource[task]);
        checksum = checksum_mix(checksum, state->finish[task]);
    }
    checksum = checksum_mix(checksum,
                            counters->gpu_ineligible_evaluations);
    checksum = checksum_mix(checksum, counters->released_edges);
    checksum = checksum_mix(checksum, counters->deadlock);
    return checksum;
}

static void print_result(const ScheduleState *state,
                         const ScheduleCounters *counters)
{
    int task;

    printf("KERNEL=agent_workflow_schedule\n");
    printf("SCHEDULE_ORDER=");
    for (task = 0; task < TASK_COUNT; ++task) {
        printf("%s%d", task == 0 ? "" : ",", state->schedule_order[task]);
    }
    printf("\nRESOURCE=");
    for (task = 0; task < TASK_COUNT; ++task) {
        printf("%s%d", task == 0 ? "" : ",", state->resource[task]);
    }
    printf("\nFINISH=");
    for (task = 0; task < TASK_COUNT; ++task) {
        printf("%s%d", task == 0 ? "" : ",", state->finish[task]);
    }
    printf("\nGPU_INELIGIBLE_EVALUATIONS=%d\n",
           counters->gpu_ineligible_evaluations);
    printf("CPU_ASSIGNMENTS=%d\n", counters->cpu_assignments);
    printf("GPU_ASSIGNMENTS=%d\n", counters->gpu_assignments);
    printf("READY_CANDIDATES=%d\n", counters->ready_candidates);
    printf("RELEASED_EDGES=%d\n", counters->released_edges);
    printf("DEADLOCK=%d\n", counters->deadlock);
}

int main(void)
{
    ScheduleInput input;
    ScheduleState state;
    ScheduleCounters counters;

    init_data(&input);
    reset_schedule(&input, &state, &counters);
    run_schedule(&input, &state, &counters);
    print_result(&state, &counters);
    printf("CHECKSUM=%u\n", checksum_result(&state, &counters));
    return 0;
}
