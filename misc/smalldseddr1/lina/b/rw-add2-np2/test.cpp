#include "test.h"

void test(float A[SZ], float B[SZ], float C[SZ], float D[SZ]) {
	for(int i = 0; i < 8; i++) {
		C[i] = B[i] * 2;

		for(int j = 0; j < 64; j++)
			D[64 * i + j] = A[64 * i + j] + 2;
	}
}
