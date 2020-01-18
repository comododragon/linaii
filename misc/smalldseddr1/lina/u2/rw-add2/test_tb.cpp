#include "test_tb.h"
#include "test.h"

#include <cstdlib>

void prepare(float A[SZ], float D_tb[SZ]) {
	for(int i = 0; i < SZ; i++) {
		A[i] = i;
		D_tb[i] = A[i] + 2;
	}
}

int main(void) {
	int retVal = EXIT_SUCCESS;
	float A[SZ], D[SZ], D_tb[SZ];

	prepare(A, D_tb);

	test(A, D);

	for(int i = 0; i < SZ; i++) {
		if(D[i] != D_tb[i]) {
			retVal = EXIT_FAILURE;
			goto _err;
		}
	}

_err:

	return retVal;
}
