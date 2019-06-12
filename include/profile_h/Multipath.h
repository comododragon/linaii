#ifndef MULTIPATH_H
#define MULTIPATH_H

#include "profile_h/lin-profile.h"

using namespace llvm;

class Multipath {
	uint64_t numCycles;
	std::string kernelName;
	ConfigurationManager &CM;
	std::ofstream *summaryFile;
	std::string loopName;
	unsigned loopLevel;
	unsigned firstNonPerfectLoopLevel;
	uint64_t loopUnrollFactor;
	std::vector<unsigned> &unrolls;
	uint64_t actualLoopUnrollFactor;
	bool enablePipelining;

	std::vector<std::tuple<unsigned, unsigned, uint64_t>> latencies;
	Pack P;

	void _Multipath();

	void recursiveLookup(unsigned currLoopLevel, unsigned finalLoopLevel);

public:
	Multipath(
		std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
		std::string loopName, unsigned loopLevel, unsigned firstNonPerfectLoopLevel,
		uint64_t loopUnrollFactor, std::vector<unsigned> &unrolls, uint64_t actualLoopUnrollFactor
	);

	Multipath(
		std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
		std::string loopName, unsigned loopLevel, unsigned firstNonPerfectLoopLevel,
		uint64_t loopUnrollFactor, std::vector<unsigned> &unrolls
	);

	~Multipath();

	uint64_t getCycles() const;

#ifdef DBG_PRINT_ALL
	void printDatabase();
#endif
};

#endif // End of MULTIPATH_H
