__attribute__((reqd_work_group_size(1,1,1)))
__kernel void test(__global __read_only float * restrict A, __global __write_only float * restrict D) {
    D[0] = A[0] + 2;
    D[1] = A[1] + 2;
    D[2] = A[2] + 2;
    D[3] = A[3] + 2;
    D[4] = A[4] + 2;
    D[5] = A[5] + 2;
    D[6] = A[6] + 2;
    D[7] = A[7] + 2;
    D[8] = A[8] + 2;
    D[9] = A[9] + 2;
    D[10] = A[10] + 2;
    D[11] = A[11] + 2;
    D[12] = A[12] + 2;
    D[13] = A[13] + 2;
    D[14] = A[14] + 2;
    D[15] = A[15] + 2;
    D[16] = A[16] + 2;
    D[17] = A[17] + 2;
    D[18] = A[18] + 2;
    D[19] = A[19] + 2;
    D[20] = A[20] + 2;
    D[21] = A[21] + 2;
    D[22] = A[22] + 2;
    D[23] = A[23] + 2;
    D[24] = A[24] + 2;
    D[25] = A[25] + 2;
    D[26] = A[26] + 2;
    D[27] = A[27] + 2;
    D[28] = A[28] + 2;
    D[29] = A[29] + 2;
    D[30] = A[30] + 2;
    D[31] = A[31] + 2;
    D[32] = A[32] + 2;
    D[33] = A[33] + 2;
    D[34] = A[34] + 2;
    D[35] = A[35] + 2;
    D[36] = A[36] + 2;
    D[37] = A[37] + 2;
    D[38] = A[38] + 2;
    D[39] = A[39] + 2;
    D[40] = A[40] + 2;
    D[41] = A[41] + 2;
    D[42] = A[42] + 2;
    D[43] = A[43] + 2;
    D[44] = A[44] + 2;
    D[45] = A[45] + 2;
    D[46] = A[46] + 2;
    D[47] = A[47] + 2;
    D[48] = A[48] + 2;
    D[49] = A[49] + 2;
    D[50] = A[50] + 2;
    D[51] = A[51] + 2;
    D[52] = A[52] + 2;
    D[53] = A[53] + 2;
    D[54] = A[54] + 2;
    D[55] = A[55] + 2;
    D[56] = A[56] + 2;
    D[57] = A[57] + 2;
    D[58] = A[58] + 2;
    D[59] = A[59] + 2;
    D[60] = A[60] + 2;
    D[61] = A[61] + 2;
    D[62] = A[62] + 2;
    D[63] = A[63] + 2;
}
