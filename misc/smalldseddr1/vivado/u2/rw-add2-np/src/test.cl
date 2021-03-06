__attribute__((reqd_work_group_size(1,1,1)))
__kernel void test(__global float * restrict A, __global float * restrict B, __global float * restrict C, __global float * restrict D) {
	for(int i = 0; i < 8; i++) {
		B[i] = A[i] * 2;

		__attribute__((opencl_unroll_hint(2)))
		for(int j = 0; j < 64; j++)
			D[64 * i + j] = A[64 * i + j] + 2;

		C[i] = A[i >> 1] + 4;
	}
}
