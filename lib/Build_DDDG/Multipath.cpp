#include "profile_h/Multipath.h"

void Multipath::_Multipath() {
	VERBOSE_PRINT(errs() << "[][][][multipath] Analysing DDDG for loop \"" << loopName << "\"\n");

	std::vector<std::tuple<unsigned, unsigned, uint64_t>> pack;
	recursiveLookup(pack, firstNonPerfectLoopLevel, loopLevel);

	VERBOSE_PRINT(errs() << "[][][][multipath] Finished\n");

#ifdef DBG_PRINT_ALL
	printDatabase();
#endif
}

// TODO: change from cycles vector to whichever structure is necessary for merging all useful infos (memory efficiency, LUT counts, etc)
// TODO: i.e. change from uint64_t to a struct containing all needed info
void Multipath::recursiveLookup(std::vector<std::tuple<unsigned, unsigned, uint64_t>> &pack, unsigned currLoopLevel, unsigned finalLoopLevel) {
	unsigned recII = 0;

	// Final requested loop level. The logic here is the same as the usual perfect-loops analysis
	if(currLoopLevel >= finalLoopLevel) {
		VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(finalLoopLevel) << "] Generating normal DDDG for this loop chain\n");

		if(enablePipelining) {
			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(finalLoopLevel) << "] Building dynamic datapath for recurrence-constrained II calculation\n");

			DynamicDatapath DD(kernelName, CM, summaryFile, loopName, finalLoopLevel, actualLoopUnrollFactor);
			recII = DD.getASAPII();

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(finalLoopLevel) << "] Recurrence-constrained II: " << recII << "\n");
		}

		VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(finalLoopLevel) << "] Building dynamic datapath\n");

		DynamicDatapath DD(kernelName, CM, summaryFile, loopName, finalLoopLevel, loopUnrollFactor, enablePipelining, recII);

		VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(finalLoopLevel) << "] Estimated cycles: " << std::to_string(DD.getCycles()) << "\n");
		VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(finalLoopLevel) << "] Finished\n");

		pack.push_back(std::make_tuple(finalLoopLevel, DynamicDatapath::NORMAL_LOOP, DD.getCycles()));

		return;
	}
	else {
		std::string wholeLoopName = appendDepthToLoopName(loopName, currLoopLevel);
		wholeloopName2perfectOrNotMapTy::iterator found = wholeloopName2perfectOrNotMap.find(wholeLoopName);
		assert(found != wholeloopName2perfectOrNotMap.end() && "Could not find loop in wholeloopName2perfectOrNotMap");
		wholeloopName2loopBoundMapTy::iterator found2 = wholeloopName2loopBoundMap.find(wholeLoopName);
		assert(found2 != wholeloopName2loopBoundMap.end() && "Could not find loop in wholeloopName2loopBoundMap");
		bool currIsPerfect = found->second;
		uint64_t currLoopBound = found2->second;
		unsigned currUnrollFactor = unrolls.at(currLoopLevel - 1);
		unsigned targetUnrollFactor = (currLoopBound < currUnrollFactor && currLoopBound)? currLoopBound : currUnrollFactor;

		if(currIsPerfect) {
			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] This loop nest is perfect. Proceeding to next level\n");

			pack.push_back(std::make_tuple(currLoopLevel, DynamicDatapath::PERFECT_LOOP, 0));
			recursiveLookup(pack, currLoopLevel + 1, finalLoopLevel);

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Finished\n");
		}
		else {
			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] This loop nest is not perfect. Starting non-perfect loop analysis\n");

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the region before the nested loop\n");
			DynamicDatapath(kernelName, CM, summaryFile, loopName, currLoopLevel, targetUnrollFactor, DynamicDatapath::NON_PERFECT_BEFORE);
			// TODO
			//pack.push_back(std::make_tuple(currLoopLevel, DynamicDatapath::NON_PERFECT_BEFORE, DD.getCycles()));

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the nested loop\n");
			recursiveLookup(pack, currLoopLevel + 1, finalLoopLevel);

			// Unroll detected. Since the code is statically replicated, we also calculate the inter-iteration scheduling to improve acurracy
			if(targetUnrollFactor > 1) {
				VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the region between the unrolled nested loops\n");
				DynamicDatapath(kernelName, CM, summaryFile, loopName, currLoopLevel, targetUnrollFactor, DynamicDatapath::NON_PERFECT_BETWEEN);
				// TODO
				//pack.push_back(std::make_tuple(currLoopLevel, DynamicDatapath::NON_PERFECT_BETWEEN, DD.getCycles()));
			}

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the region after the nested loop\n");
			DynamicDatapath(kernelName, CM, summaryFile, loopName, currLoopLevel, targetUnrollFactor, DynamicDatapath::NON_PERFECT_AFTER);
			// TODO
			//pack.push_back(std::make_tuple(currLoopLevel, DynamicDatapath::NON_PERFECT_AFTER, DD.getCycles()));

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Finished\n");
		}

		return;
	}
}

Multipath::Multipath(
	std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
	std::string loopName, unsigned loopLevel, unsigned firstNonPerfectLoopLevel,
	uint64_t loopUnrollFactor, std::vector<unsigned> &unrolls, uint64_t actualLoopUnrollFactor
) :
	kernelName(kernelName), CM(CM), summaryFile(summaryFile),
	loopName(loopName), loopLevel(loopLevel), firstNonPerfectLoopLevel(firstNonPerfectLoopLevel),
	loopUnrollFactor(loopUnrollFactor), unrolls(unrolls), actualLoopUnrollFactor(actualLoopUnrollFactor),
	enablePipelining(true)
{
	_Multipath();
}

Multipath::Multipath(
	std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
	std::string loopName, unsigned loopLevel, unsigned firstNonPerfectLoopLevel,
	uint64_t loopUnrollFactor, std::vector<unsigned> &unrolls
) :
	kernelName(kernelName), CM(CM), summaryFile(summaryFile),
	loopName(loopName), loopLevel(loopLevel), firstNonPerfectLoopLevel(firstNonPerfectLoopLevel),
	loopUnrollFactor(loopUnrollFactor), unrolls(unrolls),
	enablePipelining(false)
{
	_Multipath();
}

Multipath::~Multipath() {}

uint64_t Multipath::getCycles() const {
	return numCycles;
}

#ifdef DBG_PRINT_ALL
void Multipath::printDatabase() {
}
#endif
