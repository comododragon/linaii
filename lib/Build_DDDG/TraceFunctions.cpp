#include "profile_h/TraceFunctions.h"

FILE *fullTraceFile;
FILE *memTraceFile;
FILE *shortMemTraceFile;

bool initp = false;
int instCount = 0;

void trace_logger_init() {
	std::string traceFileName = args.workDir + FILE_DYNAMIC_TRACE;
	std::string popenComm("gzip -1 - > " + traceFileName);
	fullTraceFile = popen(popenComm.c_str(), "w");

	assert(fullTraceFile != Z_NULL && "Could not open trace output file");
}

void trace_logger_fin() {
	fclose(fullTraceFile);
}

void trace_logger_log0(int line_number, char *name, char *bbid, char *instid, int opcode) {
	if(!initp) {
		trace_logger_init();
		initp = true;
	}

	fprintf(fullTraceFile, "\n0,%d,%s,%s,%s,%d,%d\n", line_number, name, bbid, instid, opcode, instCount);
	instCount++;
}

void trace_logger_log_int(int line, int size, int64_t value, int is_reg, char *label) {
	assert(initp && "Trace Logger functions were not initialised correctly");

	if(RESULT_LINE == line)
		fprintf(fullTraceFile, "r,%d,%ld,%d,%s\n", size, value, is_reg, label);
	else if(FORWARD_LINE == line)
		fprintf(fullTraceFile, "f,%d,%ld,%d,%s\n", size, value, is_reg, label);
	else
		fprintf(fullTraceFile, "%d,%d,%ld,%d,%s\n", line, size, value, is_reg, label);
}

void trace_logger_log_double(int line, int size, double value, int is_reg, char *label) {
	assert(initp && "Trace Logger functions were not initialised correctly");

	if(RESULT_LINE == line)
		fprintf(fullTraceFile, "r,%d,%f,%d,%s\n", size, value, is_reg, label);
	else if(FORWARD_LINE == line)
		fprintf(fullTraceFile, "f,%d,%f,%d,%s\n", size, value, is_reg, label);
	else
		fprintf(fullTraceFile, "%d,%d,%f,%d,%s\n", line, size, value, is_reg, label);
}

void trace_logger_log_int_noreg(int line, int size, int64_t value, int is_reg) {
	assert(initp && "Trace Logger functions were not initialised correctly");

	if(RESULT_LINE == line)
		fprintf(fullTraceFile, "r,%d,%ld,%d\n", size, value, is_reg);
	else if(FORWARD_LINE == line)
		fprintf(fullTraceFile, "f,%d,%ld,%d\n", size, value, is_reg);
	else
		fprintf(fullTraceFile, "%d,%d,%ld,%d\n", line, size, value, is_reg);
}

void trace_logger_log_double_noreg(int line, int size, double value, int is_reg) {
	assert(initp && "Trace Logger functions were not initialised correctly");

	if(RESULT_LINE == line)
		fprintf(fullTraceFile, "r,%d,%f,%d\n", size, value, is_reg);
	else if(FORWARD_LINE == line)
		fprintf(fullTraceFile, "f,%d,%f,%d\n", size, value, is_reg);
	else
		fprintf(fullTraceFile, "%d,%d,%f,%d\n", line, size, value, is_reg);
}

bool traceEntry;
char buffName2[BUFF_STR_SZ];
char buffBB2[BUFF_STR_SZ];
std::string buffWholeLoopName;
unsigned buffNumLevels;
std::pair<std::string, std::string> buffWholeLoopNameInstNamePair;
int buffOpcode;

extern memoryTraceMapTy memoryTraceMap;
extern bool memoryTraceGenerated;

void trace_logger_init_m() {
	if(args.memTrace) {
		std::string fileName = args.workDir + FILE_MEM_TRACE;
		memTraceFile = fopen(fileName.c_str(), "w");

		assert(memTraceFile != nullptr && "Could not open memory trace output file");
	}
	if(args.shortMemTrace) {
		std::string shortFileName = args.workDir + FILE_MEM_TRACE_SHORT;
		shortMemTraceFile = fopen(shortFileName.c_str(), "wb");

		assert(shortMemTraceFile != nullptr && "Could not open short memory trace output binary file");
	}

	memoryTraceMap.clear();
	traceEntry = false;
	buffName2[0] = '\0';
	buffBB2[0] = '\0';
	buffOpcode = -1;

	trace_logger_init();
}

void trace_logger_fin_m() {
	if(args.memTrace)
		fclose(memTraceFile);
	if(args.shortMemTrace) {
		for(auto &tracePair : memoryTraceMap) {
			std::string key1 = tracePair.first.first;
			size_t key1Size = key1.length();
			std::string key2 = tracePair.first.second;
			size_t key2Size = key2.length();
			std::vector<uint64_t> addrVec = tracePair.second;
			size_t addrVecSize = addrVec.size();
			fwrite((char *) &key1Size, sizeof(size_t), 1, shortMemTraceFile);
			fwrite(key1.c_str(), sizeof(char), key1Size, shortMemTraceFile);
			fwrite((char *) &key2Size, sizeof(size_t), 1, shortMemTraceFile);
			fwrite(key2.c_str(), sizeof(char), key2Size, shortMemTraceFile);
			fwrite((char *) &addrVecSize, sizeof(size_t), 1, shortMemTraceFile);
			fwrite((char *) &addrVec[0], sizeof(uint64_t), addrVecSize, shortMemTraceFile);
		}

		fclose(shortMemTraceFile);
	}

	memoryTraceGenerated = true;

	trace_logger_fin();
}

void trace_logger_log0_m(int line_number, char *name, char *bbid, char *instid, int opcode) {
	if(!initp) {
		trace_logger_init_m();
		initp = true;
	}

	if(!traceEntry && (isStoreOp(opcode) || isLoadOp(opcode))) {
		traceEntry = true;

		std::string instName(instid);

		// Use buffered information if possible. If not, get the new information and buffer it
		if(strncmp(buffName2, name, BUFF_STR_SZ) || strncmp(buffBB2, bbid, BUFF_STR_SZ)) {
			std::string funcName(name);
			std::string bbName(bbid);
			// Load or store is inside a known loop, print this information
			bbFuncNamePair2lpNameLevelPairMapTy::iterator it = bbFuncNamePair2lpNameLevelPairMap.find(std::make_pair(bbName, funcName));
			assert(bbFuncNamePair2lpNameLevelPairMap.end() != it && "Key not found in bbFuncNamePair2lpNameLevelPairMap");

			lpNameLevelPairTy lpNameLevelPair = it->second;
			std::string loopName = lpNameLevelPair.first;
			std::string wholeLoopName = appendDepthToLoopName(loopName, lpNameLevelPair.second);
			buffWholeLoopName.assign(wholeLoopName);
			buffNumLevels = LpName2numLevelMap.at(loopName);

			strncpy(buffName2, name, BUFF_STR_SZ);
			buffName2[BUFF_STR_SZ - 1] = '\0';
			strncpy(buffBB2, bbid, BUFF_STR_SZ);
			buffBB2[BUFF_STR_SZ - 1] = '\0';
		}

		buffWholeLoopNameInstNamePair = std::make_pair(buffWholeLoopName, instName);
		buffOpcode = opcode;

		if(args.memTrace)
			fprintf(memTraceFile, "%s,%u,%s,%s,%d,", buffWholeLoopName.c_str(), buffNumLevels, instid, isLoadOp(opcode)? "load" : "store", instCount);
	}

	trace_logger_log0(line_number, name, bbid, instid, opcode);
}

void trace_logger_log_int_m(int line, int size, int64_t value, int is_reg, char *label) {
	if(traceEntry) {
		if((1 == line && isLoadOp(buffOpcode)) || (2 == line && isStoreOp(buffOpcode))) {
			if(args.memTrace)
				fprintf(memTraceFile, "%lu,", value);

			if(args.shortMemTrace)
				memoryTraceMap[buffWholeLoopNameInstNamePair].push_back(value);
		}
		else if((RESULT_LINE == line && isLoadOp(buffOpcode)) || (1 == line && isStoreOp(buffOpcode))) {
			if(args.memTrace)
				fprintf(memTraceFile, "%lu\n", value);

			traceEntry = false;
		}
	}

	trace_logger_log_int(line, size, value, is_reg, label);
}

void trace_logger_log_double_m(int line, int size, double value, int is_reg, char *label) {
	if(traceEntry) {
		if((1 == line && isLoadOp(buffOpcode)) || (2 == line && isStoreOp(buffOpcode))) {
			uint64_t uValue = value;

			if(args.memTrace)
				fprintf(memTraceFile, "%lu,", uValue);

			if(args.shortMemTrace)
				memoryTraceMap[buffWholeLoopNameInstNamePair].push_back(value);
		}
		else if((RESULT_LINE == line && isLoadOp(buffOpcode)) || (1 == line && isStoreOp(buffOpcode))) {
			float fValue = (float) value;

			if(args.memTrace)
				fprintf(memTraceFile, "%f\n", fValue);

			traceEntry = false;
		}
	}

	trace_logger_log_double(line, size, value, is_reg, label);
}

void trace_logger_log_int_noreg_m(int line, int size, int64_t value, int is_reg) {
	if(traceEntry) {
		if((1 == line && isLoadOp(buffOpcode)) || (2 == line && isStoreOp(buffOpcode))) {
			if(args.memTrace)
				fprintf(memTraceFile, "%lu,", value);

			if(args.shortMemTrace)
				memoryTraceMap[buffWholeLoopNameInstNamePair].push_back(value);
		}
		else if((RESULT_LINE == line && isLoadOp(buffOpcode)) || (1 == line && isStoreOp(buffOpcode))) {
			if(args.memTrace)
				fprintf(memTraceFile, "%lu\n", value);

			traceEntry = false;
		}
	}

	trace_logger_log_int_noreg(line, size, value, is_reg);
}

void trace_logger_log_double_noreg_m(int line, int size, double value, int is_reg) {
	if(traceEntry) {
		if((1 == line && isLoadOp(buffOpcode)) || (2 == line && isStoreOp(buffOpcode))) {
			uint64_t uValue = value;

			if(args.memTrace)
				fprintf(memTraceFile, "%lu,", uValue);

			if(args.shortMemTrace)
				memoryTraceMap[buffWholeLoopNameInstNamePair].push_back(value);
		}
		else if((RESULT_LINE == line && isLoadOp(buffOpcode)) || (1 == line && isStoreOp(buffOpcode))) {
			float fValue = (float) value;

			if(args.memTrace)
				fprintf(memTraceFile, "%f\n", fValue);

			traceEntry = false;
		}
	}

	trace_logger_log_double_noreg(line, size, value, is_reg);
}
