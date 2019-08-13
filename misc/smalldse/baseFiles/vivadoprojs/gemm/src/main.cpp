#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "gemm.h"

void init(data_t A[SIZE_A], data_t B[SIZE_B], data_t C[SIZE_C]) {
	for(int i = 0; i < NI; i++) {
		for(int j = 0; j < NK; j++)
			A[i * NK + j] = ((data_t) i * j) / NI;
	}

	for(int i = 0; i < NK; i++) {
		for(int j = 0; j < NJ; j++)
			B[i * NJ + j] = ((data_t) i * j + 1) / NJ;
	}

	for(int i = 0; i < NI; i++) {
		for(int j = 0; j < NJ; j++)
			C[i * NJ + j] = ((data_t) i * j + 2) / NJ;
	}
}

void gemmSw(data_t *A, data_t *B, data_t *C) {
	for(int i = 0; i < NI; i++) {
		for(int j = 0; j < NJ; j++) {
			C[i * NJ + j] *= BETA;

			for(int k = 0; k < NK; k++)
				C[i * NJ + j] += ALPHA * A[i * NK + k] * B[k * NJ + j];
		}
	}
}

int main(void) {
	data_t *hwA, *hwB, *hwC;
	data_t *swA, *swB, *swC;

	hwA = (data_t *) malloc(SIZE_A * sizeof(data_t));
	hwB = (data_t *) malloc(SIZE_B * sizeof(data_t));
	hwC = (data_t *) malloc(SIZE_C * sizeof(data_t));
	swA = (data_t *) malloc(SIZE_A * sizeof(data_t));
	swB = (data_t *) malloc(SIZE_B * sizeof(data_t));
	swC = (data_t *) malloc(SIZE_C * sizeof(data_t));

	init(hwA, hwB, hwC);
	gemm(hwA, hwB, hwC);

	init(swA, swB, swC);
	gemmSw(swA, swB, swC);

	for(int i = 0; i < SIZE_C; i++) {
		if(abs(swC[i] - hwC[i]) > 0.01)
			printf("Value mismatch in \"C\" position %d: hardware: %lf, software: %lf\n", i, hwC[i], swC[i]);
	}

	free(hwA);
	free(hwB);
	free(hwC);
	free(swA);
	free(swB);
	free(swC);

	return 0;
}
