#include "profile_h/DynamicDatapath.h"

DynamicDatapath::DynamicDatapath(
	std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
	std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor
) : BaseDatapath(kernelName, CM, summaryFile, loopName, loopLevel, loopUnrollFactor, 0) {
	VERBOSE_PRINT(errs() << "[][][][dynamicDatapath] Analysing DDDG for loop \"" << loopName << "\"\n");

#if 0
	initBaseAddress();

	VERBOSE_PRINT(errs() << "[][][][dynamicDatapath] Analysing recurrence-constrained II\n");
	asapII = fpgaEstimationOneMoreSubtraceForRecIICalculation();

	VERBOSE_PRINT(errs() << "[][][][dynamicDatapath] Finished\n");
#endif
}

DynamicDatapath::DynamicDatapath(
	std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
	std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor,
	uint64_t asapII
) : BaseDatapath(kernelName, CM, summaryFile, loopName, loopLevel, loopUnrollFactor, asapII) {
	VERBOSE_PRINT(errs() << "[][][][dynamicDatapath] Analysing DDDG for loop \"" << loopName << "\"\n");

#if 0
	initBaseAddress();

	if(args.showPostOptDDDG)
		dumpGraph();

	numCycles = fpgaEstimation();

	VERBOSE_PRINT(errs() << "[][][][dynamicDatapath] Finished\n");
#endif
}

DynamicDatapath::~DynamicDatapath() {}

uint64_t DynamicDatapath::getASAPII() const {
	return asapII;
}

uint64_t DynamicDatapath::getCycles() const {
	return numCycles;
}
