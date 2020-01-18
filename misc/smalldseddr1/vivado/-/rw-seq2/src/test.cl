__attribute__((reqd_work_group_size(1,1,1)))
__kernel void test(__global __read_only float * restrict A, __global __read_only float * restrict B, __global __read_only unsigned * restrict C, __global __write_only float * restrict D) {
	__local float a[256], b[256], c[256];
	for(int i = 0; i < 256; i++)
		a[i] = A[i];
	for(int i = 0; i < 256; i++)
		b[i] = B[i];
	for(int i = 0; i < 256; i++)
		c[i] = C[i];
	for(int i = 0; i < 256; i++) {
		D[i] = a[i] + b[i] + c[i];
	}
}
