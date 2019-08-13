#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "syrk.h"

void init(data_t A[SIZE], data_t C[SIZE]) {
	for(int i = 0; i < N; i++) {
		for(int j = 0; j < M; j++)
			A[i * M + j] = ((data_t) i * j) / N;

		for(int j = 0; j < N; j++)
			C[i * M + j] = ((data_t) i * j + 2) / N;
	}
}

void syrkSw(data_t *A, data_t *C) {
	for(int i = 0; i < N; i++) {
		for(int j = 0; j < N; j++) {
			C[i * M + j] *= BETA;

			for(int k = 0; k < M; k++)
				C[i * N + j] += ALPHA * A[i * M + k] * A[j * M + k];
		}
	}
}

int main(void) {
	data_t *hwA, *hwC;
	data_t *swA, *swC;

	hwA = (data_t *) malloc(SIZE * sizeof(data_t));
	hwC = (data_t *) malloc(SIZE * sizeof(data_t));
	swA = (data_t *) malloc(SIZE * sizeof(data_t));
	swC = (data_t *) malloc(SIZE * sizeof(data_t));

	init(hwA, hwC);
	syrk(hwA, hwC);

	init(swA, swC);
	syrkSw(swA, swC);

	for(int i = 0; i < SIZE; i++) {
		if(abs(swC[i] - hwC[i]) > 0.01)
			printf("Value mismatch in \"C\" position %d: hardware: %lf, software: %lf\n", i, hwC[i], swC[i]);
	}

	free(hwA);
	free(hwC);
	free(swA);
	free(swC);

	return 0;
}
