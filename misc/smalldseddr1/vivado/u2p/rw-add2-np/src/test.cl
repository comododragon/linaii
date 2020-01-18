__attribute__((reqd_work_group_size(1,1,1)))
__kernel void test(__global float2 * restrict A, __global float * restrict B, __global float * restrict C, __global float2 * restrict D) {
	for(int i = 0; i < 8; i++) {
		B[i] = A[i] * 2;

		for(int j = 0; j < 32; j++) {
			D[32 * i + j].x = A[32 * i + j].x + 2;
			D[32 * i + j].y = A[32 * i + j].y + 2;
		}

		C[i] = A[i >> 1] + 4;
	}
}
