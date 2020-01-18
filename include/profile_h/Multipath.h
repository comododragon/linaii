#ifndef MULTIPATH_H
#define MULTIPATH_H

#include "profile_h/lin-profile.h"

using namespace llvm;

typedef std::unordered_map<unsigned, std::tuple<std::vector<MemoryModel::nodeExportTy>, std::vector<MemoryModel::nodeExportTy>, bool>> exportedNodesMapTy;

class Multipath {
	uint64_t numCycles;
	std::string kernelName;
	ConfigurationManager &CM;
	ContextManager &CtxM;
	std::ofstream *summaryFile;
	std::string loopName;
	unsigned loopLevel;
	unsigned firstNonPerfectLoopLevel;
	uint64_t loopUnrollFactor;
	std::vector<unsigned> &unrolls;
	uint64_t actualLoopUnrollFactor;
	bool enablePipelining;

	std::vector<std::tuple<unsigned, unsigned, uint64_t, uint64_t>> latencies;
	Pack P;

	exportedNodesMapTy exportedNodes;

	void _Multipath();

	void recursiveLookup(unsigned currLoopLevel, unsigned finalLoopLevel);

public:
	Multipath(
		std::string kernelName, ConfigurationManager &CM, ContextManager &CtxM, std::ofstream *summaryFile,
		std::string loopName, unsigned loopLevel, unsigned firstNonPerfectLoopLevel,
		uint64_t loopUnrollFactor, std::vector<unsigned> &unrolls, uint64_t actualLoopUnrollFactor
	);

	Multipath(
		std::string kernelName, ConfigurationManager &CM, ContextManager &CtxM, std::ofstream *summaryFile,
		std::string loopName, unsigned loopLevel, unsigned firstNonPerfectLoopLevel,
		uint64_t loopUnrollFactor, std::vector<unsigned> &unrolls
	);

	~Multipath();

	uint64_t getCycles() const;

	void dumpSummary(uint64_t numCycles);

#ifdef DBG_PRINT_ALL
	void printDatabase();
#endif
};

#endif // End of MULTIPATH_H
