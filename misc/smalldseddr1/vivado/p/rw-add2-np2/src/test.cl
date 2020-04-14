__attribute__((reqd_work_group_size(1,1,1)))
__kernel void test(__global float * restrict A, __global float * restrict B, __global float * restrict C, __global float * restrict D) {
	for(int i = 0; i < 8; i++) {
		C[i] = B[i] * 2;

		__attribute__((xcl_pipeline_loop))
		for(int j = 0; j < 64; j++)
			D[64 * i + j] = A[64 * i + j] + 2;
	}
}
