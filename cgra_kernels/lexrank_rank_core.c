/*
 * CGRA kernel slice: lexrank_rank_core
 * Reference slice: reference/haystack_lexrank/analysis_zh.md
 * Output layout:
 *   rank_old_q8[] is updated in place
 *   out[0] = iterations
 *   out[1] = dangling_sources_seen
 */

#define CGRA_Q8_ONE 256
#define CGRA_MAX_SENTENCES 6
#define CGRA_RANK_ITERS 6
#define CGRA_DAMPING_Q8 218

int lexrank_rank_core(int *graph, int *out_degree,
                      int *rank_old_q8, int *rank_new_q8, int *out)
{
    int iter = 0;
    int dangling_sources_seen = 0;
    int base_q8 = (CGRA_Q8_ONE - CGRA_DAMPING_Q8) / CGRA_MAX_SENTENCES;

    for (iter = 0; iter < CGRA_RANK_ITERS; iter = iter + 1) {
        int src = 0;
        int dst = 0;
        int dangling_sum_q8 = 0;
        int dangling_share_q8 = 0;

        for (src = 0; src < CGRA_MAX_SENTENCES; src = src + 1) {
            if (out_degree[src] == 0) {
                dangling_sum_q8 = dangling_sum_q8 + rank_old_q8[src];
                dangling_sources_seen = dangling_sources_seen + 1;
            }
        }

        dangling_share_q8 = dangling_sum_q8 / CGRA_MAX_SENTENCES;

        for (dst = 0; dst < CGRA_MAX_SENTENCES; dst = dst + 1) {
            int incoming_q8 = dangling_share_q8;
            for (src = 0; src < CGRA_MAX_SENTENCES; src = src + 1) {
                if (graph[src * CGRA_MAX_SENTENCES + dst] != 0) {
                    if (out_degree[src] > 0) {
                        incoming_q8 = incoming_q8 + rank_old_q8[src] / out_degree[src];
                    }
                }
            }
            rank_new_q8[dst] = base_q8 + (CGRA_DAMPING_Q8 * incoming_q8) / CGRA_Q8_ONE;
        }

        for (src = 0; src < CGRA_MAX_SENTENCES; src = src + 1) {
            rank_old_q8[src] = rank_new_q8[src];
        }
    }

    out[0] = CGRA_RANK_ITERS;
    out[1] = dangling_sources_seen;
    return 0;
}
