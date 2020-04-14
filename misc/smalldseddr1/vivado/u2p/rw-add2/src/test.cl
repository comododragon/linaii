__attribute__((reqd_work_group_size(1,1,1)))
__kernel void test(__global __read_only float * restrict A, __global __write_only float * restrict D) {
	for(int i = 0; i < 256; i += 2) {
		float lA = A[i], lAp = A[i + 1];
		D[i] = lA + 2;
		D[i + 1] = lAp + 2;
	}
}
