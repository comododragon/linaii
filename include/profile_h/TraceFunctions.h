#ifndef TRACEFUNCTIONS_H
#define TRACEFUNCTIONS_H

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include "profile_h/lin-profile.h"

#if !defined(RESULT_LINE) && !defined(FORWARD_LINE)
#define RESULT_LINE 19134
#define FORWARD_LINE 24601
#endif

void trace_logger_fin();
void trace_logger_init();
void trace_logger_log0(int line_number, char *name, char *bbid, char *instid, int opcode);
void trace_logger_log_int(int line, int size, int64_t value, int is_reg, char *label);
void trace_logger_log_double(int line, int size, double value, int is_reg, char *label);
void trace_logger_log_int_noreg(int line, int size, int64_t value, int is_reg);
void trace_logger_log_double_noreg(int line, int size, double value, int is_reg);
void trace_logger_fin_m();
void trace_logger_init_m();
void trace_logger_log0_m(int line_number, char *name, char *bbid, char *instid, int opcode);
void trace_logger_log_int_m(int line, int size, int64_t value, int is_reg, char *label);
void trace_logger_log_double_m(int line, int size, double value, int is_reg, char *label);
void trace_logger_log_int_noreg_m(int line, int size, int64_t value, int is_reg);
void trace_logger_log_double_noreg_m(int line, int size, double value, int is_reg);

#endif // End of TRACEFUNCTIONS_H
