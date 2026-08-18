/*
 * Benchmark: robot_motion_collision
 *
 * Reference archive: reference/robot_motion_collision/.
 *
 * This deterministic reference checks fixed-sample 2-D motion edges against
 * rectangular obstacles. It represents one edge-validity process and does not
 * claim to implement FK, BVH, RRT, ROS, or a controller stack.
 *
 * Benchmark-only extensions: fixed two-dimensional edges, sample count and
 * rectangular obstacle set.
 * Simplifications: integer linear interpolation and point-versus-rectangle
 * collision in place of robot-link geometry and forward kinematics.
 * Not implemented: forward kinematics, BVH/GJK, RRT/PRM, ROS/Nav2, controller
 * and actuator execution.
 */

#include <stdint.h>
#include <stdio.h>

#include "checksum.h"

#define EDGE_COUNT 6
#define SAMPLE_COUNT 5
#define OBSTACLE_COUNT 3

typedef struct {
    int start_x[EDGE_COUNT];
    int start_y[EDGE_COUNT];
    int goal_x[EDGE_COUNT];
    int goal_y[EDGE_COUNT];
    int obstacle_x0[OBSTACLE_COUNT];
    int obstacle_y0[OBSTACLE_COUNT];
    int obstacle_x1[OBSTACLE_COUNT];
    int obstacle_y1[OBSTACLE_COUNT];
} CollisionInput;

typedef struct {
    int valid[EDGE_COUNT];
    int collision[EDGE_COUNT];
    int samples_checked[EDGE_COUNT];
    int valid_edges;
    int invalid_edges;
} CollisionResult;

typedef struct {
    int out_of_bounds;
    int obstacle_hits;
    int early_exit_edges;
} CollisionCounters;

static void init_data(CollisionInput *input)
{
    const int start_x[EDGE_COUNT] = {9, -8, 8, -7, -6, 5};
    const int start_y[EDGE_COUNT] = {-8, 7, -7, -6, 6, -6};
    const int goal_x[EDGE_COUNT] = {8, 8, -8, 7, 6, -5};
    const int goal_y[EDGE_COUNT] = {8, -7, 7, 6, -6, 6};
    const int obstacle_x0[OBSTACLE_COUNT] = {-2, 2, -1};
    const int obstacle_y0[OBSTACLE_COUNT] = {-7, -1, 3};
    const int obstacle_x1[OBSTACLE_COUNT] = {2, 4, 1};
    const int obstacle_y1[OBSTACLE_COUNT] = {-3, 3, 7};
    int i;

    for (i = 0; i < EDGE_COUNT; ++i) {
        input->start_x[i] = start_x[i];
        input->start_y[i] = start_y[i];
        input->goal_x[i] = goal_x[i];
        input->goal_y[i] = goal_y[i];
    }
    for (i = 0; i < OBSTACLE_COUNT; ++i) {
        input->obstacle_x0[i] = obstacle_x0[i];
        input->obstacle_y0[i] = obstacle_y0[i];
        input->obstacle_x1[i] = obstacle_x1[i];
        input->obstacle_y1[i] = obstacle_y1[i];
    }
}

static void reset_result(CollisionResult *result,
                         CollisionCounters *counters)
{
    int i;

    for (i = 0; i < EDGE_COUNT; ++i) {
        result->valid[i] = 0;
        result->collision[i] = 0;
        result->samples_checked[i] = 0;
    }
    result->valid_edges = 0;
    result->invalid_edges = 0;
    counters->out_of_bounds = 0;
    counters->obstacle_hits = 0;
    counters->early_exit_edges = 0;
}

static int point_in_obstacle(const CollisionInput *input, int x, int y)
{
    int obstacle;

    for (obstacle = 0; obstacle < OBSTACLE_COUNT; ++obstacle) {
        if (x >= input->obstacle_x0[obstacle] &&
            x <= input->obstacle_x1[obstacle] &&
            y >= input->obstacle_y0[obstacle] &&
            y <= input->obstacle_y1[obstacle]) {
            return 1;
        }
    }
    return 0;
}

static void check_edge(const CollisionInput *input, CollisionResult *result,
                       CollisionCounters *counters, int edge)
{
    int sample;
    int collision = 0;
    int active = 1;

    for (sample = 0; sample < SAMPLE_COUNT; ++sample) {
        if (active != 0) {
            int x = input->start_x[edge] +
                    (input->goal_x[edge] - input->start_x[edge]) * sample /
                    (SAMPLE_COUNT - 1);
            int y = input->start_y[edge] +
                    (input->goal_y[edge] - input->start_y[edge]) * sample /
                    (SAMPLE_COUNT - 1);
            int out = x < -8 || x > 8 || y < -8 || y > 8;

            result->samples_checked[edge]++;
            if (out) {
                counters->out_of_bounds++;
                collision = 1;
                active = 0;
            } else if (point_in_obstacle(input, x, y) != 0) {
                counters->obstacle_hits++;
                collision = 1;
                active = 0;
            }
        }
    }
    result->collision[edge] = collision;
    result->valid[edge] = collision == 0;
    if (collision == 0) {
        result->valid_edges++;
    } else {
        result->invalid_edges++;
        if (result->samples_checked[edge] < SAMPLE_COUNT) {
            counters->early_exit_edges++;
        }
    }
}

static void run_kernel(const CollisionInput *input, CollisionResult *result,
                       CollisionCounters *counters)
{
    int edge;

    for (edge = 0; edge < EDGE_COUNT; ++edge) {
        check_edge(input, result, counters, edge);
    }
}

static uint32_t checksum_result(const CollisionResult *result,
                                const CollisionCounters *counters)
{
    uint32_t checksum = 2166136261u;
    int i;

    for (i = 0; i < EDGE_COUNT; ++i) {
        checksum = checksum_mix(checksum, result->valid[i]);
        checksum = checksum_mix(checksum, result->collision[i]);
        checksum = checksum_mix(checksum, result->samples_checked[i]);
    }
    checksum = checksum_mix(checksum, counters->out_of_bounds);
    checksum = checksum_mix(checksum, counters->obstacle_hits);
    checksum = checksum_mix(checksum, counters->early_exit_edges);
    return checksum;
}

static void print_result(const CollisionResult *result,
                         const CollisionCounters *counters)
{
    int i;

    printf("KERNEL=robot_motion_collision\n");
    printf("VALID=");
    for (i = 0; i < EDGE_COUNT; ++i) {
        printf("%s%d", i == 0 ? "" : ",", result->valid[i]);
    }
    printf("\nCOLLISION=");
    for (i = 0; i < EDGE_COUNT; ++i) {
        printf("%s%d", i == 0 ? "" : ",", result->collision[i]);
    }
    printf("\nSAMPLES=");
    for (i = 0; i < EDGE_COUNT; ++i) {
        printf("%s%d", i == 0 ? "" : ",", result->samples_checked[i]);
    }
    printf("\nVALID_EDGES=%d\n", result->valid_edges);
    printf("INVALID_EDGES=%d\n", result->invalid_edges);
    printf("OUT_OF_BOUNDS=%d\n", counters->out_of_bounds);
    printf("OBSTACLE_HITS=%d\n", counters->obstacle_hits);
    printf("EARLY_EXIT_EDGES=%d\n", counters->early_exit_edges);
}

int main(void)
{
    CollisionInput input;
    CollisionResult result;
    CollisionCounters counters;

    init_data(&input);
    reset_result(&result, &counters);
    run_kernel(&input, &result, &counters);
    print_result(&result, &counters);
    printf("CHECKSUM=%u\n", checksum_result(&result, &counters));
    return 0;
}
