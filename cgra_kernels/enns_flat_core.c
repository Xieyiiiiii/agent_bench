/*
 * CGRA kernel slice: enns_flat_core
 * Reference slice: reference/haystack_enns_flat/analysis_zh.md
 * Output layout:
 *   out[0..3] = topk doc IDs
 *   out[4..7] = topk squared L2 distances
 *   out[8]    = docs_scanned
 */

#define CGRA_TOP_K 4
#define CGRA_INVALID_ID (-1)
#define CGRA_BIG_SCORE 2147483000

int enns_flat_core(short *query, short *db, int *out, int num_docs, int dim)
{
    int top_id0 = CGRA_INVALID_ID;
    int top_id1 = CGRA_INVALID_ID;
    int top_id2 = CGRA_INVALID_ID;
    int top_id3 = CGRA_INVALID_ID;
    int top_sc0 = CGRA_BIG_SCORE;
    int top_sc1 = CGRA_BIG_SCORE;
    int top_sc2 = CGRA_BIG_SCORE;
    int top_sc3 = CGRA_BIG_SCORE;
    int doc = 0;
    int d = 0;
    int docs_scanned = 0;

    for (doc = 0; doc < num_docs; doc = doc + 1) {
        int dist = 0;
        for (d = 0; d < dim; d = d + 1) {
            int diff = (int)query[d] - (int)db[doc * dim + d];
            dist = dist + diff * diff;
        }
        docs_scanned = docs_scanned + 1;

        if ((dist < top_sc0) || ((dist == top_sc0) && (doc < top_id0 || top_id0 < 0))) {
            top_sc3 = top_sc2; top_id3 = top_id2;
            top_sc2 = top_sc1; top_id2 = top_id1;
            top_sc1 = top_sc0; top_id1 = top_id0;
            top_sc0 = dist; top_id0 = doc;
        } else if ((dist < top_sc1) || ((dist == top_sc1) && (doc < top_id1 || top_id1 < 0))) {
            top_sc3 = top_sc2; top_id3 = top_id2;
            top_sc2 = top_sc1; top_id2 = top_id1;
            top_sc1 = dist; top_id1 = doc;
        } else if ((dist < top_sc2) || ((dist == top_sc2) && (doc < top_id2 || top_id2 < 0))) {
            top_sc3 = top_sc2; top_id3 = top_id2;
            top_sc2 = dist; top_id2 = doc;
        } else if ((dist < top_sc3) || ((dist == top_sc3) && (doc < top_id3 || top_id3 < 0))) {
            top_sc3 = dist; top_id3 = doc;
        }
    }

    out[0] = top_id0;
    out[1] = top_id1;
    out[2] = top_id2;
    out[3] = top_id3;
    out[4] = top_sc0;
    out[5] = top_sc1;
    out[6] = top_sc2;
    out[7] = top_sc3;
    out[8] = docs_scanned;
    return 0;
}
