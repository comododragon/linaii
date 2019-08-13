#include "gesummv.h"

void gesummv(data_t A[SIZE_AB], data_t B[SIZE_AB], data_t x[SIZE_XY], data_t y[SIZE_XY], data_t tmp[SIZE_XY]) {
	for(int i = 0; i < N; i++) {
		tmp[i] = 0;
		y[i] = 0;

		for(int j = 0; j < N; j++) {
			tmp[i] = A[i * N + j] * x[j] + tmp[i];
			y[i] = B[i * N + j] * x[j] + y[i];
		}

		y[i] = ALPHA * tmp[i] + BETA * y[i];
	}
}
