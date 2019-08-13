#include "bicg.h"

void bicg(data_t A[SIZE_A], data_t r[SIZE_R], data_t s[SIZE_S], data_t p[SIZE_P], data_t q[SIZE_Q]) {
<PRAGMA>
	for(int i = 0; i < NY; i++)
		s[i] = 0.0;

	for(int i = 0; i < NX; i++) {
<PRAGMA2>
		q[i] = 0.0;

		for(int j = 0; j < NY; j++) {
<PRAGMA3>
			s[j] = s[j] + r[i] * A[i * NY + j];
			q[i] = q[i] + A[i * NY + j] * p[j];
		}
	}
}
