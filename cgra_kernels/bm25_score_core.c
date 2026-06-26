/*
 * CGRA kernel slice: bm25_score_core
 * Reference slice: reference/haystack_bm25/analysis_zh.md
 * Output layout:
 *   out[0] = accepted_postings
 *   out[1] = filtered_out
 *   out[2] = empty_terms
 *   out[3] = active_docs_after_scoring
 */

#define CGRA_Q8_ONE 256
#define CGRA_BM25_K1_Q8 384
#define CGRA_BM25_B_Q8 192
#define CGRA_BM25_AVG_DOC_LEN 80

int bm25_score_core(int *query_terms, int *list_start, int *list_len,
                    int *post_doc, int *post_tf, int *doc_len,
                    int *doc_domain, int *idf_q8,
                    int *score_q8, int *active, int *out,
                    int num_query_terms, int num_docs)
{
    int accepted_postings = 0;
    int filtered_out = 0;
    int empty_terms = 0;
    int active_docs_after_scoring = 0;
    int qi = 0;
    int doc = 0;

    for (doc = 0; doc < num_docs; doc = doc + 1) {
        active[doc] = 0;
        score_q8[doc] = 0;
    }

    for (qi = 0; qi < num_query_terms; qi = qi + 1) {
        int term = query_terms[qi];
        int start = list_start[term];
        int len = list_len[term];
        int pi = 0;

        if (len == 0) {
            empty_terms = empty_terms + 1;
        } else {
            for (pi = 0; pi < len; pi = pi + 1) {
                int idx = start + pi;
                int doc_id = post_doc[idx];
                int tf = post_tf[idx];
                int valid_doc = 1;
                int term_score_q8 = 0;
                int norm_q8 = 0;
                int denom_q8 = 0;
                int tf_num_q8 = 0;
                int tf_weight_q8 = 0;

                if (doc_id < 0 || doc_id >= num_docs) {
                    valid_doc = 0;
                } else if (doc_domain[doc_id] != 1) {
                    valid_doc = 0;
                }

                if (valid_doc == 0) {
                    filtered_out = filtered_out + 1;
                } else {
                    norm_q8 = CGRA_Q8_ONE - CGRA_BM25_B_Q8 +
                              (CGRA_BM25_B_Q8 * doc_len[doc_id]) / CGRA_BM25_AVG_DOC_LEN;
                    denom_q8 = tf * CGRA_Q8_ONE +
                               (CGRA_BM25_K1_Q8 * norm_q8) / CGRA_Q8_ONE;

                    if (denom_q8 != 0) {
                        tf_num_q8 = tf * (CGRA_BM25_K1_Q8 + CGRA_Q8_ONE);
                        tf_weight_q8 = (tf_num_q8 * CGRA_Q8_ONE) / denom_q8;
                        term_score_q8 = (idf_q8[term] * tf_weight_q8) / CGRA_Q8_ONE;
                        score_q8[doc_id] = score_q8[doc_id] + term_score_q8;
                        active[doc_id] = 1;
                        accepted_postings = accepted_postings + 1;
                    }
                }
            }
        }
    }

    for (doc = 0; doc < num_docs; doc = doc + 1) {
        if (active[doc] != 0) {
            active_docs_after_scoring = active_docs_after_scoring + 1;
        }
    }

    out[0] = accepted_postings;
    out[1] = filtered_out;
    out[2] = empty_terms;
    out[3] = active_docs_after_scoring;
    return 0;
}
