#ifndef DYNAMIC_DATAPATH_H
#define DYNAMIC_DATAPATH_H

#include <string>
#include "profile_h/lin-profile.h"
#include "profile_h/BaseDatapath.h"

using namespace llvm;

class DynamicDatapath : public BaseDatapath {
public:
	DynamicDatapath(
		std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
		std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor
	);

	DynamicDatapath(
		std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
		std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor,
		bool enablePipelining, uint64_t asapII
	);

	DynamicDatapath(
		std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
		std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor, unsigned datapathType
	);

	~DynamicDatapath();

	uint64_t getASAPII() const;
	uint64_t getCycles() const;

#ifdef DBG_PRINT_ALL
	void printDatabase();
#endif
};

#endif // End of DYNAMIC_DATAPATH_H
