#include "atax.h"

void atax(data_t A[SIZE_A], data_t x[SIZE_X], data_t tmp[SIZE_TMP]) {
	for(int i = 0; i < NX; i++) {
		data_t sumTmp = (data_t) 0;

		for(int j = 0; j < NY; j++)
			sumTmp += A[i * NY + j] * x[j];

		tmp[i] = sumTmp;
	}
}
