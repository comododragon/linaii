#include "test.h"

void test(float A[SZ], float B[SZ], float C[SZ], float D[SZ], float E[SZ]) {
	for(int i = 0; i < SZ; i++) {
		float idx = ((C[i] * 3.5) / 2.7) * 5.5;
		float idx2 = ((D[i] / idx) * 2.7) / 5.5;
		E[i] = A[(unsigned) idx] + B[(unsigned) idx2];
	}
}
