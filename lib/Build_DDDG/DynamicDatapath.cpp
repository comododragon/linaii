#include "profile_h/DynamicDatapath.h"

#ifdef USE_FUTURE
DynamicDatapath::DynamicDatapath(
	std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
	std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor, FutureCache *future
) : BaseDatapath(kernelName, CM, summaryFile, loopName, loopLevel, loopUnrollFactor, future, false, 0) {
	VERBOSE_PRINT(errs() << "[][][][dynamicDatapath] Analysing DDDG for loop \"" << loopName << "\"\n");
#else
DynamicDatapath::DynamicDatapath(
	std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
	std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor
) : BaseDatapath(kernelName, CM, summaryFile, loopName, loopLevel, loopUnrollFactor, false, 0) {
	VERBOSE_PRINT(errs() << "[][][][dynamicDatapath] Analysing DDDG for loop \"" << loopName << "\"\n");
#endif

	initBaseAddress();

	VERBOSE_PRINT(errs() << "[][][][dynamicDatapath] Analysing recurrence-constrained II\n");
	asapII = fpgaEstimationOneMoreSubtraceForRecIICalculation();

	VERBOSE_PRINT(errs() << "[][][][dynamicDatapath] Finished\n");

#ifdef DBG_PRINT_ALL
	printDatabase();
#endif
}

#ifdef USE_FUTURE
DynamicDatapath::DynamicDatapath(
	std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
	std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor,
	FutureCache *future,
	bool enablePipelining, uint64_t asapII
) : BaseDatapath(kernelName, CM, summaryFile, loopName, loopLevel, loopUnrollFactor, future, enablePipelining, asapII) {
	if(future && future->isComputed())
		VERBOSE_PRINT(errs() << "[][][][dynamicDatapath] Using computed cache from previous dynamic datapath constructions\n");

#else
DynamicDatapath::DynamicDatapath(
	std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
	std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor,
	bool enablePipelining, uint64_t asapII
) : BaseDatapath(kernelName, CM, summaryFile, loopName, loopLevel, loopUnrollFactor, enablePipelining, asapII) {
#endif
	VERBOSE_PRINT(errs() << "[][][][dynamicDatapath] Analysing DDDG for loop \"" << loopName << "\"\n");

	initBaseAddress();

	if(args.showPreOptDDDG)
		dumpGraph();

	numCycles = fpgaEstimation();

	VERBOSE_PRINT(errs() << "[][][][dynamicDatapath] Finished\n");

#ifdef DBG_PRINT_ALL
	printDatabase();
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
	errs() << "-- edgeToWeight\n";
	EdgeIterator ei, eiEnd;
	for(std::tie(ei, eiEnd) = edges(graph); ei != eiEnd; ei++)
		errs() << "-- " << std::to_string(edgeToWeight[*ei]) << "\n";
	errs() << "-- ------------\n";
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
	errs() << "-- dynamicMemoryOps\n";
	for(auto const &x : dynamicMemoryOps)
		errs() << "-- " << x << "\n";
	errs() << "-- ----------------\n";
	errs() << "-- asapScheduledTime\n";
	for(auto const &x : asapScheduledTime)
		errs() << "-- " << std::to_string(x) << "\n";
	errs() << "-- -----------------\n";
	errs() << "-- alapScheduledTime\n";
	for(auto const &x : alapScheduledTime)
		errs() << "-- " << std::to_string(x) << "\n";
	errs() << "-- -----------------\n";
	errs() << "-- cPathNodes\n";
	for(auto const &x : cPathNodes)
		errs() << "-- " << std::to_string(x) << "\n";
	errs() << "-- ----------\n";
	errs() << "-- rcScheduledTime\n";
	for(auto const &x : rcScheduledTime)
		errs() << "-- " << std::to_string(x) << "\n";
	errs() << "-- ---------------\n";
}
#endif
