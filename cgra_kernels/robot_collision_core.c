/* CGRA kernel slice: fixed-sample edge collision checking. */
/* Reference slice: reference/robot_motion_collision/source_excerpt.md. */
/* Output layout: valid[0..5], collision[6..11], samples[12..17], counters[18..22]. */
#define EDGE_COUNT 6
#define SAMPLE_COUNT 5
#define OBSTACLE_COUNT 3

int robot_collision_core(const int *start_x,
                         const int *start_y,
                         const int *goal_x,
                         const int *goal_y,
                         const int *obstacle_x0,
                         const int *obstacle_y0,
                         const int *obstacle_x1,
                         const int *obstacle_y1,
                         int *out)
{
    int edge;
    int valid_edges = 0;
    int invalid_edges = 0;
    int out_of_bounds = 0;
    int obstacle_hits = 0;
    int early_exit_edges = 0;

    for (edge = 0; edge < EDGE_COUNT; ++edge) {
        int sample;
        int collision = 0;
        int active = 1;
        int samples_checked = 0;

        for (sample = 0; sample < SAMPLE_COUNT; ++sample) {
            if (active != 0) {
                int x = start_x[edge] +
                        (goal_x[edge] - start_x[edge]) * sample /
                        (SAMPLE_COUNT - 1);
                int y = start_y[edge] +
                        (goal_y[edge] - start_y[edge]) * sample /
                        (SAMPLE_COUNT - 1);
                int out_of_range = x < -8 || x > 8 || y < -8 || y > 8;
                int obstacle;
                int hit = 0;

                samples_checked++;
                if (out_of_range != 0) {
                    out_of_bounds++;
                    collision = 1;
                    active = 0;
                } else {
                    for (obstacle = 0; obstacle < OBSTACLE_COUNT; ++obstacle) {
                        if (x >= obstacle_x0[obstacle] &&
                            x <= obstacle_x1[obstacle] &&
                            y >= obstacle_y0[obstacle] &&
                            y <= obstacle_y1[obstacle]) {
                            hit = 1;
                        }
                    }
                    if (hit != 0) {
                        obstacle_hits++;
                        collision = 1;
                        active = 0;
                    }
                }
            }
        }
        out[edge] = collision == 0;
        out[EDGE_COUNT + edge] = collision;
        out[2 * EDGE_COUNT + edge] = samples_checked;
        if (collision == 0) {
            valid_edges++;
        } else {
            invalid_edges++;
            if (samples_checked < SAMPLE_COUNT) {
                early_exit_edges++;
            }
        }
    }
    out[18] = valid_edges;
    out[19] = invalid_edges;
    out[20] = out_of_bounds;
    out[21] = obstacle_hits;
    out[22] = early_exit_edges;
    return invalid_edges == 0;
}
