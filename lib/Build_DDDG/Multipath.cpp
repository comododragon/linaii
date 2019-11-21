#include "profile_h/Multipath.h"

void Multipath::_Multipath() {
	VERBOSE_PRINT(errs() << "[][][][multipath] Analysing DDDG for loop \"" << loopName << "\"\n");

	recursiveLookup(firstNonPerfectLoopLevel, loopLevel);

	VERBOSE_PRINT(errs() << "[][][][multipath] Performing final latency calculation\n");

	numCycles = 1;
#ifdef CHECK_MULTIPATH_STATE
	unsigned checkerState = 0;
#endif
	for(unsigned i = 0; i < latencies.size(); i++) {
		std::tuple<unsigned, unsigned, uint64_t, uint64_t> elem = latencies[i];
		unsigned currLoopLevel = std::get<0>(elem);
		unsigned latencyType = std::get<1>(elem);
		uint64_t latency = std::get<2>(elem);
		uint64_t maxII = std::get<3>(elem);

		std::string wholeLoopName = appendDepthToLoopName(loopName, currLoopLevel);
		wholeloopName2loopBoundMapTy::iterator found = wholeloopName2loopBoundMap.find(wholeLoopName);
		assert(found != wholeloopName2loopBoundMap.end() && "Could not find loop in wholeloopName2loopBoundMap");

		uint64_t loopBound = found->second;
		unsigned currUnrollFactor = unrolls.at(currLoopLevel - 1);

#ifdef CHECK_MULTIPATH_STATE
		if(0 == checkerState) {
			assert(BaseDatapath::NORMAL_LOOP == latencyType && "Multipath state checking failed: was expecting BaseDatapath::NORMAL_LOOP");
			checkerState = 1;
		}
		else if(1 == checkerState) {
			assert((BaseDatapath::PERFECT_LOOP == latencyType || BaseDatapath::NON_PERFECT_BEFORE == latencyType) &&
				"Multipath state checking failed: was expecting BaseDatapath::NORMAL_LOOP or BaseDatapath::NON_PERFECT_BEFORE");

			if(BaseDatapath::NON_PERFECT_BEFORE == latencyType) {
				assert(i + 1 < latencies.size() && "Invalid latency info structure: BaseDatapath::NON_PERFECT_BEFORE was provided, but the rest was not");

				unsigned afterLoopLevel = std::get<0>(latencies[i + 1]);
				unsigned afterLatencyType = std::get<1>(latencies[i + 1]);

				assert(BaseDatapath::NON_PERFECT_AFTER == afterLatencyType && "Invalid latency info structure: BaseDatapath::NON_PERFECT_BEFORE was provided, but the rest was not");
				assert(afterLoopLevel == currLoopLevel &&
					"Invalid latency info structure: BaseDatapath::NON_PERFECT_BEFORE and BaseDatapath::NON_PERFECT_AFTER have different loop levels");

				if(currUnrollFactor > 1) {
					assert(i + 2 < latencies.size() && "Invalid latency info structure: Unroll enabled for this loop level, but BaseDatapath::NON_PERFECT_BETWEEN was not provided");

					unsigned betweenLoopLevel = std::get<0>(latencies[i + 2]);
					unsigned betweenLatencyType = std::get<1>(latencies[i + 2]);

					assert(BaseDatapath::NON_PERFECT_BETWEEN == betweenLatencyType &&
						"Invalid latency info structure: Unroll enabled for this loop level, but BaseDatapath::NON_PERFECT_BETWEEN was not provided");
					assert(betweenLoopLevel == currLoopLevel &&
						"Invalid latency info structure: BaseDatapath::NON_PERFECT_BEFORE and BaseDatapath::NON_PERFECT_BETWEEN have different loop levels");
				}
			}
		}
#endif

		if(BaseDatapath::NORMAL_LOOP == latencyType) {
			uint64_t unrolledBound = loopBound / currUnrollFactor;

			if(enablePipelining)
				numCycles = maxII * (unrolledBound - 1) + latency + BaseDatapath::EXTRA_ENTER_EXIT_LOOP_LATENCY;
			else
				numCycles = latency * unrolledBound + BaseDatapath::EXTRA_ENTER_EXIT_LOOP_LATENCY;
		}
		else if(BaseDatapath::PERFECT_LOOP == latencyType) {
			numCycles = numCycles * loopBound + BaseDatapath::EXTRA_ENTER_EXIT_LOOP_LATENCY;
			// We consider EXTRA_ENTER_EXIT_LOOP_LATENCY as the overhead latency for a loop. When two consecutive loops
			// are present, a cycle for each loop overhead can be merged (i.e. the exit condition of a loop can be evaluated
			// at the same time as the enter condition of the following loop). Since right now consecutive inner loops are only
			// possible with unroll, we compensate this cycle difference with the loop unroll factor
			numCycles -= (currUnrollFactor - 1) * (loopBound / currUnrollFactor);
		}
		else if(BaseDatapath::NON_PERFECT_BEFORE == latencyType) {
			uint64_t afterLatency = std::get<2>(latencies[i + 1]);

			uint64_t betweenLatency = 0;
			if(currUnrollFactor > 1)
				betweenLatency = std::get<2>(latencies[i + 2]);

			// These in-between DDDGs may be solved at the same time as the enter/exit loop procedures. But this depends on
			// which of them are non-zero
			if(latency && afterLatency) {
				latency -= 1;
				afterLatency -= 1;

				if(betweenLatency > 1)
					betweenLatency -= 2;
				else if(betweenLatency)
					betweenLatency -= 1;
			}
			else if(latency && !afterLatency) {
				latency -= 1;

				if(betweenLatency > 1)
					betweenLatency -= 2;
				else if(betweenLatency)
					betweenLatency -= 1;
			}
			else if(!latency && afterLatency) {
				afterLatency -= 2;

				if(betweenLatency > 2)
					betweenLatency -= 3;
				else if(betweenLatency > 1)
					betweenLatency -= 2;
				else if(betweenLatency)
					betweenLatency -= 1;
			}

			numCycles = (latency + afterLatency + (betweenLatency * (currUnrollFactor - 1)) + (numCycles * currUnrollFactor)) * (loopBound / currUnrollFactor)
				+ BaseDatapath::EXTRA_ENTER_EXIT_LOOP_LATENCY;

			// We have read some elements in front of us, advance the index
			i += (currUnrollFactor > 1)? 2 : 1;
		}
	}

	// Finish latency calculation by multiplying the loop bounds that were above our analysis
	for(unsigned i = firstNonPerfectLoopLevel - 2; i + 1; i--) {
		std::string wholeLoopName = appendDepthToLoopName(loopName, i + 1);
		wholeloopName2loopBoundMapTy::iterator found = wholeloopName2loopBoundMap.find(wholeLoopName);
		assert(found != wholeloopName2loopBoundMap.end() && "Could not find loop in wholeloopName2loopBoundMap");

		uint64_t loopBound = found->second;
		unsigned currUnrollFactor = unrolls.at(i);

		numCycles = numCycles * loopBound + BaseDatapath::EXTRA_ENTER_EXIT_LOOP_LATENCY;

		// See explanation above, in the if(BaseDatapath::PERFECT_LOOP == ...)
		numCycles -= (currUnrollFactor - 1) * (loopBound / currUnrollFactor);
	}

	// If there are any out-bursts for the top loop level, take into account here
	uint64_t extraEnter = 0;
	uint64_t extraExit = 0;
	for(auto &it : nodesToBeforeDDDG) {
		unsigned latency = it.node.nonSilentLatency;
		if(latency >= extraEnter)
			extraEnter = latency;
	}
	for(auto &it : nodesToAfterDDDG) {
		unsigned latency = it.node.nonSilentLatency;
		if(latency >= extraExit)
			extraExit = latency;
	}
	numCycles += extraEnter + extraExit;

	// Remove the enter/exit loop latency that was added to the top loop
	numCycles -= BaseDatapath::EXTRA_ENTER_EXIT_LOOP_LATENCY;

	for(auto &it : P.getStructure()) {
		std::string name = std::get<0>(it);
		unsigned mergeType = std::get<1>(it);
		unsigned type = std::get<2>(it);

		if(Pack::MERGE_EQUAL == mergeType) {
			if(Pack::TYPE_UNSIGNED == type) {
				assert("true" == P.mergeElements<uint64_t>(name) && "Merged values from datapaths differ where it should not differ");
				VERBOSE_PRINT(errs() << "\t" << name << ": " << std::to_string(P.getElements<uint64_t>(name)[0]) << "\n");
			}
			else if(Pack::TYPE_SIGNED == type) {
				assert("true" == P.mergeElements<int64_t>(name) && "Merged values from datapaths differ where it should not differ");
				VERBOSE_PRINT(errs() << "\t" << name << ": " << std::to_string(P.getElements<int64_t>(name)[0]) << "\n");
			}
			else if(Pack::TYPE_FLOAT == type) {
				assert("true" == P.mergeElements<float>(name) && "Merged values from datapaths differ where it should not differ");
				VERBOSE_PRINT(errs() << "\t" << name << ": " << std::to_string(P.getElements<float>(name)[0]) << "\n");
			}
			else if(Pack::TYPE_STRING == type) {
				assert("true" == P.mergeElements<std::string>(name) && "Merged values from datapaths differ where it should not differ");
				VERBOSE_PRINT(errs() << "\t" << name << ": " << P.getElements<std::string>(name)[0] << "\n");
			}
		}
		else {
			if(Pack::TYPE_UNSIGNED == type) {
				VERBOSE_PRINT(errs() << "\t" << name << ": " << P.mergeElements<uint64_t>(name) << "\n");
			}
			else if(Pack::TYPE_SIGNED == type) {
				VERBOSE_PRINT(errs() << "\t" << name << ": " << P.mergeElements<int64_t>(name) << "\n");
			}
			else if(Pack::TYPE_FLOAT == type) {
				VERBOSE_PRINT(errs() << "\t" << name << ": " << P.mergeElements<float>(name) << "\n");
			}
			else if(Pack::TYPE_STRING == type) {
				std::string mergeResult = P.mergeElements<std::string>(name);
				VERBOSE_PRINT(errs() << "\t" << name << ": " << (("" == mergeResult)? "none" : mergeResult) << "\n");
			}
		}
	}

	VERBOSE_PRINT(errs() << "[][][][multipath] Finished\n");

#ifdef DBG_PRINT_ALL
	printDatabase();
#endif
}

// XXX: This may be a good place to implement some global optimisations.
// For example, the gemm case, where loadstoreinstcombine transforms the last store in the inner loop as a phi-store in the header, that could be detected here.
// Performing this optimisation post-DDDG may have some benefits, for example being sensitive to post-DDDG optimisations such as unroll, pipeline, etc.
// For now, I am performing instcombine before DDDG generation due to simplicity.
// One approach for global optimisation is: in this method, generate the DynamicDatapath but stop RIGHT AFTER DDDG is generated. Save all DDs in a vector
// Then, iterate over all DDs in loop nest order and detect for optimisations. Optimise, then schedule.
// One possible issue is regarding the global variables that are used. We must ensure that one DD messing in the global vars does not interfere with the other DDs
// And this is why I hate global variables...
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

		VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(finalLoopLevel) << "] Estimated cycles (might include bursts outside this loop): " << std::to_string(DD.getCycles()) << "\n");
		VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(finalLoopLevel) << "] Finished\n");

		latencies.push_back(std::make_tuple(finalLoopLevel, BaseDatapath::NORMAL_LOOP, DD.getRCIL(), DD.getMaxII()));
		P.merge(DD.getPack());
		// If there are out-bursts, save them as they will be useful later
		nodesToBeforeDDDG = std::vector<MemoryModel::nodeExportTy>(DD.getExportedNodesToBeforeDDDG());
		nodesToAfterDDDG = std::vector<MemoryModel::nodeExportTy>(DD.getExportedNodesToAfterDDDG());

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

		// XXX: SOMEWHERE AROUND HERE, the memory model must be consulted
		// XXX: for example, if the memory model says that writeresp should be outside of the loop, it should be accounted here
		// XXX: but also in the getLoopTotalLatency in BaseDatapath

		if(currIsPerfect) {
			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] This loop nest is perfect. Proceeding to next level\n");

			recursiveLookup(currLoopLevel + 1, finalLoopLevel);
			latencies.push_back(std::make_tuple(currLoopLevel, BaseDatapath::PERFECT_LOOP, 0, 0));

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Finished\n");
		}
		else {
			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] This loop nest is not perfect. Starting non-perfect loop analysis\n");

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the nested loop\n");
			recursiveLookup(currLoopLevel + 1, finalLoopLevel);

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the region before the nested loop\n");
			DynamicDatapath DD(kernelName, CM, summaryFile, loopName, currLoopLevel, targetUnrollFactor, nodesToBeforeDDDG, BaseDatapath::NON_PERFECT_BEFORE);
			latencies.push_back(std::make_tuple(currLoopLevel, BaseDatapath::NON_PERFECT_BEFORE, DD.getRCIL(), 0));
			P.merge(DD.getPack());

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the region after the nested loop\n");
			DynamicDatapath DD2(kernelName, CM, summaryFile, loopName, currLoopLevel, targetUnrollFactor, nodesToAfterDDDG, BaseDatapath::NON_PERFECT_AFTER);
			latencies.push_back(std::make_tuple(currLoopLevel, BaseDatapath::NON_PERFECT_AFTER, DD2.getRCIL(), 0));
			P.merge(DD2.getPack());

			// Unroll detected. Since the code is statically replicated, we also calculate the inter-iteration scheduling to improve acurracy
			if(targetUnrollFactor > 1) {
				std::vector<MemoryModel::nodeExportTy> nodesToImport;
				nodesToImport.insert(nodesToImport.end(), nodesToBeforeDDDG.begin(), nodesToBeforeDDDG.end());
				nodesToImport.insert(nodesToImport.end(), nodesToAfterDDDG.begin(), nodesToAfterDDDG.end());

				VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the region between the unrolled nested loops\n");
				DynamicDatapath DD3(kernelName, CM, summaryFile, loopName, currLoopLevel, targetUnrollFactor, nodesToImport, BaseDatapath::NON_PERFECT_BETWEEN);
				latencies.push_back(std::make_tuple(currLoopLevel, BaseDatapath::NON_PERFECT_BETWEEN, DD3.getRCIL(), 0));
				P.merge(DD3.getPack());
			}

			// TODO: I'm just adding the detected out-bursts to the vectors, without properly checking
			// overlappingness. Maybe we should think better about that
			// If there are out-bursts, save them as they will be useful later
			nodesToBeforeDDDG = std::vector<MemoryModel::nodeExportTy>(DD.getExportedNodesToBeforeDDDG());
			nodesToBeforeDDDG.insert(nodesToBeforeDDDG.end(), DD2.getExportedNodesToBeforeDDDG().begin(), DD2.getExportedNodesToBeforeDDDG().end());
			nodesToAfterDDDG = std::vector<MemoryModel::nodeExportTy>(DD2.getExportedNodesToAfterDDDG());
			nodesToAfterDDDG.insert(nodesToAfterDDDG.end(), DD.getExportedNodesToAfterDDDG().begin(), DD.getExportedNodesToAfterDDDG().end());

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
			case Pack::MERGE_NONE:
				aggrType = "MERGE_NONE";
				break;
			case Pack::MERGE_MAX:
				aggrType = "MERGE_MAX";
				break;
			case Pack::MERGE_MIN:
				aggrType = "MERGE_MIN";
				break;
			case Pack::MERGE_SUM:
				aggrType = "MERGE_SUM";
				break;
			case Pack::MERGE_EQUAL:
				aggrType = "MERGE_EQUAL";
				break;
			case Pack::MERGE_SET:
				aggrType = "MERGE_SET";
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
				for(auto &x2 : P.getElements<uint64_t>(std::get<0>(x))) {
					if(firstElem) {
						errs() << std::to_string(x2);
						firstElem = false;
					}
					else {
						errs() << ", " << std::to_string(x2);
					}
				}
				errs() << "\n";
				errs() << "---- Merged: " << P.mergeElements<uint64_t>(std::get<0>(x)) << "\n";
				break;
			case Pack::TYPE_SIGNED:
				errs() << "---- ";
				for(auto &x2 : P.getElements<int64_t>(std::get<0>(x))) {
					if(firstElem) {
						errs() << std::to_string(x2);
						firstElem = false;
					}
					else {
						errs() << ", " << std::to_string(x2);
					}
				}
				errs() << "\n";
				errs() << "---- Merged: " << P.mergeElements<int64_t>(std::get<0>(x)) << "\n";
				break;
			case Pack::TYPE_FLOAT:
				errs() << "---- ";
				for(auto &x2 : P.getElements<float>(std::get<0>(x))) {
					if(firstElem) {
						errs() << std::to_string(x2);
						firstElem = false;
					}
					else {
						errs() << ", " << std::to_string(x2);
					}
				}
				errs() << "\n";
				errs() << "---- Merged: " << P.mergeElements<float>(std::get<0>(x)) << "\n";
				break;
			case Pack::TYPE_STRING:
				errs() << "---- ";
				for(auto &x2 : P.getElements<std::string>(std::get<0>(x))) {
					if(firstElem) {
						errs() << x2;
						firstElem = false;
					}
					else {
						errs() << ", " << x2;
					}
				}
				errs() << "\n";
				errs() << "---- Merged: " << P.mergeElements<std::string>(std::get<0>(x)) << "\n";
				break;
		}
	}
	errs() << "-- -\n";
}
#endif
