#include "test_tb.h"
#include "test.h"

#include <cstdlib>

void prepare(float A[SZ], float B[SZ], float C_tb[SZ], float D_tb[SZ]) {
	for(int i = 0; i < SZ; i++) {
		A[i] = i * 3;
		B[i] = i << 1;
	}

	for(int i = 0; i < 8; i++) {
		C_tb[i] = B[i] * 2;

		for(int j = 0; j < 64; j++)
			D_tb[64 * i + j] = A[64 * i + j] + 2;
	}
}

int main(void) {
	int retVal = EXIT_SUCCESS;
	float A[SZ], B[SZ], C[SZ], C_tb[SZ], D[SZ], D_tb[SZ];

	prepare(A, B, C_tb, D_tb);

	test(A, B, C, D);

	for(int i = 0; i < SZ / 64; i++) {
		if(C[i] != C_tb[i]) {
			retVal = EXIT_FAILURE;
			goto _err;
		}
	}
	for(int i = 0; i < SZ; i++) {
		if(D[i] != D_tb[i]) {
			retVal = EXIT_FAILURE;
			goto _err;
		}
	}

_err:

	return retVal;
}
