#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "atax.h"

void init(data_t *A, data_t *x) {
	for(int i = 0; i < NX; i++) {
		x[i] = i * M_PI;

		for(int j = 0; j < NY; j++)
			A[i * NY + j] = ((data_t) i * (j)) / NX;
	}
}

void ataxSw(data_t *A, data_t *x, data_t *tmp) {
	for(int i = 0; i < NX; i++) {
		data_t sumTmp = (data_t) 0;

		for(int j = 0; j < NY; j++)
			sumTmp += A[i * NY + j] * x[j];

		tmp[i] = sumTmp;
	}
}

int main(void) {
	data_t *hwA, *hwX, *hwTmp;
	data_t *swA, *swX, *swTmp;

	hwA = (data_t *) malloc(SIZE_A * sizeof(data_t));
	hwX = (data_t *) malloc(SIZE_X * sizeof(data_t));
	hwTmp = (data_t *) malloc(SIZE_TMP * sizeof(data_t));
	swA = (data_t *) malloc(SIZE_A * sizeof(data_t));
	swX = (data_t *) malloc(SIZE_X * sizeof(data_t));
	swTmp = (data_t *) malloc(SIZE_TMP * sizeof(data_t));

	init(hwA, hwX);
	atax(hwA, hwX, hwTmp);

	init(swA, swX);
	ataxSw(swA, swX, swTmp);

	for(int i = 0; i < SIZE_TMP; i++) {
		if(abs(swTmp[i] - hwTmp[i]) > 0.01)
			printf("Value mismatch in \"tmp\" position %d: hardware: %lf, software: %lf\n", i, hwTmp[i], swTmp[i]);
	}

	free(hwA);
	free(hwX);
	free(hwTmp);
	free(swA);
	free(swX);
	free(swTmp);

	return 0;
}
