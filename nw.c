#define MATCH_SCORE 1
#define MISMATCH_SCORE (-1)
#define GAP_SCORE (-1)

#define ALIGN 92
#define SKIPA 94
#define SKIPB 60

#define MAX(A,B) ( ((A)>(B))?(A):(B) )

int needwun(char* SEQA, char* SEQB,char* alignedA, char* alignedB,int* M, char* ptr,int ALEN,int BLEN){

    int score=1, up_left=0, up=0, left=0, max=0;
    int row=0, row_up=0, r=0;
    int a_idx=0, b_idx=0;
    int a_str_idx=0, b_str_idx=0;
    int pandu=1;

    for(a_idx=0; a_idx<(ALEN+1); a_idx=a_idx+1){
        M[a_idx] = a_idx * GAP_SCORE;
    }
    for(b_idx=0; b_idx<(BLEN+1); b_idx=b_idx+1){
        M[b_idx*(ALEN+1)] = b_idx * GAP_SCORE;
    }

    // Matrix filling loop
    for(b_idx=1; b_idx<(BLEN+1); b_idx=b_idx+1){
        row = (b_idx) * (ALEN + 1);
        int uk = M[row + 0];
        for(a_idx=1; a_idx<(ALEN+1); a_idx=a_idx+1){
            if(SEQA[a_idx-1] == SEQB[b_idx-1]){
                score = MATCH_SCORE;
            } else {
                score = -1;
            }

            row_up = (b_idx-1)*(ALEN+1);
            row = (b_idx)*(ALEN+1);

            up_left = M[row_up + (a_idx-1)] + score;
            up      = M[row_up + (a_idx  )] + GAP_SCORE;
            left    = uk + GAP_SCORE;
            if(up>left){
                max=up;
            }
            else{
                max=left;
            }
            if(up_left>max){
                max=up_left;
            }
            

            
            

            M[row + a_idx] = max;
            uk = max;
            if(max == left){
                ptr[row + a_idx] = SKIPB;
            } else if(max == up){
                ptr[row + a_idx] = SKIPA;
            } else{
                ptr[row + a_idx] = ALIGN;
            }
        }
    }

    // TraceBack (n.b. aligned sequences are backwards to avoid string appending)
    a_idx = ALEN;
    b_idx = BLEN;
    a_str_idx = 0;
    b_str_idx = 0;
    if(a_idx>0){
        pandu=1;
    }
    else if(b_idx>0){
        pandu=1;
    }
    else{
        pandu=0;
    }
    while(pandu>0) {
        r = b_idx*(ALEN+1);
        if (ptr[r + a_idx] == ALIGN){
            alignedA[a_str_idx] = SEQA[a_idx-1];
            a_str_idx=a_str_idx+1;
            alignedB[b_str_idx] = SEQB[b_idx-1];
            b_str_idx=b_str_idx+1;
            a_idx=a_idx-1;
            b_idx=b_idx-1;
        }
        else if (ptr[r + a_idx] == SKIPB){
            alignedA[a_str_idx] = SEQA[a_idx-1];
            alignedB[b_str_idx] = 45;
            a_str_idx=a_str_idx+1;
            b_str_idx=b_str_idx+1;
            a_idx=a_idx-1;
        }
        else{ // SKIPA
            alignedA[a_str_idx] = 45;
            alignedB[b_str_idx] = SEQB[b_idx-1];
            a_str_idx=a_str_idx+1;
            b_str_idx=b_str_idx+1;
            b_idx=b_idx-1;
        }
        if(a_idx>0){
            pandu=1;
        }
        else if(b_idx>0){
            pandu=1;
        }
        else{
            pandu=0;
        }
    }

    // Pad the result
    for( a_str_idx=a_str_idx; a_str_idx<ALEN+BLEN; a_str_idx=a_str_idx+1 ) {
      alignedA[a_str_idx] = 95;
    }
    for( b_str_idx=b_str_idx; b_str_idx<ALEN+BLEN; b_str_idx=b_str_idx+1) {
      alignedB[b_str_idx] = 95;
    }
    return 0;
}