#include "test.h"

void test(float A[SZ], float D[SZ]) {
	for(int i = 0; i < SZ; i++)
		D[i] = A[i] + 2;
}
