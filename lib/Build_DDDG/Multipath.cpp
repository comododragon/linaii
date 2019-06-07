#include "profile_h/Multipath.h"

Multipath::Multipath(
	std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
	std::string loopName, unsigned loopLevel, unsigned firstNonPerfectLoopLevel, uint64_t loopUnrollFactor,
	bool enablePipelining, uint64_t asapII
) {
	VERBOSE_PRINT(errs() << "[][][][multipath] Analysing DDDG for loop \"" << loopName << "\"\n");

	// Generating datapaths top-down
	for(unsigned i = firstNonPerfectLoopLevel; i <= loopLevel; i++) {
		VERBOSE_PRINT(errs() << "[][][][multipath][ " << std::to_string(i) << "] Analysing loop depth " << std::to_string(i) << "\n";

		// TODO parei aqui
		// Primeiro testar os getTraceLineFromTo: before, normal, e after
		// Tambem fazer a lógica para usar o getTraceLineFromTo especial para unrolled loops
		// E colocar os VERBOSE_PRINTs para cada estágio e deixar bonitinho
		
		VERBOSE_PRINT(errs() << "[][][][multipath][ " << std::to_string(i) << "] Finished\n";
	}

	VERBOSE_PRINT(errs() << "[][][][multipath] Finished\n");

#ifdef DBG_PRINT_ALL
	printDatabase();
#endif
}

Multipath::~Multipath() {}

uint64_t Multipath::getCycles() const {
	return numCycles;
}

#ifdef DBG_PRINT_ALL
void Multipath::printDatabase() {
}
#endif
