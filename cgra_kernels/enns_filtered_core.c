/*
 * CGRA kernel slice: enns_filtered_core
 * Reference slice: reference/haystack_enns_filtered/analysis_zh.md
 * Output layout:
 *   out[0..1]   = top2 doc IDs
 *   out[2..3]   = top2 squared L2 distances
 *   out[4]      = filtered_out
 *   out[5]      = distance_full
 *   out[6]      = distance_abandoned
 */

#define CGRA_INVALID_ID (-1)
#define CGRA_BIG_SCORE 2147483000

int enns_filtered_core(short *query, short *db, int *meta, int *out,
                       int num_docs, int dim)
{
    int top_id0 = CGRA_INVALID_ID;
    int top_id1 = CGRA_INVALID_ID;
    int top_sc0 = CGRA_BIG_SCORE;
    int top_sc1 = CGRA_BIG_SCORE;
    int filtered_out = 0;
    int distance_full = 0;
    int distance_abandoned = 0;
    int doc = 0;

    for (doc = 0; doc < num_docs; doc = doc + 1) {
        int year = meta[doc * 3 + 0];
        int domain = meta[doc * 3 + 1];
        int flags = meta[doc * 3 + 2];
        int pass = 0;
        int dist = 0;
        int complete = 1;
        int d = 0;

        if (year >= 2021) {
            if (domain == 1) {
                if ((flags & 1) != 0) {
                    pass = 1;
                }
            }
        }
        if (pass == 0) {
            filtered_out = filtered_out + 1;
        } else {
            if (top_id1 < 0) {
                for (d = 0; d < dim; d = d + 1) {
                    int diff = (int)query[d] - (int)db[doc * dim + d];
                    dist = dist + diff * diff;
                }
            } else {
                for (d = 0; d < dim; d = d + 1) {
                    int diff = (int)query[d] - (int)db[doc * dim + d];
                    dist = dist + diff * diff;
                    if (dist > top_sc1) {
                        complete = 0;
                        d = dim;
                    }
                }
            }

            if (complete == 0) {
                distance_abandoned = distance_abandoned + 1;
            } else {
                distance_full = distance_full + 1;
                if ((dist < top_sc0) || ((dist == top_sc0) && (doc < top_id0 || top_id0 < 0))) {
                    top_sc1 = top_sc0; top_id1 = top_id0;
                    top_sc0 = dist; top_id0 = doc;
                } else if ((dist < top_sc1) || ((dist == top_sc1) && (doc < top_id1 || top_id1 < 0))) {
                    top_sc1 = dist; top_id1 = doc;
                }
            }
        }
    }

    out[0] = top_id0; out[1] = top_id1;
    out[2] = top_sc0; out[3] = top_sc1;
    out[4] = filtered_out;
    out[5] = distance_full;
    out[6] = distance_abandoned;
    return 0;
}
