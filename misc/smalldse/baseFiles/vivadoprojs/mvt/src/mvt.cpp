#include "mvt.h"

void mvt(data_t a[SIZE_A], data_t x[SIZE], data_t y[SIZE]) {
<PRAGMA>
	for(int i = 0; i < N; i++) {
<PRAGMA2>
		for(int j = 0; j < N; j++) {
<PRAGMA3>
			x[i] = x[i] + a[i * N + j] * y[j];
		}
	}
}
