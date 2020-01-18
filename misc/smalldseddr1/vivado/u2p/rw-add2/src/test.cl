__attribute__((reqd_work_group_size(1,1,1)))
__kernel void test(__global __read_only float2 * restrict A, __global __write_only float2 * restrict D) {
	for(int i = 0; i < 128; i++) {
		D[i].x = A[i].x + 2;
		D[i].y = A[i].y + 2;
	}
}
