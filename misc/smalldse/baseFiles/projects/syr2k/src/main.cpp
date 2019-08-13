#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "syr2k.h"

void init(data_t A[SIZE], data_t B[SIZE], data_t C[SIZE]) {
	for(int i = 0; i < N; i++) {
		for(int j = 0; j < N; j++)
			C[i * N + j] = ((data_t) i * j + 2) / N;

		for(int j = 0; j < M; j++) {
			A[i * N + j] = ((data_t) i * j) / N;
			B[i * N + j] = ((data_t) i * j + 1) / N;
		}
	}
}

void syr2kSw(data_t *A, data_t *B, data_t *C) {
	for(int i = 0; i < N; i++) {
		for(int j = 0; j < N; j++) {
			C[i * N + j] *= BETA;

			for(int k = 0; k < M; k++) {
				C[i * N + j] += ALPHA * A[i * M + k] * B[j * M + k];
				C[i * N + j] += ALPHA * B[i * M + k] * A[j * M + k];
			}
		}
	}
}

int main(void) {
	data_t *hwA, *hwB, *hwC;
	data_t *swA, *swB, *swC;

	hwA = (data_t *) malloc(SIZE * sizeof(data_t));
	hwB = (data_t *) malloc(SIZE * sizeof(data_t));
	hwC = (data_t *) malloc(SIZE * sizeof(data_t));
	swA = (data_t *) malloc(SIZE * sizeof(data_t));
	swB = (data_t *) malloc(SIZE * sizeof(data_t));
	swC = (data_t *) malloc(SIZE * sizeof(data_t));

	init(hwA, hwB, hwC);
	syr2k(hwA, hwB, hwC);

	init(swA, swB, swC);
	syr2kSw(swA, swB, swC);

	for(int i = 0; i < SIZE; i++) {
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
