#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "convolution2d.h"

void init(data_t hwA[SIZE], data_t hwB[SIZE], data_t swA[SIZE], data_t swB[SIZE]) {
	for(int i = 0; i < NI; i++) {
		for(int j = 0; j < NJ; j++) {
			hwA[i * NJ + j] = (float) rand() / RAND_MAX;
			hwB[i * NJ + j] = 0.0f;
			swA[i * NJ + j] = hwA[i * NJ + j];
			swB[i * NJ + j] = 0.0f;
		}
	}
}

void convolution2dSw(data_t *A, data_t *B) {
	data_t c11, c12, c13, c21, c22, c23, c31, c32, c33;
	c11 = 0.2; c21 = 0.5; c31 = -0.8;
	c12 = -0.3; c22 = 0.6; c32 = -0.9;
	c13 = 0.4; c23 = 0.7; c33 = 0.10;

	for(int i = 1; i < (NI - 1); i++) {
		for(int j = 1; j < (NJ - 1); j++) {
			B[i * NJ + j] = c11 * A[(i - 1) * NJ + (j - 1)]  +  c12 * A[(i + 0) * NJ + (j - 1)]  +  c13 * A[(i + 1) * NJ + (j - 1)]
				+ c21 * A[(i - 1) * NJ + (j + 0)]  +  c22 * A[(i + 0) * NJ + (j + 0)]  +  c23 * A[(i + 1) * NJ + (j + 0)]
				+ c31 * A[(i - 1) * NJ + (j + 1)]  +  c32 * A[(i + 0) * NJ + (j + 1)]  +  c33 * A[(i + 1) * NJ + (j + 1)];
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

	init(hwA, hwB, swA, swB);
	convolution2d(hwA, hwB);
	convolution2dSw(swA, swB);

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
