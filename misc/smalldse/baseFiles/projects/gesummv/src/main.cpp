#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "gesummv.h"

void init(data_t A[SIZE_AB], data_t B[SIZE_AB], data_t x[SIZE_XY]) {
	for(int i = 0; i < N; i++) {
		x[i] = ((data_t) i) / N;

		for(int j = 0; j < N; j++) {
			A[i * N + j] = ((data_t) i * j) / N;
			B[i * N + j] = ((data_t) i * j) / N;
		}
	}
}

void gesummvSw(data_t *A, data_t *B, data_t *x, data_t *y, data_t *tmp) {
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

int main(void) {
	data_t *hwA, *hwB, *hwX, *hwY, *hwTmp;
	data_t *swA, *swB, *swX, *swY, *swTmp;

	hwA = (data_t *) malloc(SIZE_AB * sizeof(data_t));
	hwB = (data_t *) malloc(SIZE_AB * sizeof(data_t));
	hwX = (data_t *) malloc(SIZE_XY * sizeof(data_t));
	hwY = (data_t *) malloc(SIZE_XY * sizeof(data_t));
	hwTmp = (data_t *) malloc(SIZE_XY * sizeof(data_t));
	swA = (data_t *) malloc(SIZE_AB * sizeof(data_t));
	swB = (data_t *) malloc(SIZE_AB * sizeof(data_t));
	swX = (data_t *) malloc(SIZE_XY * sizeof(data_t));
	swY = (data_t *) malloc(SIZE_XY * sizeof(data_t));
	swTmp = (data_t *) malloc(SIZE_XY * sizeof(data_t));

	init(hwA, hwB, hwX);
	gesummv(hwA, hwB, hwX, hwY, hwTmp);

	init(swA, swB, swX);
	gesummvSw(swA, swB, swX, swY, swTmp);

	for(int i = 0; i < SIZE_XY; i++) {
		if(abs(swY[i] - hwY[i]) > 0.01)
			printf("Value mismatch in \"y\" position %d: hardware: %lf, software: %lf\n", i, hwY[i], swY[i]);
	}
	for(int i = 0; i < SIZE_XY; i++) {
		if(abs(swTmp[i] - hwTmp[i]) > 0.01)
			printf("Value mismatch in \"tmp\" position %d: hardware: %lf, software: %lf\n", i, hwTmp[i], swTmp[i]);
	}

	free(hwA);
	free(hwB);
	free(hwX);
	free(hwY);
	free(hwTmp);
	free(swA);
	free(swB);
	free(swX);
	free(swY);
	free(swTmp);

	return 0;
}
