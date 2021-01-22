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
		std::vector<MemoryModel::nodeExportTy> nodesToBeforeDDDG;
		std::vector<MemoryModel::nodeExportTy> nodesToAfterDDDG;
		bool canOutBurstsOverlap;
		exportedNodesMapTy::iterator exportedFound = exportedNodes.find(currLoopLevel + 1);
		if(exportedFound != exportedNodes.end()) {
			nodesToBeforeDDDG = std::get<0>(exportedFound->second);
			nodesToAfterDDDG = std::get<1>(exportedFound->second);
			canOutBurstsOverlap = std::get<2>(exportedFound->second);
		}

		std::string wholeLoopName = appendDepthToLoopName(loopName, currLoopLevel);
		wholeloopName2loopBoundMapTy::iterator found = wholeloopName2loopBoundMap.find(wholeLoopName);
		assert(found != wholeloopName2loopBoundMap.end() && "Could not find loop in wholeloopName2loopBoundMap");

		uint64_t loopBound = found->second;
		unsigned currUnrollFactor = unrolls.at(currLoopLevel - 1);

#ifdef CHECK_MULTIPATH_STATE
		if(0 == checkerState) {
			assert(DatapathType::NORMAL_LOOP == latencyType && "Multipath state checking failed: was expecting DatapathType::NORMAL_LOOP");
			checkerState = 1;
		}
		else if(1 == checkerState) {
			assert((DatapathType::PERFECT_LOOP == latencyType || DatapathType::NON_PERFECT_BEFORE == latencyType) &&
				"Multipath state checking failed: was expecting DatapathType::NORMAL_LOOP or DatapathType::NON_PERFECT_BEFORE");

			if(DatapathType::NON_PERFECT_BEFORE == latencyType) {
				assert(i + 1 < latencies.size() && "Invalid latency info structure: DatapathType::NON_PERFECT_BEFORE was provided, but the rest was not");

				unsigned afterLoopLevel = std::get<0>(latencies[i + 1]);
				unsigned afterLatencyType = std::get<1>(latencies[i + 1]);

				assert(DatapathType::NON_PERFECT_AFTER == afterLatencyType && "Invalid latency info structure: DatapathType::NON_PERFECT_BEFORE was provided, but the rest was not");
				assert(afterLoopLevel == currLoopLevel &&
					"Invalid latency info structure: DatapathType::NON_PERFECT_BEFORE and DatapathType::NON_PERFECT_AFTER have different loop levels");

				if(currUnrollFactor > 1) {
					assert(i + 2 < latencies.size() && "Invalid latency info structure: Unroll enabled for this loop level, but DatapathType::NON_PERFECT_BETWEEN was not provided");

					unsigned betweenLoopLevel = std::get<0>(latencies[i + 2]);
					unsigned betweenLatencyType = std::get<1>(latencies[i + 2]);

					assert(DatapathType::NON_PERFECT_BETWEEN == betweenLatencyType &&
						"Invalid latency info structure: Unroll enabled for this loop level, but DatapathType::NON_PERFECT_BETWEEN was not provided");
					assert(betweenLoopLevel == currLoopLevel &&
						"Invalid latency info structure: DatapathType::NON_PERFECT_BEFORE and DatapathType::NON_PERFECT_BETWEEN have different loop levels");
				}
			}
		}
#endif

		if(!(args.fNoMMA)) {
			if(ArgPack::MMA_MODE_GEN == args.mmaMode) {
				VERBOSE_PRINT(errs() <<"[][][][multipath] \"--mma-mode\" is set to \"gen\", halting now\n");
				return;
			}
		}

		// TODO: Requires further testing
		if(DatapathType::NORMAL_LOOP == latencyType) {
			uint64_t unrolledBound = loopBound / currUnrollFactor;

			if(enablePipelining)
				numCycles = maxII * (unrolledBound - 1) + latency + BaseDatapath::EXTRA_ENTER_EXIT_LOOP_LATENCY;
			else
				numCycles = latency * unrolledBound + BaseDatapath::EXTRA_ENTER_EXIT_LOOP_LATENCY;
		}
		else if(DatapathType::PERFECT_LOOP == latencyType) {
			// Even though this is a perfect loop level, there might be exported nodes to be considered
			uint64_t extraEnter = BaseDatapath::EXTRA_ENTER_LOOP_LATENCY;
			uint64_t extraExit = BaseDatapath::EXTRA_EXIT_LOOP_LATENCY;
			bool allocatedDDDGBefore = false;
			bool allocatedDDDGAfter = false;
			// If there are out-burst nodes to allocate before DDDG, we add on the extra cycles
			for(auto &it : nodesToBeforeDDDG) {
				// We don't use profile->getLatency() here because the opcode might be silent
				// and here we want the non-silent case. We could call getNonSilentOpcode(),
				// but since we already have this calculated, why not use it?
				unsigned latency = it.node.nonSilentLatency;
				if(latency >= extraEnter)
					extraEnter = latency;

				allocatedDDDGBefore = true;
			}
			// If there are out-burst nodes to allocate after DDDG, we add on the extra cycles
			for(auto &it : nodesToAfterDDDG) {
				// Look explanation on the previous loop
				unsigned latency = it.node.nonSilentLatency;
				if(latency >= extraExit)
					extraExit = latency;

				allocatedDDDGAfter = true;
			}
			uint64_t extraEnterExit = extraEnter + extraExit;

			numCycles = numCycles * loopBound + extraEnterExit;
			// We consider EXTRA_ENTER_EXIT_LOOP_LATENCY as the overhead latency for a loop. When two consecutive loops
			// are present, a cycle for each loop overhead can be merged (i.e. the exit condition of a loop can be evaluated
			// at the same time as the enter condition of the following loop). Since right now consecutive inner loops are only
			// possible with unroll, we compensate this cycle difference with the loop unroll factor
			// TODO: same as BaseDatapath, this wasn't thoroughly tested!
			if((currUnrollFactor > 1) && !(allocatedDDDGBefore && allocatedDDDGAfter && canOutBurstsOverlap))
				numCycles -= (currUnrollFactor - 1) * std::min(extraEnter, extraExit) * (loopBound / currUnrollFactor);
		}
		else if(DatapathType::NON_PERFECT_BEFORE == latencyType) {
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

		uint64_t extraEnter = BaseDatapath::EXTRA_ENTER_LOOP_LATENCY;
		uint64_t extraExit = BaseDatapath::EXTRA_EXIT_LOOP_LATENCY;
		bool allocatedDDDGBefore = false;
		bool allocatedDDDGAfter = false;
		bool canOutBurstsOverlap = false;

		// For the first loop level right after the DDDGs, we analyse for out-bursts
		if(firstNonPerfectLoopLevel - 2 == i) {
			// TODO: should be here i + 1 or i + 2? Failing for now until a better thought is given
			// I think it is i + 2, otherwise it clashes with the logic right after this loop
			// But maybe if here is executed, it shouldn't run below (in this case, here would be i + 1)
			assert(false && "TODO!");
			exportedNodesMapTy::iterator exportedFound = exportedNodes.find(i + 2);
			if(exportedFound != exportedNodes.end()) {
				for(auto &it : std::get<0>(exportedFound->second)) {
					unsigned latency = it.node.nonSilentLatency;
					if(latency >= extraEnter)
						extraEnter = latency;

					allocatedDDDGBefore = true;
				}
				for(auto &it : std::get<1>(exportedFound->second)) {
					unsigned latency = it.node.nonSilentLatency;
					if(latency >= extraExit)
						extraExit = latency;

					allocatedDDDGAfter = true;
				}
				canOutBurstsOverlap = std::get<2>(exportedFound->second);
			}
		}

		uint64_t extraEnterExit = extraEnter + extraExit;

		numCycles = numCycles * loopBound + extraEnterExit;;

		// See explanation above, in the if(DatapathType::PERFECT_LOOP == ...)
		bool shouldShrink =
			(firstNonPerfectLoopLevel - 2 == i)? (currUnrollFactor > 1) && !(allocatedDDDGBefore && allocatedDDDGAfter && canOutBurstsOverlap) : true;
		if(shouldShrink)
			numCycles -= (currUnrollFactor - 1) * std::min(extraEnter, extraExit) * (loopBound / currUnrollFactor);
	}

	// Remove the enter/exit loop latency that was added to the top loop
	numCycles -= BaseDatapath::EXTRA_ENTER_EXIT_LOOP_LATENCY;

	// If the top-level out-bursted, we should take into account here
	uint64_t extraEnter = 0;
	uint64_t extraExit = 0;
	exportedNodesMapTy::iterator exportedFound = exportedNodes.find(1);
	if(exportedFound != exportedNodes.end()) {
		for(auto &it : std::get<0>(exportedFound->second)) {
			unsigned latency = it.node.nonSilentLatency;
			if(latency >= extraEnter)
				extraEnter = latency;
		}
		for(auto &it : std::get<1>(exportedFound->second)) {
			unsigned latency = it.node.nonSilentLatency;
			if(latency >= extraExit)
				extraExit = latency;
		}
	}
	uint64_t extraEnterExit = extraEnter + extraExit;
	DBG_DUMP("Top level loop additional cycles: " << extraEnter << " " << extraExit << "\n");
	numCycles += extraEnterExit;

	for(auto &it : P.getStructure()) {
		std::string name = std::get<0>(it);
		unsigned mergeType = std::get<1>(it);
		unsigned type = std::get<2>(it);

		// Names starting with "_" are not printed (used for other purposes)
		if('_' == name[0])
			continue;

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

	dumpSummary(numCycles);

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

			DynamicDatapath DD(kernelName, CM, CtxM, summaryFile, loopName, finalLoopLevel, actualLoopUnrollFactor);
			recII = DD.getASAPII();

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(finalLoopLevel) << "] Recurrence-constrained II: " << recII << "\n");
		}

		VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(finalLoopLevel) << "] Building dynamic datapath\n");

		DynamicDatapath DD(kernelName, CM, CtxM, summaryFile, loopName, finalLoopLevel, loopUnrollFactor, enablePipelining, recII);

		VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(finalLoopLevel) << "] Estimated cycles (might include bursts outside this loop): " << std::to_string(DD.getCycles()) << "\n");
		VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(finalLoopLevel) << "] Finished\n");

		latencies.push_back(std::make_tuple(finalLoopLevel, DatapathType::NORMAL_LOOP, DD.getRCIL(), DD.getMaxII()));
		P.merge(DD.getPack());
		// If there are out-bursts, save them as they will be useful later
		exportedNodes.insert(std::make_pair(finalLoopLevel, std::make_tuple(
			std::vector<MemoryModel::nodeExportTy>(DD.getExportedNodesToBeforeDDDG()),
			std::vector<MemoryModel::nodeExportTy>(DD.getExportedNodesToAfterDDDG()),
			MemoryModel::canOutBurstsOverlap(DD.getExportedNodesToBeforeDDDG(), DD.getExportedNodesToAfterDDDG())
		)));

		return;
	}
	else {
// XXX Different generations of NPLA logic, remove!
#if 1
		std::string wholeLoopName = appendDepthToLoopName(loopName, currLoopLevel);
		wholeloopName2loopBoundMapTy::iterator found = wholeloopName2loopBoundMap.find(wholeLoopName);
		assert(found != wholeloopName2loopBoundMap.end() && "Could not find loop in wholeloopName2loopBoundMap");
		uint64_t currLoopBound = found->second;
		unsigned currUnrollFactor = unrolls.at(currLoopLevel - 1);
		unsigned targetUnrollFactor = (currLoopBound < currUnrollFactor && currLoopBound)? currLoopBound : currUnrollFactor;

		// There used to be logic to control NPLA here, but for now it is always active as long --f-npla is set
		bool calculateBefore = true;
		bool calculateAfter = true;
		if(!calculateBefore && !calculateAfter) {
#else
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
#endif
			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] This loop nest is perfect. Proceeding to next level\n");

			recursiveLookup(currLoopLevel + 1, finalLoopLevel);
			latencies.push_back(std::make_tuple(currLoopLevel, DatapathType::PERFECT_LOOP, 0, 0));

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Finished\n");
		}
		else {
			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] This loop nest is not perfect. Starting non-perfect loop analysis\n");

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the nested loop\n");
			recursiveLookup(currLoopLevel + 1, finalLoopLevel);

			std::vector<MemoryModel::nodeExportTy> nodesToBeforeDDDG;
			std::vector<MemoryModel::nodeExportTy> nodesToAfterDDDG;
			exportedNodesMapTy::iterator exportedFound = exportedNodes.find(currLoopLevel + 1);
			if(exportedFound != exportedNodes.end()) {
				nodesToBeforeDDDG = std::get<0>(exportedFound->second);
				nodesToAfterDDDG = std::get<1>(exportedFound->second);
			}

			unsigned ddRCIL = 0;
			// XXX: If was commented since for now calculateBefore is always true (uncommenting the if will cause some scope errors that i did not solve)
			//if(calculateBefore) {
				VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the region before the nested loop\n");
				DynamicDatapath DD(kernelName, CM, CtxM, summaryFile, loopName, currLoopLevel, targetUnrollFactor, nodesToBeforeDDDG, DatapathType::NON_PERFECT_BEFORE);
				latencies.push_back(std::make_tuple(currLoopLevel, DatapathType::NON_PERFECT_BEFORE, DD.getRCIL(), 0));
				P.merge(DD.getPack());
			//}
			//else {
			//	VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Region before the nested loop not tagged for exploration, skipping\n");
			//	latencies.push_back(std::make_tuple(currLoopLevel, DatapathType::NON_PERFECT_BEFORE, 0, 0));
			//}

			unsigned dd2RCIL = 0;
			// XXX: If was commented since for now calculateAfter is always true (uncommenting the if will cause some scope errors that i did not solve)
			//if(calculateAfter) {
				VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the region after the nested loop\n");
				DynamicDatapath DD2(kernelName, CM, CtxM, summaryFile, loopName, currLoopLevel, targetUnrollFactor, nodesToAfterDDDG, DatapathType::NON_PERFECT_AFTER);
				latencies.push_back(std::make_tuple(currLoopLevel, DatapathType::NON_PERFECT_AFTER, DD2.getRCIL(), 0));
				P.merge(DD2.getPack());
			//}
			//else {
			//	VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Region after the nested loop not tagged for exploration, skipping\n");
			//	latencies.push_back(std::make_tuple(currLoopLevel, DatapathType::NON_PERFECT_AFTER, 0, 0));
			//}

			// Unroll detected. Since the code is statically replicated, we also calculate the inter-iteration scheduling to improve acurracy
			if(targetUnrollFactor > 1) {
				std::vector<MemoryModel::nodeExportTy> nodesToImport;
				nodesToImport.insert(nodesToImport.end(), nodesToBeforeDDDG.begin(), nodesToBeforeDDDG.end());
				nodesToImport.insert(nodesToImport.end(), nodesToAfterDDDG.begin(), nodesToAfterDDDG.end());

				if(ddRCIL || dd2RCIL) {
					VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Building dynamic datapath for the region between the unrolled nested loops\n");
					DynamicDatapath DD3(kernelName, CM, CtxM, summaryFile, loopName, currLoopLevel, targetUnrollFactor, nodesToImport, DatapathType::NON_PERFECT_BETWEEN);
					latencies.push_back(std::make_tuple(currLoopLevel, DatapathType::NON_PERFECT_BETWEEN, DD3.getRCIL(), 0));
					P.merge(DD3.getPack());
				}
				else {
					latencies.push_back(std::make_tuple(currLoopLevel, DatapathType::NON_PERFECT_BETWEEN, 0, 0));
				}
			}

			nodesToBeforeDDDG = std::vector<MemoryModel::nodeExportTy>(DD.getExportedNodesToBeforeDDDG());
			nodesToBeforeDDDG.insert(nodesToBeforeDDDG.end(), DD2.getExportedNodesToBeforeDDDG().begin(), DD2.getExportedNodesToBeforeDDDG().end());
			nodesToAfterDDDG = std::vector<MemoryModel::nodeExportTy>(DD2.getExportedNodesToAfterDDDG());
			nodesToAfterDDDG.insert(nodesToAfterDDDG.end(), DD.getExportedNodesToAfterDDDG().begin(), DD.getExportedNodesToAfterDDDG().end());
			bool canOutBurstsOverlap = MemoryModel::canOutBurstsOverlap(nodesToBeforeDDDG, nodesToAfterDDDG);
			exportedNodes.insert(std::make_pair(currLoopLevel, std::make_tuple(nodesToBeforeDDDG, nodesToAfterDDDG, canOutBurstsOverlap)));

			VERBOSE_PRINT(errs() << "[][][][multipath][" << std::to_string(currLoopLevel) << "] Finished\n");
		}

		return;
	}
}

Multipath::Multipath(
	std::string kernelName, ConfigurationManager &CM, ContextManager &CtxM, std::ofstream *summaryFile,
	std::string loopName, unsigned loopLevel, unsigned firstNonPerfectLoopLevel,
	uint64_t loopUnrollFactor, std::vector<unsigned> &unrolls, uint64_t actualLoopUnrollFactor
) :
	kernelName(kernelName), CM(CM), CtxM(CtxM), summaryFile(summaryFile),
	loopName(loopName), loopLevel(loopLevel), firstNonPerfectLoopLevel(firstNonPerfectLoopLevel),
	loopUnrollFactor(loopUnrollFactor), unrolls(unrolls), actualLoopUnrollFactor(actualLoopUnrollFactor),
	enablePipelining(true)
{
	_Multipath();
}

Multipath::Multipath(
	std::string kernelName, ConfigurationManager &CM, ContextManager &CtxM, std::ofstream *summaryFile,
	std::string loopName, unsigned loopLevel, unsigned firstNonPerfectLoopLevel,
	uint64_t loopUnrollFactor, std::vector<unsigned> &unrolls
) :
	kernelName(kernelName), CM(CM), CtxM(CtxM), summaryFile(summaryFile),
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

void Multipath::dumpSummary(uint64_t numCycles) {
	//*summaryFile << "=======================================================================\n";
	//*summaryFile << "Non-perfect loop analysis results\n";

	//*summaryFile << "Total cycles: " << std::to_string(numCycles) << "\n";
	//*summaryFile << "------------------------------------------------\n";
	//*summaryFile << "================================================\n";
	*summaryFile << "================================================\n";
	if(args.fNoTCS)
		*summaryFile << "Time-constrained scheduling disabled\n";
	*summaryFile << "Target clock: " << std::to_string(args.frequency) << " MHz\n";
	*summaryFile << "Clock uncertainty: " << std::to_string(args.uncertainty) << " %\n";
	*summaryFile << "Target clock period: " << std::to_string(1000 / args.frequency) << " ns\n";
	*summaryFile << "Effective clock period: " << std::to_string((1000 / args.frequency) - (10 * args.uncertainty / args.frequency)) << " ns\n";
	*summaryFile << "Achieved clock period: " << P.mergeElements<float>("Achieved period") << " ns\n";
	*summaryFile << "Loop name: " << loopName << "\n";
	*summaryFile << "Loop level: " << std::to_string(firstNonPerfectLoopLevel) << "\n";

	*summaryFile << "DDDG type: non-perfect loop nest (more than 1 DDDG)\n";

	*summaryFile << "Total cycles: " << std::to_string(numCycles) << "\n";
	*summaryFile << "------------------------------------------------\n";

	/* XXX Resource estimation! */

	// Finalise shared resources calculation
	unsigned sharedFU = 0;
	unsigned sharedDSP = 0;
	unsigned sharedFF = 0;
	unsigned sharedLUT = 0;
#ifdef LEGACY_SEPARATOR
	std::string value = P.mergeElements<Pack::resourceNodeTy>("_shared~fadd");
#else
	std::string value = P.mergeElements<Pack::resourceNodeTy>("_shared" GLOBAL_SEPARATOR "fadd");
#endif
	// Unpacking the values from the string. Each variable has 10 characters allocated
	// I know this is ugly, please don't kill me...
	sharedFU += stol(value.substr(0, 10));
	sharedDSP += stol(value.substr(10, 10));
	sharedFF += stol(value.substr(20, 10));
	sharedLUT += stol(value.substr(30, 10));
#ifdef LEGACY_SEPARATOR
	value = P.mergeElements<Pack::resourceNodeTy>("_shared~fsub");
#else
	std::string value = P.mergeElements<Pack::resourceNodeTy>("_shared" GLOBAL_SEPARATOR "fsub");
#endif
	sharedFU += stol(value.substr(0, 10));
	sharedDSP += stol(value.substr(10, 10));
	sharedFF += stol(value.substr(20, 10));
	sharedLUT += stol(value.substr(30, 10));
#ifdef LEGACY_SEPARATOR
	value = P.mergeElements<Pack::resourceNodeTy>("_shared~fmul");
#else
	std::string value = P.mergeElements<Pack::resourceNodeTy>("_shared" GLOBAL_SEPARATOR "fmul");
#endif
	sharedFU += stol(value.substr(0, 10));
	sharedDSP += stol(value.substr(10, 10));
	sharedFF += stol(value.substr(20, 10));
	sharedLUT += stol(value.substr(30, 10));
#ifdef LEGACY_SEPARATOR
	value = P.mergeElements<Pack::resourceNodeTy>("_shared~fdiv");
#else
	std::string value = P.mergeElements<Pack::resourceNodeTy>("_shared" GLOBAL_SEPARATOR "fdiv");
#endif
	sharedFU += stol(value.substr(0, 10));
	sharedDSP += stol(value.substr(10, 10));
	sharedFF += stol(value.substr(20, 10));
	sharedLUT += stol(value.substr(30, 10));

	// Finalise unshared resources calculation
	unsigned unsharedFU = 0;
	unsigned unsharedDSP = 0;
	unsigned unsharedFF = 0;
	unsigned unsharedLUT = 0;
	for(auto &it : P.getStructure()) {
		std::string name = std::get<0>(it);

#ifdef LEGACY_SEPARATOR
		if(!(name.compare(0, 10, "_unshared~"))) {
#else
		if(!(name.compare(0, 10, "_unshared" GLOBAL_SEPARATOR))) {
#endif
			value = P.mergeElements<Pack::resourceNodeTy>(name);
			unsharedFU += stol(value.substr(0, 10));
			unsharedDSP += stol(value.substr(10, 10));
			unsharedFF += stol(value.substr(20, 10));
			unsharedLUT += stol(value.substr(30, 10));
		}
	}

	std::string wholeLoopName = appendDepthToLoopName(loopName, 1);
	wholeloopName2loopBoundMapTy::iterator found = wholeloopName2loopBoundMap.find(wholeLoopName);
	uint64_t loopBound = found->second;
	// Use all bounds
	for(unsigned i = 2; i <= LpName2numLevelMap.at(loopName); i++) {
		wholeLoopName = appendDepthToLoopName(loopName, i);
		found = wholeloopName2loopBoundMap.find(wholeLoopName);
		loopBound *= found->second;
	}

	assert("true" == P.mergeElements<uint64_t>("_memlogicFF") && "Merged values from datapaths differ where it should not differ (_memlogicFF)");
	unsigned mlFF = P.getElements<uint64_t>("_memlogicFF")[0];
	assert("true" == P.mergeElements<uint64_t>("_memlogicLUT") && "Merged values from datapaths differ where it should not differ (_memlogicLUT)");
	unsigned mlLUT = P.getElements<uint64_t>("_memlogicLUT")[0];

	uint64_t nStore = stol(P.mergeElements<uint64_t>("_nStore"));
	uint64_t nLoad = stol(P.mergeElements<uint64_t>("_nLoad"));
	uint64_t nOp = sharedFU + unsharedFU;
	uint64_t tRcIL = stol(P.mergeElements<uint64_t>("_tRcIL"));
	unsigned lK = LpName2numLevelMap.at(loopName);
	unsigned e = logNextPowerOf2(loopBound);
	unsigned V1 = e + 1, V2 = 2 * e, V3 = e + 2;

	for(auto &it : P.getStructure()) {
		std::string name = std::get<0>(it);
		unsigned mergeType = std::get<1>(it);
		unsigned type = std::get<2>(it);

		// Names starting with "_" are not printed (used for other purposes)
		if('_' == name[0])
			continue;

		// Special treament for certain merges (other arithmetics are performed instead of simple merge
		if("DSPs" == name) {
			uint64_t value = sharedDSP + unsharedDSP;

			*summaryFile << name << ": " << value << "\n";
			continue;
		}
		else if("FFs" == name) {
			unsigned rFF = 32 * (nLoad + nStore + nOp) + tRcIL + (1 == lK? 1 : 2) * V1 * lK;
			uint64_t value = sharedFF + unsharedFF + rFF + mlFF;

			*summaryFile << name << ": " << value << "\n";
			continue;
		}
		else if("LUTs" == name) {
			unsigned mLUT = 32 * (nStore + nOp) + 14 * nLoad + V1 * lK;
			// Use all unrolls
			loopName2levelUnrollVecMapTy::iterator found2 = loopName2levelUnrollVecMap.find(loopName);
			assert(found2 != loopName2levelUnrollVecMap.end() && "Could not find loop in loopName2levelUnrollVecMap");
			std::vector<unsigned> targetUnroll = found2->second;
			uint64_t accUnrollFactor = 1;
			for(unsigned i = loopLevel - 1; i + 1; i--)
				accUnrollFactor *= targetUnroll.at(i);
			unsigned exLUT = (V1 + V2 + V3) * lK + V1 * (accUnrollFactor - 1);

			uint64_t value = sharedLUT + unsharedLUT + mLUT + exLUT + mlLUT;

			*summaryFile << name << ": " << value << "\n";
			continue;
		}

		if(Pack::MERGE_EQUAL == mergeType) {
			if(Pack::TYPE_UNSIGNED == type) {
				assert("true" == P.mergeElements<uint64_t>(name) && "Merged values from datapaths differ where it should not differ");
				*summaryFile << name << ": " << std::to_string(P.getElements<uint64_t>(name)[0]) << "\n";
			}
			else if(Pack::TYPE_SIGNED == type) {
				assert("true" == P.mergeElements<int64_t>(name) && "Merged values from datapaths differ where it should not differ");
				*summaryFile << name << ": " << std::to_string(P.getElements<int64_t>(name)[0]) << "\n";
			}
			else if(Pack::TYPE_FLOAT == type) {
				assert("true" == P.mergeElements<float>(name) && "Merged values from datapaths differ where it should not differ");
				*summaryFile << name << ": " << std::to_string(P.getElements<float>(name)[0]) << "\n";
			}
			else if(Pack::TYPE_STRING == type) {
				assert("true" == P.mergeElements<std::string>(name) && "Merged values from datapaths differ where it should not differ");
				*summaryFile << name << ": " << P.getElements<std::string>(name)[0] << "\n";
			}
		}
		else {
			if(Pack::TYPE_UNSIGNED == type) {
				*summaryFile << name << ": " << P.mergeElements<uint64_t>(name) << "\n";
			}
			else if(Pack::TYPE_SIGNED == type) {
				*summaryFile << name << ": " << P.mergeElements<int64_t>(name) << "\n";
			}
			else if(Pack::TYPE_FLOAT == type) {
				*summaryFile << name << ": " << P.mergeElements<float>(name) << "\n";
			}
			else if(Pack::TYPE_STRING == type) {
				std::string mergeResult = P.mergeElements<std::string>(name);
				*summaryFile << name << ": " << (("" == mergeResult)? "none" : mergeResult) << "\n";
			}
		}
	}
}

#ifdef DBG_PRINT_ALL
void Multipath::printDatabase() {
	errs() << "-- latencies\n";
	for(auto const &x : latencies) {
		std::string loopType;

		switch(std::get<1>(x)) {
			case DatapathType::NORMAL_LOOP:
				loopType = "NORMAL_LOOP";
				break;
			case DatapathType::PERFECT_LOOP:
				loopType = "PERFECT_LOOP";
				break;
			case DatapathType::NON_PERFECT_BEFORE:
				loopType = "NON_PERFECT_BEFORE";
				break;
			case DatapathType::NON_PERFECT_BETWEEN:
				loopType = "NON_PERFECT_BETWEEN";
				break;
			case DatapathType::NON_PERFECT_AFTER:
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
