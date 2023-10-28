#ifndef DYNAMIC_DATAPATH_H
#define DYNAMIC_DATAPATH_H

#include <string>
#include "profile_h/lin-profile.h"
#include "profile_h/BaseDatapath.h"

using namespace llvm;

class DynamicDatapath : public BaseDatapath {
	void _DynamicDatapath(
		std::string kernelName, ConfigurationManager &CM, ContextManager &CtxM, std::ofstream *summaryFile, SharedDynamicTrace &traceFile,
		std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor,
		std::vector<MemoryModel::nodeExportTy> *nodesToImport,
		unsigned datapathType
	);

public:
	DynamicDatapath(
		std::string kernelName, ConfigurationManager &CM, ContextManager &CtxM, std::ofstream *summaryFile, SharedDynamicTrace &traceFile,
		std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor
	);

	DynamicDatapath(
		std::string kernelName, ConfigurationManager &CM, ContextManager &CtxM, std::ofstream *summaryFile, SharedDynamicTrace &traceFile,
		std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor,
		bool enablePipelining, uint64_t asapII
	);

	DynamicDatapath(
		std::string kernelName, ConfigurationManager &CM, ContextManager &CtxM, std::ofstream *summaryFile, SharedDynamicTrace &traceFile,
		std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor,
		unsigned datapathType
	);

	DynamicDatapath(
		std::string kernelName, ConfigurationManager &CM, ContextManager &CtxM, std::ofstream *summaryFile, SharedDynamicTrace &traceFile,
		std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor,
		std::vector<MemoryModel::nodeExportTy> &nodesToImport, unsigned datapathType
	);

	~DynamicDatapath();

	uint64_t getASAPII() const;
	uint64_t getCycles() const;

#ifdef DBG_PRINT_ALL
	void printDatabase();
#endif
};

#endif // End of DYNAMIC_DATAPATH_H
