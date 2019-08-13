#include "syrk.h"

void syrk(data_t A[SIZE], data_t C[SIZE]) {
	for(int i = 0; i < N; i++) {
		for(int j = 0; j < N; j++) {
			C[i * M + j] *= BETA;

			for(int k = 0; k < M; k++)
				C[i * N + j] += ALPHA * A[i * M + k] * A[j * M + k];
		}
	}
}