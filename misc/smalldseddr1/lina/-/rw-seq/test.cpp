#include "test.h"

void test(float A[SZ], float B[SZ], unsigned C[SZ], float D[SZ]) {
	for(int i = 0; i < SZ; i++)
		D[i] = A[i] + B[i] + C[i];
}
