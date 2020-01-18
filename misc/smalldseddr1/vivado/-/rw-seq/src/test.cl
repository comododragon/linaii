__attribute__((reqd_work_group_size(1,1,1)))
__kernel void test(__global __read_only float * restrict A, __global __read_only float * restrict B, __global __read_only unsigned * restrict C, __global __write_only float * restrict D) {
	for(int i = 0; i < 1024; i++) {
		D[i] = A[i] + B[i] + C[i];
	}
}
