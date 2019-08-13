#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "bicg.h"

void init(data_t *A, data_t *r, data_t *p) {
	for(int i = 0; i < NX; i++) {
		r[i] = i * M_PI;

		for(int j = 0; j < NY; j++)
			A[i * NY + j] = ((data_t) i * j) / NX;
	}

	for(int i = 0; i < NY; i++)
		p[i] = i * M_PI;
}

void bicgSw(data_t *A, data_t *r, data_t *s, data_t *p, data_t *q) {
	int i, j;

  	for(i = 0; i < NY; i++)
		s[i] = 0.0;

	for(i = 0; i < NX; i++) {
		q[i] = 0.0;

		for(j = 0; j < NY; j++) {
			s[j] = s[j] + r[i] * A[i * NY + j];
			q[i] = q[i] + A[i * NY + j] * p[j];
		}
	}
}

int main(void) {
	data_t *hwA, *hwR, *hwS, *hwP, *hwQ;
	data_t *swA, *swR, *swS, *swP, *swQ;

	hwA = (data_t *) malloc(SIZE_A * sizeof(data_t));
	hwR = (data_t *) malloc(SIZE_R * sizeof(data_t));
	hwS = (data_t *) malloc(SIZE_S * sizeof(data_t));
	hwP = (data_t *) malloc(SIZE_P * sizeof(data_t));
	hwQ = (data_t *) malloc(SIZE_Q * sizeof(data_t));
	swA = (data_t *) malloc(SIZE_A * sizeof(data_t));
	swR = (data_t *) malloc(SIZE_R * sizeof(data_t));
	swS = (data_t *) malloc(SIZE_S * sizeof(data_t));
	swP = (data_t *) malloc(SIZE_P * sizeof(data_t));
	swQ = (data_t *) malloc(SIZE_Q * sizeof(data_t));

	init(hwA, hwR, hwP);
	bicg(hwA, hwR, hwS, hwP, hwQ);

	init(swA, swR, swP);
	bicgSw(swA, swR, swS, swP, swQ);

	for(int i = 0; i < SIZE_S; i++) {
		if(abs(swS[i] - hwS[i]) > 0.01)
			printf("Value mismatch in \"s\" position %d: hardware: %lf, software: %lf\n", i, hwS[i], swS[i]);
	}
	for(int i = 0; i < SIZE_Q; i++) {
		if(abs(swQ[i] - hwQ[i]) > 0.01)
			printf("Value mismatch in \"q\" position %d: hardware: %lf, software: %lf\n", i, hwQ[i], swQ[i]);
	}

	free(hwA);
	free(hwR);
	free(hwS);
	free(hwP);
	free(hwQ);
	free(swA);
	free(swR);
	free(swS);
	free(swP);
	free(swQ);

	return 0;
}
