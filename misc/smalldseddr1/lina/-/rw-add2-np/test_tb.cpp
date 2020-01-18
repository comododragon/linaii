#include "test_tb.h"
#include "test.h"

#include <cstdlib>

void prepare(float A[SZ], float B_tb[SZ], float C_tb[SZ], float D_tb[SZ]) {
	for(int i = 0; i < SZ; i++) {
		A[i] = i * 3;
	}

	for(int i = 0; i < 8; i++) {
		B_tb[i] = A[i] * 2;

		for(int j = 0; j < 64; j++)
			D_tb[64 * i + j] = A[64 * i + j] + 2;

		C_tb[i] = A[i >> 1] + 4;
	}
}

int main(void) {
	int retVal = EXIT_SUCCESS;
	float A[SZ], B[SZ], B_tb[SZ], C[SZ], C_tb[SZ], D[SZ], D_tb[SZ];

	prepare(A, B_tb, C_tb, D_tb);

	test(A, B, C, D);

	for(int i = 0; i < SZ / 64; i++) {
		if(B[i] != B_tb[i]) {
			retVal = EXIT_FAILURE;
			goto _err;
		}
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
