#include "gemm.h"

void gemm(data_t A[SIZE_A], data_t B[SIZE_B], data_t C[SIZE_C]) {
<PRAGMA>
	for(int i = 0; i < NI; i++) {
		for(int j = 0; j < NJ; j++) {
<PRAGMA2>
			C[i * NJ + j] *= BETA;

			for(int k = 0; k < NK; k++) {
<PRAGMA3>
				C[i * NJ + j] += ALPHA * A[i * NK + k] * B[k * NJ + j];
			}
		}
	}
}
