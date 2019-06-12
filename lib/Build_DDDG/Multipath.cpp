#include "profile_h/Multipath.h"

void Multipath::_Multipath() {
	VERBOSE_PRINT(errs() << "[][][][multipath] Analysing DDDG for loop \"" << loopName << "\"\n");

	recursiveLookup(firstNonPerfectLoopLevel, loopLevel);

	VERBOSE_PRINT(errs() << "[][][][multipath] Finished\n");

#ifdef DBG_PRINT_ALL
	printDatabase();
#endif
}

void Multipath::recursiveLookup(unsigned currLoopLevel, unsigned finalLoopLevel) {
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

		latencies.push_back(std::make_tuple(finalLoopLevel, BaseDatapath::NORMAL_LOOP, DD.getRCIL()));
		P.merge(DD.getPack());

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

			latencies.push_back(std::make_tuple(currLoopLevel, BaseDatapath::PERFECT_LOOP, 0));
			recursiveLookup(currLoopLevel + 1, finalLoopLevel);

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Finished\n");
		}
		else {
			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] This loop nest is not perfect. Starting non-perfect loop analysis\n");

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the region before the nested loop\n");
			DynamicDatapath DD(kernelName, CM, summaryFile, loopName, currLoopLevel, targetUnrollFactor, BaseDatapath::NON_PERFECT_BEFORE);
			latencies.push_back(std::make_tuple(currLoopLevel, BaseDatapath::NON_PERFECT_BEFORE, DD.getRCIL()));
			P.merge(DD.getPack());

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the nested loop\n");
			recursiveLookup(currLoopLevel + 1, finalLoopLevel);

			// Unroll detected. Since the code is statically replicated, we also calculate the inter-iteration scheduling to improve acurracy
			if(targetUnrollFactor > 1) {
				VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the region between the unrolled nested loops\n");
				DynamicDatapath DD2(kernelName, CM, summaryFile, loopName, currLoopLevel, targetUnrollFactor, BaseDatapath::NON_PERFECT_BETWEEN);
				latencies.push_back(std::make_tuple(currLoopLevel, BaseDatapath::NON_PERFECT_BETWEEN, DD2.getRCIL()));
				P.merge(DD2.getPack());
			}

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the region after the nested loop\n");
			DynamicDatapath DD3(kernelName, CM, summaryFile, loopName, currLoopLevel, targetUnrollFactor, BaseDatapath::NON_PERFECT_AFTER);
			latencies.push_back(std::make_tuple(currLoopLevel, BaseDatapath::NON_PERFECT_AFTER, DD3.getRCIL()));
			P.merge(DD3.getPack());

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
	errs() << "-- latencies\n";
	for(auto const &x : latencies) {
		std::string loopType;

		switch(std::get<1>(x)) {
			case BaseDatapath::NORMAL_LOOP:
				loopType = "NORMAL_LOOP";
				break;
			case BaseDatapath::PERFECT_LOOP:
				loopType = "PERFECT_LOOP";
				break;
			case BaseDatapath::NON_PERFECT_BEFORE:
				loopType = "NON_PERFECT_BEFORE";
				break;
			case BaseDatapath::NON_PERFECT_BETWEEN:
				loopType = "NON_PERFECT_BETWEEN";
				break;
			case BaseDatapath::NON_PERFECT_AFTER:
				loopType = "NON_PERFECT_AFTER";
				break;
			default:
				loopType = "(unknown)";
				break;
		}

		errs() << "-- " << std::get<0>(x) << ": <" << loopType << ", " << std::to_string(std::get<2>(x)) << ">\n";
	}
	errs() << "-- ---------\n";

	errs() << "-- P\n";
	for(auto &x : P.getStructure()) {
		std::string aggrType, elemType;

		switch(std::get<1>(x)) {
			case Pack::AGGREGATE_NONE:
				aggrType = "AGGREGATE_NONE";
				break;
			case Pack::AGGREGATE_MAX:
				aggrType = "AGGREGATE_MAX";
				break;
			case Pack::AGGREGATE_MIN:
				aggrType = "AGGREGATE_MIN";
				break;
			case Pack::AGGREGATE_SUM:
				aggrType = "AGGREGATE_SUM";
				break;
			case Pack::AGGREGATE_EQUAL:
				aggrType = "AGGREGATE_EQUAL";
				break;
			case Pack::AGGREGATE_SET:
				aggrType = "AGGREGATE_SET";
				break;
		}

		switch(std::get<2>(x)) {
			case Pack::TYPE_UNSIGNED:
				elemType = "TYPE_UNSIGNED";
				break;
			case Pack::TYPE_SIGNED:
				elemType = "TYPE_SIGNED";
				break;
			case Pack::TYPE_FLOAT:
				elemType = "TYPE_FLOAT";
				break;
			case Pack::TYPE_STRING:
				elemType = "TYPE_STRING";
				break;
		}

		errs() << "-- " << std::get<0>(x) << ": <" << aggrType << ", " << elemType << ">\n";

		bool firstElem = true;
		switch(std::get<2>(x)) {
			case Pack::TYPE_UNSIGNED:
				errs() << "---- ";
				for(auto &x2 : P.getUnsignedElements(std::get<0>(x))) {
					if(firstElem) {
						errs() << std::to_string(x2);
						firstElem = false;
					}
					else {
						errs() << ", " << std::to_string(x2);
					}
				}
				errs() << "\n";
				errs() << "---- Aggregation: " << P.aggregateUnsignedElements(std::get<0>(x)) << "\n";
				break;
			case Pack::TYPE_SIGNED:
				errs() << "---- ";
				for(auto &x2 : P.getSignedElements(std::get<0>(x))) {
					if(firstElem) {
						errs() << std::to_string(x2);
						firstElem = false;
					}
					else {
						errs() << ", " << std::to_string(x2);
					}
				}
				errs() << "\n";
				errs() << "---- Aggregation: " << P.aggregateSignedElements(std::get<0>(x)) << "\n";
				break;
			case Pack::TYPE_FLOAT:
				errs() << "---- ";
				for(auto &x2 : P.getFloatElements(std::get<0>(x))) {
					if(firstElem) {
						errs() << std::to_string(x2);
						firstElem = false;
					}
					else {
						errs() << ", " << std::to_string(x2);
					}
				}
				errs() << "\n";
				errs() << "---- Aggregation: " << P.aggregateFloatElements(std::get<0>(x)) << "\n";
				break;
			case Pack::TYPE_STRING:
				errs() << "---- ";
				for(auto &x2 : P.getStringElements(std::get<0>(x))) {
					if(firstElem) {
						errs() << x2;
						firstElem = false;
					}
					else {
						errs() << ", " << x2;
					}
				}
				errs() << "\n";
				errs() << "---- Aggregation: " << P.aggregateStringElements(std::get<0>(x)) << "\n";
				break;
		}
	}
	errs() << "-- -\n";
}
#endif
