#include "syr2k.h"

void syr2k(data_t A[SIZE], data_t B[SIZE], data_t C[SIZE]) {
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