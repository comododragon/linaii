__attribute__((reqd_work_group_size(1,1,1)))
__kernel void test(__global float * restrict A, __global float * restrict B, __global float * restrict C, __global float * restrict D) {
	for(int i = 0; i < 8; i++) {
		for(int j = 0; j < 64; j += 2) {
			float lA = A[64 * i + j], lAp = A[64 * i + j + 1];
			D[64 * i + j] = lA + 2;
			D[64 * i + j + 1] = lAp + 2;
		}

		C[i] = B[i] * 2;
	}
}
