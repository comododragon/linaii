__attribute__((reqd_work_group_size(1,1,1)))
__kernel void test(
	__global float * restrict A, __global float * restrict B,
	__global float * restrict C, __global float * restrict D,
	__global float * restrict E, __global float * restrict F,
	__global float * restrict G, __global float * restrict H
) {
	for(int i = 0; i < 8; i++) {
		for(int j = 0; j < 64; j++)
			D[64 * i + j] = A[64 * i + j] + 2;

		C[i] = B[i] * 2;

		for(int j = 0; j < 64; j++)
			H[64 * i + j] = E[64 * i + j] + 4;

		G[i] = F[i] * 2;
	}
}
