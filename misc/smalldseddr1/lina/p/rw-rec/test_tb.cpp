#include "test_tb.h"
#include "test.h"

#include <cstdlib>

void prepare(float A[SZ], float B[SZ], float C[SZ], float D[SZ], float E[SZ]) {
	for(int i = 0; i < SZ; i++) {
		A[i] = i;
		B[i] = i + 10;
		C[i] = (5 * i) % 30;
		D[i] = 2 * (2.46 + i) / 2.2;
	}
}

int main(void) {
	int retVal = EXIT_SUCCESS;
	float A[SZ], B[SZ], C[SZ], D[SZ], E[SZ];

	prepare(A, B, C, D, E);

	test(A, B, C, D, E);

_err:

	return retVal;
}
