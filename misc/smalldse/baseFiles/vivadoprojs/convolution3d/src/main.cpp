#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "convolution3d.h"

void init(data_t A[SIZE], data_t B[SIZE]) {
	for(int i = 0; i < NI; i++) {
		for(int j = 0; j < NJ; j++) {
			for(int k = 0; k < NK; k++) {
				A[i * (NK * NJ) + j * NK + k] = i % 12 + 2 * (j % 7) + 3 * (k % 13);
				B[i * (NK * NJ) + j * NK + k] = (data_t) 0;
			}
		}
	}
}

void convolution3dSw(data_t *A, data_t *B) {
	data_t c11, c12, c13, c21, c22, c23, c31, c32, c33;
	c11 = 2.0; c21 = 5.0; c31 = -8.0;
	c12 = -3.0; c22 = 6.0; c32 = -9.0;
	c13 = 4.0; c23 = 7.0; c33 = 10.0;

	for(int i = 1; i < (NI - 1); i++) {
		for(int j = 1; j < (NJ - 1); j++) {
			for(int k = 1; k < (NK - 1); k++) {
				B[i * (NK * NJ) + j * NK + k] = c11 * A[(i - 1) * (NK * NJ) + (j - 1) * NK + (k - 1)]  +  c13 * A[(i + 1) * (NK * NJ) + (j - 1) * NK + (k - 1)]
					+ c21 * A[(i - 1) * (NK * NJ) + (j - 1) * NK + (k - 1)]  +  c23 * A[(i + 1) * (NK * NJ) + (j - 1) * NK + (k - 1)]
					+ c31 * A[(i - 1) * (NK * NJ) + (j - 1) * NK + (k - 1)]  +  c33 * A[(i + 1) * (NK * NJ) + (j - 1) * NK + (k - 1)]
					+ c12 * A[(i + 0) * (NK * NJ) + (j - 1) * NK + (k + 0)]  +  c22 * A[(i + 0) * (NK * NJ) + (j + 0) * NK + (k + 0)]
					+ c32 * A[(i + 0) * (NK * NJ) + (j + 1) * NK + (k + 0)]  +  c11 * A[(i - 1) * (NK * NJ) + (j - 1) * NK + (k + 1)]
					+ c13 * A[(i + 1) * (NK * NJ) + (j - 1) * NK + (k + 1)]  +  c21 * A[(i - 1) * (NK * NJ) + (j + 0) * NK + (k + 1)]
					+ c23 * A[(i + 1) * (NK * NJ) + (j + 0) * NK + (k + 1)]  +  c31 * A[(i - 1) * (NK * NJ) + (j + 1) * NK + (k + 1)]
					+ c33 * A[(i + 1) * (NK * NJ) + (j + 1) * NK + (k + 1)];
			}
		}
	}
}

int main(void) {
	data_t *hwA, *hwB;
	data_t *swA, *swB;

	hwA = (data_t *) malloc(SIZE * sizeof(data_t));
	hwB = (data_t *) malloc(SIZE * sizeof(data_t));
	swA = (data_t *) malloc(SIZE * sizeof(data_t));
	swB = (data_t *) malloc(SIZE * sizeof(data_t));

	init(hwA, hwB);
	convolution3d(hwA, hwB);

	init(swA, swB);
	convolution3dSw(swA, swB);

	for(int i = 0; i < SIZE; i++) {
		if(abs(swB[i] - hwB[i]) > 0.01)
			printf("Value mismatch in \"B\" position %d: hardware: %lf, software: %lf\n", i, hwB[i], swB[i]);
	}

	free(hwA);
	free(hwB);
	free(swA);
	free(swB);

	return 0;
}
