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
	// XXX DEBUUUUUUUUUUUUGGGGGGGGGGGGGGGGGGGGGGGGGG
	//return;

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
	// XXX DEBUUUUUUUUUUUUGGGGGGGGGGGGGGGGGGGGGGGGGG
	//return;
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

// XXX: enablePipeliningand asapII are not set here. See the new BaseDapath constructor comments for the reason
DynamicDatapath::DynamicDatapath(
	std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
	std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor, unsigned datapathType
) : BaseDatapath(kernelName, CM, summaryFile, loopName, loopLevel, loopUnrollFactor, datapathType) {
	VERBOSE_PRINT(errs() << "\tBuild initial DDDG\n");

	std::string traceFileName = args.workDir + FILE_DYNAMIC_TRACE;
	gzFile traceFile;

	traceFile = gzopen(traceFileName.c_str(), "r");
	assert(traceFile != Z_NULL && "Could not open trace input file");

	builder = new DDDGBuilder(this, PC);
	intervalTy interval;
	if(NON_PERFECT_BEFORE == datapathType)
		interval = builder->getTraceLineFromToBeforeNestedLoop(traceFile);
	else if(NON_PERFECT_BETWEEN == datapathType)
		interval = builder->getTraceLineFromToBetweenAfterAndBefore(traceFile);
	else if(NON_PERFECT_AFTER == datapathType)
		interval = builder->getTraceLineFromToAfterNestedLoop(traceFile);
	else
		assert(false && "Invalid type of datapath passed to this type of dynamic datapath constructor");

#if 0
	if(NON_PERFECT_BEFORE == datapathType)
		std::cout << "!!!!!!!!! BEFORE " << loopName << " " << std::to_string(loopLevel) << " byteFrom: " << std::to_string(std::get<0>(interval)) << " to: " << std::to_string(std::get<1>(interval)) << " instCount: " << std::to_string(std::get<2>(interval)) << "\n";
	else if(NON_PERFECT_BETWEEN == datapathType)
		std::cout << "!!!!!!!!! BETWEEN " << loopName << " " << std::to_string(loopLevel) << " byteFrom: " << std::to_string(std::get<0>(interval)) << " to: " << std::to_string(std::get<1>(interval)) << " instCount: " << std::to_string(std::get<2>(interval)) << "\n";
	else if(NON_PERFECT_AFTER == datapathType)
		std::cout << "!!!!!!!!! AFTER " << loopName << " " << std::to_string(loopLevel) << " byteFrom: " << std::to_string(std::get<0>(interval)) << " to: " << std::to_string(std::get<1>(interval)) << " instCount: " << std::to_string(std::get<2>(interval)) << "\n";
#endif
	// XXX DEBUUUUUUUUUUUUGGGGGGGGGGGGGGGGGGGGGGGGGG
	//return;

	builder->buildInitialDDDG(interval);
	delete builder;
	builder = nullptr;

	postDDDGBuild();

	VERBOSE_PRINT(errs() << "[][][][][dynamicDatapath] Analysing DDDG for loop \"" << loopName << "\"\n");

	initBaseAddress();

	if(args.showPreOptDDDG)
		dumpGraph();

	numCycles = fpgaEstimation();

	VERBOSE_PRINT(errs() << "[][][][][dynamicDatapath] Finished\n");

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
