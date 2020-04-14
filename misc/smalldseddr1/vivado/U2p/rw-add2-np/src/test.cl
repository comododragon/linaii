__attribute__((reqd_work_group_size(1,1,1)))
__kernel void test(__global float * restrict A, __global float * restrict B, __global float * restrict C, __global float * restrict D) {
	for(int i = 0; i < 8; i += 2) {
		B[i] = A[i] * 2;

		for(int j = 0; j < 64; j++)
			D[64 * i + j] = A[64 * i + j] + 2;

		float lA = A[i >> 1], lAp = A[i + 1];
		C[i] = lA + 4;
		B[i + 1] = lAp * 2;

		for(int j = 0; j < 64; j++)
			D[64 * (i + 1) + j] = A[64 * (i + 1) + j] + 2;

		C[i + 1] = A[(i + 1) >> 1] + 4;
	}
}
