#ifndef MULTIPATH_H
#define MULTIPATH_H

#include "profile_h/lin-profile.h"

using namespace llvm;

class Multipath {
	uint64_t numCycles;

public:
	Multipath(
		std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
		std::string loopName, unsigned loopLevel, unsigned firstNonPerfectLoopLevel, uint64_t loopUnrollFactor,
		bool enablePipelining, uint64_t asapII
	);

	~Multipath();

	uint64_t getCycles() const;

#ifdef DBG_PRINT_ALL
	void printDatabase();
#endif
};

#endif // End of MULTIPATH_H
