#include "profile_h/DynamicDatapath.h"

DynamicDatapath::DynamicDatapath(
	std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
	std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor
) : BaseDatapath(kernelName, CM, summaryFile, loopName, loopLevel, loopUnrollFactor, 0) {
	VERBOSE_PRINT(errs() << "[][][][dynamicDatapath] Analysing DDDG for loop \"" << loopName << "\"\n");

	initBaseAddress();

	VERBOSE_PRINT(errs() << "[][][][dynamicDatapath] Analysing recurrence-constrained II\n");
	asapII = fpgaEstimationOneMoreSubtraceForRecIICalculation();

	VERBOSE_PRINT(errs() << "[][][][dynamicDatapath] Finished\n");

#ifdef DBG_PRINT_ALL
	printDatabase();
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

#ifdef DBG_PRINT_ALL
void DynamicDatapath::printDatabase() {
	errs() << "-- microops\n";
	for(auto const &x : microops)
		errs() << "-- " << std::to_string(x) << "\n";
	errs() << "-- --------\n";
	errs() << "-- nameToVertex\n";
	for(auto const &x : nameToVertex)
		errs() << "-- " << std::to_string(x.first) << ": " << x.second << "\n";
	errs() << "-- ------------\n";
	errs() << "-- vertexToName\n";
	VertexIterator vi, viEnd;
	for(std::tie(vi, viEnd) = vertices(graph); vi != viEnd; vi++)
		errs() << "-- " << vertexToName[*vi] << "\n";
	errs() << "-- ------------\n";
	errs() << "-- edgeToParamID\n";
	EdgeIterator ei, eiEnd;
	for(std::tie(ei, eiEnd) = edges(graph); ei != eiEnd; ei++)
		errs() << "-- " << std::to_string(edgeToParamID[*ei]) << "\n";
	errs() << "-- -------------\n";
	errs() << "-- functionNames\n";
	for(auto const &x : functionNames)
		errs() << "-- " << x << "\n";
	errs() << "-- -------------\n";
	errs() << "-- baseAddress\n";
	for(auto const &x : baseAddress)
		errs() << "-- " << std::to_string(x.first) << ": <" << x.second.first << ", " << std::to_string(x.second.second) << ">\n";
	errs() << "-- -----------\n";
	errs() << "-- noPartitionArrayName\n";
	for(auto const &x : noPartitionArrayName)
		errs() << "-- " << x << "\n";
	errs() << "-- --------------------\n";
}
#endif
