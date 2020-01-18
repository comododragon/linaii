#include "test_tb.h"
#include "test.h"

#include <cstdlib>

void prepare(float A[SZ], float B[SZ], unsigned C[SZ], float D_tb[SZ]) {
	for(int i = 0; i < SZ; i++) {
		A[i] = i;
		B[i] = i * 2;
		C[i] = i;
		D_tb[i] = A[C[i]] + B[C[i]];
	}
}

int main(void) {
	int retVal = EXIT_SUCCESS;
	float A[SZ], B[SZ], D[SZ], D_tb[SZ];
	unsigned C[SZ];

	prepare(A, B, C, D_tb);

	test(A, B, C, D);

	for(int i = 0; i < SZ; i++) {
		if(D[i] != D_tb[i]) {
			retVal = EXIT_FAILURE;
			goto _err;
		}
	}

_err:

	return retVal;
}
