__attribute__((reqd_work_group_size(1,1,1)))
__kernel void test(__global __read_only float * restrict A, __global __write_only float * restrict D) {
	__attribute__((opencl_unroll_hint(2)))
	for(int i = 0; i < 256; i++) {
		D[i] = A[i] + 2;
	}
}
