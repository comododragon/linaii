#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "mvt.h"

void init(data_t a[SIZE_A], data_t x[SIZE], data_t y[SIZE]) {
	for(int i = 0; i < N; i++) {
		x[i] = 0.0;
		y[i] = 0.0;

		for(int j = 0; j < N; j++)
			a[i * N + j] = (data_t) (i + j + 1.0) / N;
	}
}

void mvtSw(data_t *a, data_t *x, data_t *y) {
	for(int i = 0; i < N; i++) {
		for(int j = 0; j < N; j++)
			x[i] = x[i] + a[i * N + j] * y[j];
	}
}

int main(void) {
	data_t *hwA, *hwX, *hwY;
	data_t *swA, *swX, *swY;

	hwA = (data_t *) malloc(SIZE_A * sizeof(data_t));
	hwX = (data_t *) malloc(SIZE * sizeof(data_t));
	hwY = (data_t *) malloc(SIZE * sizeof(data_t));
	swA = (data_t *) malloc(SIZE_A * sizeof(data_t));
	swX = (data_t *) malloc(SIZE * sizeof(data_t));
	swY = (data_t *) malloc(SIZE * sizeof(data_t));

	init(hwA, hwX, hwY);
	mvt(hwA, hwX, hwY);

	init(swA, swX, swY);
	mvtSw(swA, swX, swY);

	for(int i = 0; i < SIZE; i++) {
		if(abs(swX[i] - hwX[i]) > 0.01)
			printf("Value mismatch in \"x\" position %d: hardware: %lf, software: %lf\n", i, hwX[i], swX[i]);
	}

	free(hwA);
	free(hwX);
	free(hwY);
	free(swA);
	free(swX);
	free(swY);

	return 0;
}
