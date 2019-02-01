#include "profile_h/TraceFunctions.h"

gzFile fullTraceFile;

bool initp = false;
int instCount = 0;

void trace_logger_init() {
	std::string traceFileName = args.workDir + "dynamic_trace.gz";
	fullTraceFile = gzopen(traceFileName.c_str(), "w");

	assert(fullTraceFile && "Could not open trace output file");
}

void trace_logger_fin() {
	gzclose(fullTraceFile);
}

void trace_logger_log0(int line_number, char *name, char *bbid, char *instid, int opcode) {
	if(!initp) {
		trace_logger_init();
		initp = true;
	}

	gzprintf(fullTraceFile, "\n0,%d,%s,%s,%s,%d,%d\n", line_number, name, bbid, instid, opcode, instCount);
	instCount++;
}

void trace_logger_log_int(int line, int size, int64_t value, int is_reg, char *label) {
	assert(initp && "Trace Logger functions were not initialised correctly");

	if(RESULT_LINE == line)
		gzprintf(fullTraceFile, "r,%d,%ld,%d,%s\n", size, value, is_reg, label);
	else if(FORWARD_LINE == line)
		gzprintf(fullTraceFile, "f,%d,%ld,%d,%s\n", size, value, is_reg, label);
	else
		gzprintf(fullTraceFile, "%d,%d,%ld,%d,%s\n", line, size, value, is_reg, label);
}

void trace_logger_log_double(int line, int size, double value, int is_reg, char *label) {
	assert(initp && "Trace Logger functions were not initialised correctly");

	if(RESULT_LINE == line)
		gzprintf(fullTraceFile, "r,%d,%f,%d,%s\n", size, value, is_reg, label);
	else if(FORWARD_LINE == line)
		gzprintf(fullTraceFile, "f,%d,%f,%d,%s\n", size, value, is_reg, label);
	else
		gzprintf(fullTraceFile, "%d,%d,%f,%d,%s\n", line, size, value, is_reg, label);
}

void trace_logger_log_int_noreg(int line, int size, int64_t value, int is_reg) {
	assert(initp && "Trace Logger functions were not initialised correctly");

	if(RESULT_LINE == line)
		gzprintf(fullTraceFile, "r,%d,%ld,%d\n", size, value, is_reg);
	else if(FORWARD_LINE == line)
		gzprintf(fullTraceFile, "f,%d,%ld,%d\n", size, value, is_reg);
	else
		gzprintf(fullTraceFile, "%d,%d,%ld,%d\n", line, size, value, is_reg);
}

void trace_logger_log_double_noreg(int line, int size, double value, int is_reg) {
	assert(initp && "Trace Logger functions were not initialised correctly");

	if(RESULT_LINE == line)
		gzprintf(fullTraceFile, "r,%d,%f,%d\n", size, value, is_reg);
	else if(FORWARD_LINE == line)
		gzprintf(fullTraceFile, "f,%d,%f,%d\n", size, value, is_reg);
	else
		gzprintf(fullTraceFile, "%d,%d,%f,%d\n", line, size, value, is_reg);
}
