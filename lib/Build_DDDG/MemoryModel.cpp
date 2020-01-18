#include "profile_h/MemoryModel.h"

#include "profile_h/BaseDatapath.h"

extern memoryTraceMapTy memoryTraceMap;
extern bool memoryTraceGenerated;

std::unordered_map<std::string, std::vector<ddrInfoTy>> globalDDRMap;

MemoryModel::MemoryModel(BaseDatapath *datapath) :
	datapath(datapath), microops(datapath->getMicroops()), graph(datapath->getDDDG()),
	nameToVertex(datapath->getNameToVertex()), vertexToName(datapath->getVertexToName()), edgeToWeight(datapath->getEdgeToWeight()),
	baseAddress(datapath->getBaseAddress()), CM(datapath->getConfigurationManager()), PC(datapath->getParsedTraceContainer()) {
	wholeLoopName = appendDepthToLoopName(datapath->getTargetLoopName(), datapath->getTargetLoopLevel());
	ddrBanking = CM.getGlobalCfg<bool>(ConfigurationManager::globalCfgTy::GLOBAL_DDRBANKING);
	importedFromContext = false;
}

MemoryModel *MemoryModel::createInstance(BaseDatapath *datapath) {
	switch(args.target) {
		case ArgPack::TARGET_XILINX_VC707:
			assert(args.fNoMMA && "Memory model analysis is currently not supported with the selected platform. Please activate the \"--fno-mma\" flag");
			return nullptr;
		case ArgPack::TARGET_XILINX_ZCU102:
		case ArgPack::TARGET_XILINX_ZCU104:
			return new XilinxZCUMemoryModel(datapath);
		case ArgPack::TARGET_XILINX_ZC702:
		default:
			assert(args.fNoMMA && "Memory model analysis is currently not supported with the selected platform. Please activate the \"--fno-mma\" flag");
			return nullptr;
	}
}

bool MemoryModel::canOutBurstsOverlap(std::vector<MemoryModel::nodeExportTy> toBefore, std::vector<MemoryModel::nodeExportTy> toAfter) {
	// We follow here the policies as documented in tryAllocate()
	for(auto &after : toAfter) {
		for(auto &before : toBefore) {
			// If DDR banking is active, only consider cases with the same arrayName
			if(after.arrayName != before.arrayName)
				continue;

			// With current out-burst logic, here is simplified considering that:
			// - "after" will always be bound to write transactions (LLVM_IR_DDRWriteResp)
			// - "before" can be either read or write (LLVM_IR_DDRReadReq or LLVM_IR_DDRWriteReq)

			uint64_t afterBase = after.baseAddress;
			uint64_t beforeBase = before.baseAddress;
#ifdef VAR_WSIZE
			uint64_t afterEnd = afterBase + after.offset * after.wordSize;
			uint64_t beforeEnd = beforeBase + before.offset * before.wordSize;
#else
			// XXX: As always, assuming 32-bit
			uint64_t afterEnd = afterBase + after.offset * 4;
			uint64_t beforeEnd = beforeBase + before.offset * 4;
#endif

			// Asserting just for security
			assert(LLVM_IR_DDRWriteResp == getNonSilentOpcode(after.node.opcode) && "Out-bursted node to \"after\" DDDG is not LLVM_IR_DDRWriteResp, this is likely a bug");

			if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
				switch(getNonSilentOpcode(before.node.opcode)) {
					// If "before" is a write, we cannot overlap
					case LLVM_IR_DDRWriteReq:
						return false;
					// If "before" is a read, we can overlap if the regions do not
					case LLVM_IR_DDRReadReq:
						if(beforeEnd >= afterBase && afterEnd >= beforeBase)
							return false;
						break;
					default:
						assert(false && "Out-bursted node to \"before\" DDDG is not of an expected type, this is likely a bug");
				}
			}
			else if(ArgPack::DDR_POLICY_CAN_OVERLAP == args.ddrSched) {
				// Any region can co-exist if their regions do not overlap
				if(beforeEnd >= afterBase && afterEnd >= beforeBase)
					return false;
			}
			else {
				assert(false && "Invalid DDR scheduling policy selected (this assert should never execute)");
			}
		}
	}

	return true;
 
#if 0
	// XXX: This overlap analysis on out-bursts does not actually consider the whole space traversed through the loop
	// Maybeb this information should be included in the exported node (we have this info when running findOutBursts!)
	// It might be needed also to add this info to the context
	//assert(false && "Re-check this function!");
	// Check if the scheduled outbursts overlap
	// Yes, this is a O(n^2) loop, but it will (hopefully) rarely run in total:
	// - When banking is active, only cases where v1[i].arrayName == v2[j].arrayName will execute
	// - Else, there should be at most 2 elements in v1 and v2
	for(auto &it1 : v1) {
		for(auto &it2 : v2) {
			// If DDR banking is active, only consider cases with the same arrayName
			if(it1.arrayName != it2.arrayName)
				continue;

			uint64_t base1 = it1.baseAddress;
#ifdef VAR_WSIZE
			uint64_t end1 = base1 + it1.offset * it1.wordSize;
#else
			// XXX: As always, assuming 32-bit
			uint64_t end1 = base1 + it1.offset * 4;
#endif
			uint64_t base2 = it2.baseAddress;
#ifdef VAR_WSIZE
			uint64_t end2 = base2 + it2.offset * it2.wordSize;
#else
			// XXX: As always, assuming 32-bit
			uint64_t end2 = base2 + it2.offset * 4;
#endif

			if(end1 >= base2 && end2 >= base1)
				return true;
		}
	}

	return false;
#endif
}

void MemoryModel::analyseAndTransform() { }

void XilinxZCUMemoryModel::findInBursts(
		std::unordered_map<unsigned, std::pair<std::string, uint64_t>> &foundNodes,
		std::vector<unsigned> &behavedNodes,
		std::unordered_map<unsigned, burstInfoTy> &burstedNodes,
		std::function<bool(unsigned, unsigned)> comparator
) {
	// Ignore if behavedNodes is empty
	if(0 == behavedNodes.size())
		return;

	// Lina supports burst mix, which means that it can form bursts when there are mixed-array contiguous transactions
	// With DDR banking, burst mix is always disabled
	bool shouldMix = false;
	if(args.fBurstMix && !ddrBanking)
		shouldMix = true;

	// If burst mix is enabled, we just add everyone to a single element
	std::unordered_map<std::string, std::vector<unsigned>> behavedNodesBuckets;
	if(shouldMix) {
		behavedNodesBuckets[""] = (behavedNodes);
	}
	// Else, we must separate behavedNodes by their arrays
	else {
		for(auto &behavedNode : behavedNodes)
			behavedNodesBuckets[foundNodes[behavedNode].first].push_back(behavedNode);
	}

	for(auto &it : behavedNodesBuckets) {
		std::vector<unsigned> &behavedNodesFiltered = it.second;

		// Sort transactions, so that we can find continuities
		std::sort(behavedNodesFiltered.begin(), behavedNodesFiltered.end(), comparator);

		// Kickstart: logic for the first node
		unsigned currRootNode = behavedNodesFiltered[0];
		std::string currArrayName = shouldMix? "" : foundNodes[currRootNode].first;
		uint64_t currBaseAddress = foundNodes[currRootNode].second;
		uint64_t currOffset = 0;
		std::vector<unsigned> currNodes({currRootNode});
		uint64_t lastVisitedBaseAddress = currBaseAddress;

		// XXX: Even though MemoryModel support values larger than 32-bit, it assumes that all variables in code are 32-bit, as the rest of Lina

		// Now iterate for all the remaining nodes
		for(unsigned i = 1; i < behavedNodesFiltered.size(); i++) {
			unsigned node = behavedNodesFiltered[i];

			// If the current address is the same as the last, this is a redundant node that could be optimised
			// We consider part of a burst
			if(lastVisitedBaseAddress == foundNodes[node].second) {
				currNodes.push_back(node);
			}
			// Else, check if current address is contiguous to the last one
			else if(lastVisitedBaseAddress + 4 == foundNodes[node].second) {
				currNodes.push_back(node);
				currOffset++;
			}
			// Discontinuity found. Close current burst and reset variables
			else {
#ifdef VAR_WSIZE
				burstedNodes[currRootNode] = burstInfoTy(currBaseAddress, currOffset, 4, currNodes);
#else
				burstedNodes[currRootNode] = burstInfoTy(currBaseAddress, currOffset, currNodes);
#endif

				// Reset variables
				currRootNode = node;
				if(!shouldMix)
					currArrayName = foundNodes[node].first;
				currBaseAddress = foundNodes[node].second;
				currOffset = 0;
				currNodes.clear();
				currNodes.push_back(node);
			}

			lastVisitedBaseAddress = foundNodes[node].second;
		}

		// Close the last burst
#ifdef VAR_WSIZE
		burstedNodes[currRootNode] = burstInfoTy(currBaseAddress, currOffset, 4, currNodes);
#else
		burstedNodes[currRootNode] = burstInfoTy(currBaseAddress, currOffset, currNodes);
#endif
	}
}

bool XilinxZCUMemoryModel::analyseLoadOutBurstFeasability(unsigned burstID, std::string arrayName) {
	if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
		if(ddrBanking) {
			// No reads for the same array
			for(auto &it : burstedLoads) {
				// Ignore if this is the burst under analysis
				if(burstID != it.first) {
					std::string otherArrayName = loadNodes.at(it.first).first;
					if(otherArrayName == arrayName)
						return false;
				}
			}
		}
		else {
			// No reads from other bursts
			if(burstedLoads.size() > 1)
				return false;
		}

		// No writes for the same array
		for(auto &it : burstedStores) {
			std::string otherArrayName = storeNodes.at(it.first).first;
			if(otherArrayName == arrayName)
				return false;
		}
	}
	else if(ArgPack::DDR_POLICY_CAN_OVERLAP == args.ddrSched) {
		// No reads for the same array
		for(auto &it : burstedLoads) {
			// Ignore if this is the burst under analysis
			if(burstID != it.first) {
				std::string otherArrayName = loadNodes.at(it.first).first;
				if(otherArrayName == arrayName)
					return false;
			}
		}

		// No writes for the same array
		for(auto &it : burstedStores) {
			std::string otherArrayName = storeNodes.at(it.first).first;
			if(otherArrayName == arrayName)
				return false;
		}
	}
	else {
		assert(false && "Invalid DDR scheduling policy selected (this assert should never execute)");
	}

	return true;
}

bool XilinxZCUMemoryModel::analyseStoreOutBurstFeasability(unsigned burstID, std::string arrayName) {
	if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
		if(ddrBanking) {
			// No writes for the same array
			for(auto &it : burstedStores) {
				// Ignore if this is the burst under analysis
				if(burstID != it.first) {
					std::string otherArrayName = storeNodes.at(it.first).first;
					if(otherArrayName == arrayName)
						return false;
				}
			}
		}
		else {
			// No writes from other bursts
			if(burstedStores.size() > 1)
				return false;
		}

		// No reads for the same array
		for(auto &it : burstedLoads) {
			std::string otherArrayName = loadNodes.at(it.first).first;
			if(otherArrayName == arrayName)
				return false;
		}
	}
	else if(ArgPack::DDR_POLICY_CAN_OVERLAP == args.ddrSched) {
		// No writes for the same array
		for(auto &it : burstedStores) {
			// Ignore if this is the burst under analysis
			if(burstID != it.first) {
				std::string otherArrayName = storeNodes.at(it.first).first;
				if(otherArrayName == arrayName)
					return false;
			}
		}

		// No reads for the same array
		for(auto &it : burstedLoads) {
			std::string otherArrayName = loadNodes.at(it.first).first;
			if(otherArrayName == arrayName)
				return false;
		}
	}
	else {
		assert(false && "Invalid DDR scheduling policy selected (this assert should never execute)");
	}

	return true;
}

std::unordered_map<std::string, outBurstInfoTy> XilinxZCUMemoryModel::findOutBursts(
	std::unordered_map<unsigned, burstInfoTy> &burstedNodes,
	std::unordered_map<unsigned, std::pair<std::string, uint64_t>> &foundNodes,
	const std::vector<std::string> &instIDList,
	bool (XilinxZCUMemoryModel::*analyseOutBurstFeasability)(unsigned, std::string)
) {
	std::unordered_map<std::string, outBurstInfoTy> canOutBurst;

	// This is a local search. It will decide whether out-burst or not solely
	// on information about this DDDG. This does not mean that the out-burst is
	// actually possible if all DDDGS all considered. This deeper analysis only
	// happens with the gen-use execution model, where blockInvalidOutBursts()
	// is used.

	// Local search policies:
	// Conservative:
	// - Writes can only be out-bursted if:
	//   - There is no read for the same array in this DDDG;
	//   - There is no writes at all for the same memory space;
	// - Reads can only be out-bursted if:
	//   - There is no write for the same array in this DDDG;
	//   - There is no reads at all for the same memory space;
	// Overlapping:
	// - Writes or reads can only be out-bursted if:
	//   - There is no transactions for the same array in this DDDG;
	// This logic is implemented at:
	// - analyseLoadOutBurstFeasability()
	// - analyseStoreOutBurstFeasability()

	// Offset defines the burst size, excluding the first element. Unrolling a loop N times will make the offset grow N times,
	// considering that the code execution will not vary through each iteration. memoryTraceMap has no knowledge about the
	// unrolling performed by Lina, therefore the offset must be adjusted to properly seek through memoryTraceMap
	// same instruction at the map. 
	// NOTE: This is only applicable to DDDGs where unroll actually causes node replication (i.e. the innermost).
	//       It is not the case for non-perfect DDDGs.
	unsigned adjustFactor = (BaseDatapath::NORMAL_LOOP == datapath->getDatapathType())?
		loopName2levelUnrollVecMap.at(datapath->getTargetLoopName())[datapath->getTargetLoopLevel() - 1] : 1;

	for(auto &it : burstedNodes) {
		std::string arrayName = foundNodes.at(it.first).first;
		burstInfoTy &burstedNode = it.second;

		// If canOutBurst already have the element for this array name,
		// it means that there is more than one transaction of same type
		// for the same array, already breaking the "no X for the same array"
		// rule. We can stop here and already inform that this array is not
		// out-burstable.
		std::unordered_map<std::string, outBurstInfoTy>::iterator found = canOutBurst.find(arrayName);
		if(found != canOutBurst.end()) {
			// Even if it was out-burstable, it is not anymore
			found->second.canOutBurst = false;
			continue;
		}

		// We are optimistic at first
		canOutBurst[arrayName] = outBurstInfoTy(true);

		// If local analysis of out-burst fails, no need to continue also
		if((this->*analyseOutBurstFeasability)(it.first, arrayName)) {
			// In this case, all transactions between iterations must be contiguous with no overlap
			// This means that all entangled nodes (i.e. DDDG nodes that represent the same instruction but
			// different instances) must continuously increase their address by the burst size (offset)
			// If any fails, we assume that inter-iteration bursting is not possible.
			uint64_t offset = burstedNode.offset + 1;
#ifdef VAR_WSIZE
			uint64_t wordSize = burstedNode.wordSize;
#endif
			std::vector<unsigned> burstedNodesVec = burstedNode.participants;

			for(auto &it2 : burstedNodesVec) {
				std::pair<std::string, std::string> wholeLoopNameInstNamePair = std::make_pair(wholeLoopName, instIDList[it2]);

				// Check if all instances of this instruction are well behaved in the memory trace
				// XXX: We do not sort the list here like when searching for inner bursts, since we do not support loop reordering (for now)
				std::vector<uint64_t> addresses = memoryTraceMap.at(wholeLoopNameInstNamePair);
#ifdef VAR_WSIZE
				uint64_t nextAddress = addresses[0] + wordSize * (offset / adjustFactor);
#else
				// XXX: As always, assuming 32-bit
				uint64_t nextAddress = addresses[0] + 4 * (offset / adjustFactor);
#endif
				for(unsigned i = 1; i < addresses.size(); i++) {
					if(nextAddress != addresses[i]) {
						canOutBurst[arrayName].canOutBurst = false;
						break;
					}
					else {
#ifdef VAR_WSIZE
						nextAddress += wordSize * (offset / adjustFactor);
#else
						// XXX: As always, assuming 32-bit
						nextAddress += 4 * (offset / adjustFactor);
#endif
					}
				}

				// If out-burst is apparently possible, we save the expanded memory region (considering the entangled nodes)
				// to be exported later with the nodes
				if(canOutBurst.at(arrayName).canOutBurst) {
					outBurstInfoTy &info = canOutBurst.at(arrayName);

					// If the out-burst region information was already registered, we check if the region is the same
					if(info.isRegistered) {
						// If not, this is also an irregular out-burst
#ifdef VAR_WSIZE
						if((burstedNode.baseAddress != info.baseAddress) || ((addresses.size() - 1) != info.offset) || (wordSize != info.wordSize)) {
#else
						if((burstedNode.baseAddress != info.baseAddress) || ((addresses.size() - 1) != info.offset)) {
#endif
							canOutBurst[arrayName].canOutBurst = false;
							break;
						}
					}
					else {
						info.baseAddress = burstedNode.baseAddress;
						info.offset = addresses.size() - 1;
#ifdef VAR_WIZE
						info.wordSize = wordSize;
#endif
						info.isRegistered = true;
					}
				}
				else {
					break;
				}
			}
		}
		else {
			canOutBurst[arrayName].canOutBurst = false;
		}
	}

#if 0
	// Our current assumption for inter-iteration bursting is very restrictive. It should happen only when
	// there is one (and one only) load/store transaction (which may be bursted) inside a loop level for
	// the same array. If banking is not active, it means only one transaction at all, regardless of array.
	// We can already cancel if this is the case.
	if(!ddrBanking && burstedNodes.size() > 1)
		return canOutBurst;

	for(auto &it : burstedNodes) {
#if 0
		std::string arrayName = ddrBanking? foundNodes.at(it.first).first : "";
#else
		std::string arrayName = foundNodes.at(it.first).first;
#endif
		burstInfoTy &burstedNode = it.second;

		// If out-burst was already checked for this array (or "" if no banking)
		// this means more than one transaction per array name, which is not allowed in our
		// current out-burst logic
		std::unordered_map<std::string, bool>::iterator found = canOutBurst.find(arrayName);
		if(found != canOutBurst.end()) {
			found->second = false;
			continue;
		}

		// We are optimistic at first
		canOutBurst[arrayName] = true;

		// In this case, all transactions between iterations must be contiguous with no overlap
		// This means that all entangled nodes (i.e. DDDG nodes that represent the same instruction but
		// different instances) must continuously increase their address by the burst size (offset)
		// If any fails, we assume that inter-iteration bursting is not possible.
		uint64_t offset = burstedNode.offset + 1;
#ifdef VAR_WSIZE
		uint64_t wordSize = burstedNode.wordSize;
#endif
		std::vector<unsigned> burstedNodesVec = burstedNode.participants;

		// If this loop level was unrolled, the memoryTraceMap will contain entangled nodes that
		// should not be considered (as successive loop iterations were "merged" by unroll).
		// Therefore we adjust the loop increment accordingly
		unsigned loopIncrement = loopName2levelUnrollVecMap.at(datapath->getTargetLoopName())[datapath->getTargetLoopLevel() - 1];

		for(auto &it2 : burstedNodesVec) {
			std::pair<std::string, std::string> wholeLoopNameInstNamePair = std::make_pair(wholeLoopName, instIDList[it2]);

			// Check if all instances of this instruction are well behaved in the memory trace
			// XXX: We do not sort the list here like when searching for inner bursts, since we do not support loop reordering (for now)
			std::vector<uint64_t> addresses = memoryTraceMap.at(wholeLoopNameInstNamePair);
#ifdef VAR_WSIZE
			uint64_t nextAddress = addresses[0] + wordSize * offset;
#else
			// XXX: As always, assuming 32-bit
			uint64_t nextAddress = addresses[0] + 4 * offset;
#endif
			for(unsigned i = loopIncrement; i < addresses.size(); i += loopIncrement) {
				if(nextAddress != addresses[i]) {
					canOutBurst[arrayName] = false;
					break;
				}
				else {
#ifdef VAR_WSIZE
					nextAddress += wordSize * offset;
#else
					// XXX: As always, assuming 32-bit
					nextAddress += 4 * offset;
#endif
				}
			}

			if(!(canOutBurst[arrayName]))
				break;
		}
	}
#endif

	return canOutBurst;
}

void XilinxZCUMemoryModel::packBursts(
	std::unordered_map<unsigned, burstInfoTy> &burstedNodes,
	std::unordered_map<unsigned, std::pair<std::string, uint64_t>> &nodes,
#ifdef CROSS_DDDG_PACKING
	std::unordered_map<std::string, remainingBudgetTy> &remainingBudget,
#endif
	int silentOpcode, bool nonSilentLast
) {
	for(auto &burst : burstedNodes) {
		// If nonSilentLast is true, this will pack as SOp>SOp>SOp>Op>...
		// otherwise: Op>SOp>SOp>SOp>...0
		int busBudget = nonSilentLast? DDR_DATA_BUS_WIDTH - 4 : 0;
		uint64_t lastVisitedBaseAddress;
		std::vector<unsigned> participants = burst.second.participants;
		unsigned participantsSz = participants.size();

		// Iterate through the bursted nodes, pack until the bus budget is zero'd
		unsigned participant = -1;
		for(unsigned int i = 0; i < participantsSz; i++) {
			participant = participants[i];

			// Remember that bursted nodes may include repeated nodes (that are treated as redundant transactions)
			// if we face one of those, we don't change the budget, but we silence the node
			if(i && lastVisitedBaseAddress == nodes.at(participant).second) {
				microops.at(participant) = silentOpcode;
			}
			else {
				busBudget -= 4;

				// If bus budget is over, we start a new read transaction and reset
				if(busBudget < 0)
					busBudget = DDR_DATA_BUS_WIDTH - 4;
				// Else, we "silence" this node
				else
					microops.at(participant) = silentOpcode;
			}

			lastVisitedBaseAddress = nodes.at(participant).second;
		}

		// If last should be non-silent, we make sure of that
		if(nonSilentLast) {
			microops.at(participant) = getNonSilentOpcode(silentOpcode);
		}

#ifdef CROSS_DDDG_PACKING
		// Save the remaining budget, it can be used by other DDDGs in the same loop level (conditions apply)
		std::string arrayName = nodes.at(burst.first).first;
		std::unordered_map<std::string, remainingBudgetTy>::iterator found = remainingBudget.find(arrayName);
		// If there is already an element for this array, we cannot reuse this budget later as there may be overlapping
		// concurrency. In this case we invalidate the remaining budget at all
		remainingBudget[arrayName] = (remainingBudget.end() == found)?
			remainingBudgetTy(nonSilentLast? (busBudget + 4) % DDR_DATA_BUS_WIDTH : busBudget, burst.second.baseAddress, burst.second.offset) :
			remainingBudgetTy();
#endif
	}
}

bool XilinxZCUMemoryModel::analyseLoadOutBurstFeasabilityGlobal(std::string arrayName, unsigned loopLevel, unsigned datapathType) {
	if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
		for(auto &it : filteredDDRMap) {
			// Ignore elements that are from upper loops
			if(it.loopLevel < loopLevel)
				continue;
			// If it refers to this DDDG itself, also ignore
			if(it.loopLevel == loopLevel && datapathType == it.datapathType)
				continue;
			// Also ignore BETWEEN DDDGs
			if(BaseDatapath::NON_PERFECT_BETWEEN == it.datapathType)
				continue;

			if(ddrBanking) {
				// No reads for the same array
				if(it.arraysLoaded.count(arrayName))
					return false;
			}
			else {
				// No reads at all
				if(it.arraysLoaded.size())
					return false;
			}

			// No writes for the same array
			if(it.arraysStored.count(arrayName))
				return false;
		}
	}
	else if(ArgPack::DDR_POLICY_CAN_OVERLAP == args.ddrSched) {
		for(auto &it : filteredDDRMap) {
			// Ignore elements that are from upper loops
			if(it.loopLevel < loopLevel)
				continue;
			// If it refers to this DDDG itself, also ignore
			if(it.loopLevel == loopLevel && datapathType == it.datapathType)
				continue;
			// Also ignore BETWEEN DDDGs
			if(BaseDatapath::NON_PERFECT_BETWEEN == it.datapathType)
				continue;

			// No reads for the same array
			if(it.arraysLoaded.count(arrayName))
				return false;

			// No writes for the same array
			if(it.arraysStored.count(arrayName))
				return false;
		}
	}
	else {
		assert(false && "Invalid DDR scheduling policy selected (this assert should never execute)");
	}

	return true;
}

bool XilinxZCUMemoryModel::analyseStoreOutBurstFeasabilityGlobal(std::string arrayName, unsigned loopLevel, unsigned datapathType) {
	if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
		for(auto &it : filteredDDRMap) {
			// Ignore elements that are from upper loops
			if(it.loopLevel < loopLevel)
				continue;
			// If it refers to this DDDG itself, also ignore
			if(it.loopLevel == loopLevel && datapathType == it.datapathType)
				continue;
			// Also ignore BETWEEN DDDGs
			if(BaseDatapath::NON_PERFECT_BETWEEN == it.datapathType)
				continue;

			if(ddrBanking) {
				// No writes for the same array
				if(it.arraysStored.count(arrayName))
					return false;
			}
			else {
				// No writes at all
				if(it.arraysStored.size())
					return false;
			}

			// No reads for the same array
			if(it.arraysLoaded.count(arrayName))
				return false;
		}
	}
	else if(ArgPack::DDR_POLICY_CAN_OVERLAP == args.ddrSched) {
		for(auto &it : filteredDDRMap) {
			// Ignore elements that are from upper loops
			if(it.loopLevel < loopLevel)
				continue;
			// If it refers to this DDDG itself, also ignore
			if(it.loopLevel == loopLevel && datapathType == it.datapathType)
				continue;
			// Also ignore BETWEEN DDDGs
			if(BaseDatapath::NON_PERFECT_BETWEEN == it.datapathType)
				continue;

			// No writes for the same array
			if(it.arraysStored.count(arrayName))
				return false;

			// No reads for the same array
			if(it.arraysLoaded.count(arrayName))
				return false;
		}
	}
	else {
		assert(false && "Invalid DDR scheduling policy selected (this assert should never execute)");
	}

	return true;
}

void XilinxZCUMemoryModel::blockInvalidOutBursts(
	std::unordered_map<std::string, outBurstInfoTy> &outBurstsFound, unsigned loopLevel, unsigned datapathType,
	bool (XilinxZCUMemoryModel::*analyseOutBurstFeasabilityGlobal)(std::string, unsigned, unsigned)
) {
	// The same logic performed locally by findOutBursts(), will be performed globally here
	for(auto &it : outBurstsFound) {
		// If it is already false, no need to analyse then
		if(it.second.canOutBurst) {
			std::string arrayName = it.first;

			// If with global information we assume that is not feasible, too bad, cancel this out-burst
			if(!((this->*analyseOutBurstFeasabilityGlobal)(it.first, loopLevel, datapathType)))
				it.second.canOutBurst = false;
		}
	}
#if 0
	for(auto &it : outBurstsFound) {
		// If it is already false, no need to analyse then
		if(it.second) {
			std::string arrayName = it.first;

			// Block nodes that would be exported to a previous outer-loop if they overlap inner transactions
			for(auto &it2 : filteredDDRMap) {
				// Ignore elements that are from upper loops
				if(it2.loopLevel < loopLevel)
					continue;
				// If it refers to this DDDG itself, also ignore
				else if(it2.loopLevel == loopLevel && datapathType == it2.datapathType)
					continue;

				// If banking is enabled, disable out-burst if there are transactions for the same array
				// Else, disable if there is any transaction
#if 0
				if(ddrBanking) {
#endif
					if(it2.arrays.count(arrayName)) {
						it.second = false;
						break;
					}
#if 0
				}
				else {
					if(it2.arrays.size()) {
						it.second = false;
						break;
					}
				}
#endif
			}
		}
	}
#endif
}

XilinxZCUMemoryModel::XilinxZCUMemoryModel(BaseDatapath *datapath) : MemoryModel(datapath) {
	importedReadReqs.clear();
	importedWriteReqs.clear();
	importedWriteResps.clear();
}

void XilinxZCUMemoryModel::analyseAndTransform() {
	std::unordered_map<std::string, outBurstInfoTy> loadOutBurstsFound;
	std::unordered_map<std::string, outBurstInfoTy> storeOutBurstsFound;

	if(!(args.fNoMMABurst)) {
		// The memory trace map can be generated in two ways:
		// - Running Lina with "--mem-trace" and any other mode than "--mode=estimation"
		// - After running Lina once with the aforementioned configuration, the file "mem_trace.txt" will be available and can be used

		// If memory trace map was not constructed yet, try to generate it from "mem_trace.txt"
		if(!memoryTraceGenerated) {
			std::string line;
			std::string traceFileName = args.workDir + FILE_MEM_TRACE;
			std::ifstream traceFile;

			traceFile.open(traceFileName);
			assert(traceFile.is_open() && "No memory trace found. Please run Lina with \"--mem-trace\" flag (leave it enabled) and any mode other than \"--mode=estimation\" (only once is needed) to generate it; or deactivate inter-iteration burst analysis with \"--fno-mmaburst\"");

			while(true) {
				std::getline(traceFile, line);
				if(traceFile.eof())
					break;

				char buffer[BUFF_STR_SZ];
				char buffer2[BUFF_STR_SZ];
				uint64_t address;
				sscanf(line.c_str(), "%[^,],%*d,%[^,],%*[^,],%*d,%lu,%*d", buffer, buffer2, &address);
				std::pair<std::string, std::string> wholeLoopNameInstNamePair = std::make_pair(std::string(buffer), std::string(buffer2));
				memoryTraceMap[wholeLoopNameInstNamePair].push_back(address);
			}

			traceFile.close();
			memoryTraceGenerated = true;
		}
	}

	const ConfigurationManager::arrayInfoCfgMapTy arrayInfoCfgMap = CM.getArrayInfoCfgMap();
	const std::unordered_map<int, std::pair<int64_t, unsigned>> &memoryTraceList = PC.getMemoryTraceList();

	// Change all load/stores marked as offchip to DDR read/writes (and mark their locations)
	VertexIterator vi, viEnd;
	for(std::tie(vi, viEnd) = vertices(graph); vi != viEnd; vi++) {
		Vertex currNode = *vi;
		unsigned nodeID = vertexToName[currNode];
		int nodeMicroop = microops.at(nodeID);

		if(!isMemoryOp(nodeMicroop))
			continue;

		// Only consider offchip arrays
		if(arrayInfoCfgMap.at(baseAddress[nodeID].first).type != ConfigurationManager::arrayInfoCfgTy::ARRAY_TYPE_OFFCHIP)
			continue;

		if(isLoadOp(nodeMicroop)) {
			microops.at(nodeID) = LLVM_IR_DDRRead;
			loadNodes[nodeID] = std::make_pair(baseAddress.at(nodeID).first, memoryTraceList.at(nodeID).first);
			//loadNodes[nodeID] = baseAddress.at(nodeID);
		}

		if(isStoreOp(nodeMicroop)) {
			microops.at(nodeID) = LLVM_IR_DDRWrite;
			storeNodes[nodeID] = std::make_pair(baseAddress.at(nodeID).first, memoryTraceList.at(nodeID).first);
			//storeNodes[nodeID] = baseAddress.at(nodeID);
		}
	}

	// Try to find contiguous loads inside the DDDG.
	std::vector<unsigned> behavedLoads;
	for(auto const &loadNode : loadNodes) {
		// For simplicity, we only consider loads that are either dominated by getelementptr or nothing
		// XXX (but are there other cases?)
		bool behaved = true;
		InEdgeIterator inEdgei, inEdgeEnd;
		for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(nameToVertex[loadNode.first], graph); inEdgei != inEdgeEnd; inEdgei++) {
			unsigned parentID = vertexToName[boost::source(*inEdgei, graph)];
			if(microops.at(parentID) != LLVM_IR_GetElementPtr)
				behaved = false;
		}

		if(behaved)
			behavedLoads.push_back(loadNode.first);
	}
	// Sort the behaved nodes in terms of address
	auto loadComparator = [this](unsigned a, unsigned b) {
		return this->loadNodes[a] < this->loadNodes[b];
	};
	findInBursts(loadNodes, behavedLoads, burstedLoads, loadComparator);

	// Try to find contiguous stores inside the DDDG.
	std::vector<unsigned> behavedStores;
	for(auto const &storeNode : storeNodes) {
		// For simplicity, we only consider stores that are terminal
		// XXX (but are there other cases?)
		if(0 == boost::out_degree(nameToVertex[storeNode.first], graph))
			behavedStores.push_back(storeNode.first);
	}
	// Sort the behaved nodes in terms of address
	auto storeComparator = [this](unsigned a, unsigned b) {
		return this->storeNodes[a] < this->storeNodes[b];
	};
	findInBursts(storeNodes, behavedStores, burstedStores, storeComparator);

	// TODO: Here would be the perfect place to implement the warning about redundant load/store
	// Recalling: If in an after, a load is performed that in a BEFORE DDDG it could be well performed
	// by using the remaining budget from a load, this is a redudant load that could be supressed,
	// if there are no writes for the same array in the inner loops that are between BEFORE and AFTER.
	// Something similar applies to the AFTER DDDG regarding to stores: if an AFTER has remaining budget
	// for a store and there are stores in BEFORE DDDG that could use this remaining budget, it is also
	// a redundant store, if there are no writes or reads for the same array in the inner loops.
	// The BETWEEN DDDG also comes into act here somehow
#ifdef CROSS_DDDG_PACKING
	if(!(args.fNoBurstPack) && ArgPack::MMA_MODE_USE == args.mmaMode) {
		assert(importedFromContext && "\"--mma-mode\" is set to USE and no context supplied");

		tryAllocateRemainingBudget(microops, loadNodes, behavedLoads, burstedLoads, loadRemainingBudget);
	}
#endif

	// TODO: why && burstedStores.size()????
	if(!(args.fNoMMABurst) && burstedStores.size()) {
		// XXX: Out-burst analysis assumes that no arrays overlap in memory space!
		// This is because for our analysis like it is explained at the beginning of tryAllocate(), we need the
		// region that is read/written to analyse overlap. Within a single DDDG, this information is available
		// for out-bursts, we would have to know the entire region considering the whole loop execution. This information
		// we don't have now and it is not calculated.
		// - For local out-burst analysis, we can still take into account this information as it is available
		// - For global out-burst analysis, we would need to calculate the region read not only by a single DDDG, but all
		//   the iteration instances of it
		// For now we are considering the worst-case scenario:
		// - If arrays overlap, the worst-case scenario would be the whole memory (would block 99.9% of out-bursts)
		// - If arrays don't overlap, the worst-case scenario is the total array size (as it is guaranteed that arrays will never overlap)

		// XXX: For now out-burst analysis does not work with burst mix active
		assert((!args.fBurstMix || ddrBanking) && "Currently burst mix is not supported when iteration burst analysis is active");

		const std::vector<std::string> &instIDList = PC.getInstIDList();

		// TODO: Perhaps if loadOutBurstFound and storeOutBurstFound are true, we should check if
		// the spaces do not overlap before outbursting both

		// If mode is not USE, we run findOutBursts() normally
		if(args.mmaMode != ArgPack::MMA_MODE_USE) {
			// Try to find contiguous loads between loop iterations
			loadOutBurstsFoundCached = findOutBursts(burstedLoads, loadNodes, instIDList, &XilinxZCUMemoryModel::analyseLoadOutBurstFeasability);
			// Try to find contiguous stores between loop iterations
			storeOutBurstsFoundCached = findOutBursts(burstedStores, storeNodes, instIDList, &XilinxZCUMemoryModel::analyseStoreOutBurstFeasability);
		}

		// If MMA mode is set to GEN, we do not out-burst, since the context-import file is actually
		// used to better analyse out-bursts, at least for now.
		if(args.mmaMode != ArgPack::MMA_MODE_GEN) {
			loadOutBurstsFound = loadOutBurstsFoundCached;
			storeOutBurstsFound = storeOutBurstsFoundCached;
		}

		// If MMA mode is set to USE, we use the context-imported cache information to improve out-bursts
		if(ArgPack::MMA_MODE_USE == args.mmaMode) {
			unsigned loopLevel = datapath->getTargetLoopLevel();
			unsigned datapathType = datapath->getDatapathType();

			// In this case we will use information from previous execution to make a better prediction on out-bursts
			assert(importedFromContext && "\"--mma-mode\" is set to USE and no context supplied");
			assert(filteredDDRMap.size() && "\"--mma-mode\" is set to USE and global DDR map is not populated");

			// Perform global out-burst analysis using context-imported information
			if(BaseDatapath::NON_PERFECT_BEFORE == datapathType || BaseDatapath::NON_PERFECT_AFTER == datapathType) {
				blockInvalidOutBursts(loadOutBurstsFound, loopLevel, datapathType, &XilinxZCUMemoryModel::analyseLoadOutBurstFeasabilityGlobal);
				blockInvalidOutBursts(storeOutBurstsFound, loopLevel, datapathType, &XilinxZCUMemoryModel::analyseStoreOutBurstFeasabilityGlobal);
			}
		}
	}

	// If DDR burst packing is enabled, we analyse the burst transactions and merge contiguous
	// transactions together until the whole data bus is consumed
	// For example on a 128-bit wide memory bus, 4 32-bit words can be transferred at the same time
	// XXX: Please note that, as always, we are considering 32-bit words here coming from the code
	if(!(args.fNoBurstPack)) {
		packBursts(burstedLoads, loadNodes, LLVM_IR_DDRSilentRead);
		packBursts(burstedStores, storeNodes, LLVM_IR_DDRSilentWrite, true);

		// XXX: If we start removing edges when packing, should we request DDDG update here?
	}

	// Add the relevant DDDG nodes for offchip load
	for(auto &burst : burstedLoads) {
#if 0
		std::string arrayName = ddrBanking? loadNodes.at(burst.first).first : "";
#else
		std::string arrayName = loadNodes.at(burst.first).first;
#endif

		std::unordered_map<std::string, outBurstInfoTy>::iterator found = loadOutBurstsFound.find(arrayName);
		bool canOutBurst = (found != loadOutBurstsFound.end())? found->second.canOutBurst : false;

		// Create a node with opcode LLVM_IR_DDRReadReq
		artificialNodeTy newNode = datapath->createArtificialNode(burst.first, canOutBurst? LLVM_IR_DDRSilentReadReq : LLVM_IR_DDRReadReq);

		// Add this new node to the mapping map (lol)
		ddrNodesToRootLS[newNode.ID] = burst.first;

		// If this is an out-bursted load, we must export the LLVM_IR_DDRReadReq node to be allocated to the DDDG before the current
		if(canOutBurst) {
			uint64_t expBaseAddress = (found->second.isRegistered)? found->second.baseAddress : burst.second.baseAddress;
			uint64_t expOffset = (found->second.isRegistered)? found->second.offset : burst.second.offset;
#ifdef VAR_WSIZE
			uint64_t expWordSize = (found->second.isRegistered)? found->second.wordSize : burst.second.wordSize;

			nodesToBeforeDDDG.push_back(nodeExportTy(newNode, loadNodes.at(burst.first).first, expBaseAddress, expOffset, expWordSize));
#else

			nodesToBeforeDDDG.push_back(nodeExportTy(newNode, loadNodes.at(burst.first).first, expBaseAddress, expOffset));
#endif
		}

		// Disconnect the edges incoming to the first load and connect to the read request
		std::set<Edge> edgesToRemove;
		std::vector<edgeTy> edgesToAdd;
		InEdgeIterator inEdgei, inEdgeEnd;
		for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(nameToVertex[burst.first], graph); inEdgei != inEdgeEnd; inEdgei++) {
			unsigned sourceID = vertexToName[boost::source(*inEdgei, graph)];
			edgesToRemove.insert(*inEdgei);

			// If an out-burst was detected, we isolate these nodes instead of connecting them, since they will be "executed" outside this DDDG
			// TODO: Perhaps we should isolate only relevant nodes (e.g. getelementptr and indexadd/sub)
			if(!canOutBurst)
				edgesToAdd.push_back({sourceID, newNode.ID, edgeToWeight[*inEdgei]});
		}
		// Connect this node to the first load
		// XXX: Does the edge weight matter here?
		edgesToAdd.push_back({newNode.ID, burst.first, 0});

		// Now, chain the loads to create the burst effect
		std::vector<unsigned> burstChain = burst.second.participants;
		for(unsigned i = 1; i < burstChain.size(); i++) {
			edgesToAdd.push_back({burstChain[i - 1], burstChain[i], 0});

			// Also disconnect getelementptr edges (as their index is implicitly calculated by burst logic)
			InEdgeIterator inEdgei, inEdgeEnd;
			for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(nameToVertex[burstChain[i]], graph); inEdgei != inEdgeEnd; inEdgei++) {
				if(LLVM_IR_GetElementPtr == microops.at(vertexToName[boost::source(*inEdgei, graph)]))
					edgesToRemove.insert(*inEdgei);
			}
		}

		// Update DDDG
		datapath->updateRemoveDDDGEdges(edgesToRemove);
		datapath->updateAddDDDGEdges(edgesToAdd);
	}

	// Add the relevant DDDG nodes for offchip store
	for(auto &burst : burstedStores) {
#if 0
		std::string arrayName = ddrBanking? storeNodes.at(burst.first).first : "";
#else
		std::string arrayName = storeNodes.at(burst.first).first;
#endif

		std::unordered_map<std::string, outBurstInfoTy>::iterator found = storeOutBurstsFound.find(arrayName);
		bool canOutBurst = (found != storeOutBurstsFound.end())? found->second.canOutBurst : false;

		// DDRWriteReq is positioned before the burst
		artificialNodeTy newNode = datapath->createArtificialNode(burst.first, canOutBurst? LLVM_IR_DDRSilentWriteReq : LLVM_IR_DDRWriteReq);

		// Add this new node to the mapping map (lol)
		ddrNodesToRootLS[newNode.ID] = burst.first;

		// If this is an out-bursted store, we must export the LLVM_IR_DDRWriteReq node to be allocated to the DDDG before the current
		if(canOutBurst) {
			uint64_t expBaseAddress = (found->second.isRegistered)? found->second.baseAddress : burst.second.baseAddress;
			uint64_t expOffset = (found->second.isRegistered)? found->second.offset : burst.second.offset;
#ifdef VAR_WSIZE
			uint64_t expWordSize = (found->second.isRegistered)? found->second.wordSize : burst.second.wordSize;

			nodesToBeforeDDDG.push_back(nodeExportTy(newNode, storeNodes.at(burst.first).first, expBaseAddress, expOffset, expWordSize));
#else

			nodesToBeforeDDDG.push_back(nodeExportTy(newNode, storeNodes.at(burst.first).first, expBaseAddress, expOffset));
#endif
		}

		// Two type of edges can come to a store: data and address
		// For data, we keep it at the appropriate writes
		// For address, we disconnect the incoming to the first store and connect to the write request
		std::set<Edge> edgesToRemove;
		std::vector<edgeTy> edgesToAdd;
		InEdgeIterator inEdgei, inEdgeEnd;
		for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(nameToVertex[burst.first], graph); inEdgei != inEdgeEnd; inEdgei++) {
			unsigned sourceID = vertexToName[boost::source(*inEdgei, graph)];
			uint8_t weight = edgeToWeight[*inEdgei];

			// Store can receive two parameters: 1 is data and 2 is address
			// We only want to move the address edges (weight 2)
			if(weight != 2)
				continue;

			edgesToRemove.insert(*inEdgei);

			// If an out-burst was detected, we isolate these nodes instead of connecting them, since they will be "executed" outside this DDDG
			if(!canOutBurst)
				edgesToAdd.push_back({sourceID, newNode.ID, weight});
		}
		// Connect this node to the first store
		// XXX: Does the edge weight matter here?
		edgesToAdd.push_back({newNode.ID, burst.first, 0});

		// Now, chain the stores to create the burst effect
		std::vector<unsigned> burstChain = burst.second.participants;
		for(unsigned i = 1; i < burstChain.size(); i++) {
			edgesToAdd.push_back({burstChain[i - 1], burstChain[i], 0});

			// Also disconnect getelementptr edges (as their index is implicitly calculated by burst logic)
			InEdgeIterator inEdgei, inEdgeEnd;
			for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(nameToVertex[burstChain[i]], graph); inEdgei != inEdgeEnd; inEdgei++) {
				if(LLVM_IR_GetElementPtr == microops.at(vertexToName[boost::source(*inEdgei, graph)]))
					edgesToRemove.insert(*inEdgei);
			}
		}

		// Update DDDG
		datapath->updateRemoveDDDGEdges(edgesToRemove);
		datapath->updateAddDDDGEdges(edgesToAdd);

		// DDRWriteResp is positioned after the last burst beat
		unsigned lastStore = burst.second.participants.back();
		newNode = datapath->createArtificialNode(lastStore, canOutBurst? LLVM_IR_DDRSilentWriteResp : LLVM_IR_DDRWriteResp);

		// Add this new node to the mapping map (lol)
		ddrNodesToRootLS[newNode.ID] = burst.first;

		// If this is an out-bursted store, we must export the LLVM_IR_DDRWriteResp node to be allocated to the DDDG after the current
		if(canOutBurst) {
			uint64_t expBaseAddress = (found->second.isRegistered)? found->second.baseAddress : burst.second.baseAddress;
			uint64_t expOffset = (found->second.isRegistered)? found->second.offset : burst.second.offset;
#ifdef VAR_WSIZE
			uint64_t expWordSize = (found->second.isRegistered)? found->second.wordSize : burst.second.wordSize;

			nodesToAfterDDDG.push_back(nodeExportTy(newNode, storeNodes.at(burst.first).first, expBaseAddress, expOffset, expWordSize));
#else

			nodesToAfterDDDG.push_back(nodeExportTy(newNode, storeNodes.at(burst.first).first, expBaseAddress, expOffset));
#endif
		}

		// Disconnect the edges outcoming from the last store and connect to the write response
		edgesToRemove.clear();
		edgesToAdd.clear();
		OutEdgeIterator outEdgei, outEdgeEnd;
		for(std::tie(outEdgei, outEdgeEnd) = boost::out_edges(nameToVertex[lastStore], graph); outEdgei != outEdgeEnd; outEdgei++) {
			unsigned destID = vertexToName[boost::target(*outEdgei, graph)];

			edgesToRemove.insert(*outEdgei);
			edgesToAdd.push_back({newNode.ID, destID, edgeToWeight[*outEdgei]});
		}
		// Connect this node to the last store
		// XXX: Does the edge weight matter here?
		edgesToAdd.push_back({lastStore, newNode.ID, 0});

		// Update DDDG
		datapath->updateRemoveDDDGEdges(edgesToRemove);
		datapath->updateAddDDDGEdges(edgesToAdd);
	}

	// Temporary map used for the recently-imported nodes. The main structures are only updated after the imported nodes
	// are successfully imported
	std::unordered_map<unsigned, unsigned> ddrNodesToRootLSTmp;
	std::unordered_map<unsigned, unsigned> rootLSToDDRNodesTmp;

	// Add the relevant DDDG nodes for imported offchip load
	for(auto &burst : importedLoads) {
		// Create a node with opcode LLVM_IR_DDRReadReq
		artificialNodeTy newNode = datapath->createArtificialNode(burst.first, LLVM_IR_DDRReadReq);

		// Add this new node to the mapping map (lol)
		ddrNodesToRootLSTmp[newNode.ID] = burst.first;

		// Connect this node to the first load
		// XXX: Does the edge weight matter here?
		std::vector<edgeTy> edgesToAdd;
		edgesToAdd.push_back({newNode.ID, burst.first, 0});

		// With conservative DDR scheduling policy, this imported transaction must execute after all DDR transactions from this DDDG
		//if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
			// Connect imported LLVM_IR_DDRReadReq to all last DDR transactions (LLVM_IR_DDRRead's and LLVM_IR_DDRWriteResp)

			// Find for LLVM_IR_DDRWriteResp
			for(auto &it : ddrNodesToRootLS) {
				int opcode = microops.at(it.first);

				if(LLVM_IR_DDRWriteResp == opcode) {
					// If DDR banking is active, we only create the edge if this node shares the same memory port as the imported one
					if(ddrBanking && (storeNodes.at(it.second).first != genFromImpLoadNodes.at(burst.first).first))
						continue;

					// XXX: Does the edge weight matter here?
					edgesToAdd.push_back({it.first, newNode.ID, 0});
				}
			}

			// Also, connect to the last node of DDR read transactions
			for(auto &it : burstedLoads) {
				// If DDR banking is active, we only create the edge if this node shares the same memory port as the imported one
				if(ddrBanking && (loadNodes.at(it.first).first != genFromImpLoadNodes.at(burst.first).first))
					continue;

				unsigned lastLoad = it.second.participants.back();
				// XXX: Does the edge weight matter here?
				edgesToAdd.push_back({lastLoad, newNode.ID, 0});
			}
		//}

		// Update DDDG
		datapath->updateAddDDDGEdges(edgesToAdd);
	}

	// Add the relevant DDDG nodes for imported offchip store
	for(auto &burst : importedStores) {
		// DDRWriteReq is positioned before the burst
		artificialNodeTy newNode;
		if(importedWriteReqs.count(burst.first))
			newNode = datapath->createArtificialNode(burst.first, LLVM_IR_DDRWriteReq);
		else if(importedWriteResps.count(burst.first))
			newNode = datapath->createArtificialNode(burst.first, LLVM_IR_DDRSilentWriteReq);
		else
			assert(false && "There is an artificially-generated store for imported write transactions, but no imported write node found");
		
		// Add this new node to the mapping map (lol)
		ddrNodesToRootLSTmp[newNode.ID] = burst.first;

		// Connect this node to the first store
		// XXX: Does the edge weight matter here?
		std::vector<edgeTy> edgesToAdd;
		edgesToAdd.push_back({newNode.ID, burst.first, 0});

		// With conservative DDR scheduling policy, this imported transaction must execute after (for LLVM_IR_DDRWriteReq)
		// all DDR transactions from this DDDG
		//if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
			// Connect imported LLVM_IR_DDRWriteReq to all last DDR transactions (LLVM_IR_DDRRead's and LLVM_IR_DDRWriteResp)
			if(importedWriteReqs.count(burst.first)) {
				// Find for LLVM_IR_DDRWriteResp
				for(auto &it : ddrNodesToRootLS) {
					int opcode = microops.at(it.first);

					if(LLVM_IR_DDRWriteResp == opcode) {
						// If DDR banking is active, we only create the edge if this node shares the same memory port as the imported one
						if(ddrBanking && (storeNodes.at(it.second).first != genFromImpStoreNodes.at(burst.first).first))
							continue;

						// XXX: Does the edge weight matter here?
						edgesToAdd.push_back({it.first, newNode.ID, 0});
					}
				}

				// Also, connect to the last node of DDR read transactions
				for(auto &it : burstedLoads) {
					// If DDR banking is active, we only create the edge if this node shares the same memory port as the imported one
					if(ddrBanking && (loadNodes.at(it.first).first != genFromImpStoreNodes.at(burst.first).first))
						continue;

					unsigned lastLoad = it.second.participants.back();
					edgesToAdd.push_back({lastLoad, newNode.ID, 0});
				}
			}
		//}

		// Update DDDG
		datapath->updateAddDDDGEdges(edgesToAdd);

		// DDRWriteResp is positioned after the last burst beat
		unsigned lastStore = burst.second.participants.back();
		if(importedWriteResps.count(burst.first))
			newNode = datapath->createArtificialNode(lastStore, LLVM_IR_DDRWriteResp);
		else if(importedWriteReqs.count(burst.first))
			newNode = datapath->createArtificialNode(lastStore, LLVM_IR_DDRSilentWriteResp);
		else
			assert(false && "There is an artificially-generated store for imported write transactions, but no imported write node found");

		// Add this new node to the mapping map (lol)
		ddrNodesToRootLSTmp[newNode.ID] = burst.first;
		// Reverse map is used on wrap-up
		rootLSToDDRNodesTmp[burst.first] = newNode.ID;

		// Connect this node to the last store
		// XXX: Does the edge weight matter here?
		edgesToAdd.clear();
		edgesToAdd.push_back({lastStore, newNode.ID, 0});

		// With conservative DDR scheduling policy, this imported transaction must execute before (for LLVM_IR_DDRWriteResp)
		// all DDR transactions from this DDDG
		//if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
			// Connect imported LLVM_IR_DDRWriteResp to all root DDR transactions (LLVM_IR_DDRReadReq's and LLVM_IR_DDRWriteReq's)
			if(importedWriteResps.count(burst.first)) {
				// Find for LLVM_IR_DDRReadReq and LLVM_IR_DDRWriteReq
				for(auto &it : ddrNodesToRootLS) {
					int opcode = microops.at(it.first);

					if(LLVM_IR_DDRReadReq == opcode) {
						// If DDR banking is active, we only create the edge if this node shares the same memory port as the imported one
						if(ddrBanking && (loadNodes.at(it.second).first != genFromImpStoreNodes.at(burst.first).first))
							continue;

						// XXX: Does the edge weight matter here?
						edgesToAdd.push_back({newNode.ID, it.first, 0});
					}
					if(LLVM_IR_DDRWriteReq == opcode) {
						// If DDR banking is active, we only create the edge if this node shares the same memory port as the imported one
						if(ddrBanking && (storeNodes.at(it.second).first != genFromImpStoreNodes.at(burst.first).first))
							continue;

						// XXX: Does the edge weight matter here?
						edgesToAdd.push_back({newNode.ID, it.first, 0});
					}
				}
			}
		//}

		// Update DDDG
		datapath->updateAddDDDGEdges(edgesToAdd);
	}

	// Wrap-up run. On cases for BETWEEN DDDGs, we might have imported nodes from both sides. We have to
	// ensure the order here as well. Here is simple: this logic is reciprocate, so if we guarantee that A comes after B,
	// for sure B will come before A (sounds stupid but hey). Only LLVM_IR_DDRWriteResp comes from the DDDG from that
	// direction. So we only check for this type of node against all the rest
	//if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
		std::vector<edgeTy> edgesToAdd;

		for(auto &burst : importedStores) {
			unsigned respID = rootLSToDDRNodesTmp.at(burst.first);

			if(importedWriteResps.count(burst.first)) {
				for(auto &it : ddrNodesToRootLSTmp) {
					int opcode = microops.at(it.first);

					if(LLVM_IR_DDRReadReq == opcode) {
						// If DDR banking is active, we only create the edge if this node shares the same memory port as the imported one
						if(ddrBanking && (genFromImpLoadNodes.at(it.second).first != genFromImpStoreNodes.at(burst.first).first))
							continue;

						// XXX: Does the edge weight matter here?
						edgesToAdd.push_back({respID, it.first, 0});
					}
					if(LLVM_IR_DDRWriteReq == opcode) {
						// If DDR banking is active, we only create the edge if this node shares the same memory port as the imported one
						if(ddrBanking && (genFromImpStoreNodes.at(it.second).first != genFromImpStoreNodes.at(burst.first).first))
							continue;

						// XXX: Does the edge weight matter here?
						edgesToAdd.push_back({respID, it.first, 0});
					}
				}
			}
		}

		// Update DDDG
		datapath->updateAddDDDGEdges(edgesToAdd);
	//}

	// After finished updating DDDG, we import all the data to the normal structures of MemoryModel
	burstedLoads.insert(importedLoads.begin(), importedLoads.end());
	burstedStores.insert(importedStores.begin(), importedStores.end());
	ddrNodesToRootLS.insert(ddrNodesToRootLSTmp.begin(), ddrNodesToRootLSTmp.end());
	loadNodes.insert(genFromImpLoadNodes.begin(), genFromImpLoadNodes.end());
	storeNodes.insert(genFromImpStoreNodes.begin(), genFromImpStoreNodes.end());

	datapath->refreshDDDG();

	// Create a dummy last node and connect the DDDG leaves and the imported nodes to it,
	// so that the imported nodes (if any) won't stay disconnected
	// TODO: maybe this should be performed somewhere else, in a different timing?
	if(importedWriteResps.size() || importedReadReqs.size() || importedWriteReqs.size()) {
		datapath->createDummySink();
		datapath->refreshDDDG();
	}

	// Save accessed array names to global DDR map
	if(ArgPack::MMA_MODE_GEN == args.mmaMode) {
		std::set<std::string> arrayNamesLoaded;
		std::set<std::string> arrayNamesStored;

#if 0
		// We only save the actual names if DDR banking is enabled
		if(ddrBanking) {
#endif
			for(auto &it : loadNodes)
				arrayNamesLoaded.insert(it.second.first);
			for(auto &it : storeNodes)
				arrayNamesStored.insert(it.second.first);
#if 0
		}
		else {
			arrayNames.insert("");
		}
#endif

		globalDDRMap[datapath->getTargetLoopName()].push_back(ddrInfoTy(
			datapath->getTargetLoopLevel(), datapath->getDatapathType(), arrayNamesLoaded, arrayNamesStored));
	}

#if 0
	errs() << "-- behavedLoads\n";
	for(auto const &x : behavedLoads)
		errs() << "-- " << std::to_string(x) << ": " << std::to_string(loadNodes[x]) << "\n";

	errs() << "-- behavedStores\n";
	for(auto const &x : behavedStores)
		errs() << "-- " << std::to_string(x) << ": " << std::to_string(storeNodes[x]) << "\n";

	errs() << "-- baseAddress\n";
	for(auto const &x : baseAddress)
		errs() << "-- " << std::to_string(x.first) << ": <" << x.second.first << ", " << std::to_string(x.second.second) << ">\n";
#endif

// TODO TODO TODO TODO
// TODO TODO TODO TODO
// TODO TODO TODO TODO
// TODO TODO TODO TODO
// TODO TODO TODO TODO
//#ifdef DBG_PRINT_ALL
#if 1
	printDatabase();
#endif
}

bool XilinxZCUMemoryModel::tryAllocate(unsigned node, int opcode, bool commit) {
	// Two different memory policies are implemented here:
	// Conservative:
	// - Read and write transactions can coexist if their regions do not overlap;
	// - A read request can be issued when a read transaction is working only if the current read transaction is unbursted;
	// - Write requests are exclusive;
	// Overlapping:
	// - Any transaction can coexist if their regions do not overlap;
	// Please note that if DDR banking is active, active reads and writes will be filtered by array

	if(LLVM_IR_DDRReadReq == opcode || LLVM_IR_DDRSilentReadReq == opcode) {
		unsigned nodeToRootLS = ddrNodesToRootLS.at(node);
		burstInfoTy readInfo = burstedLoads.at(nodeToRootLS);
		// If no DDR banking, we deactivate filtering by array (i.e. "")
		std::string readArrayName = ddrBanking? loadNodes[nodeToRootLS].first : "";
		uint64_t readBase = readInfo.baseAddress;
#ifdef VAR_WSIZE
		uint64_t readEnd = readBase + readInfo.offset * readInfo.wordSize;
#else
		// XXX: As always, assuming 32-bit
		uint64_t readEnd = readBase + readInfo.offset * 4;
#endif

		std::set<unsigned> &curActiveReads = activeReads[readArrayName];
		std::set<unsigned> &curActiveWrites = activeWrites[readArrayName];
		bool &curReadActive = readActive[readArrayName];
		bool &curWriteActive = writeActive[readArrayName];

		bool finalCond;
		if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
			// Read requests are issued when:

			// - No active transactions;
			// XXX: readActive and writeActive already handle this

			// - If there are active reads, they must be unbursted;
			bool allActiveReadsAreUnbursted = true;
			if(curReadActive) {
				for(auto &it : curActiveReads) {
					if(burstedLoads.at(it).offset) {
						allActiveReadsAreUnbursted = false;
						break;
					}
				}
			}

			// - If there are active writes, they must be for different regions
			bool activeWritesDoNotOverlap = true;
			if(curWriteActive) {
				for(auto &it : curActiveWrites) {
					burstInfoTy otherInfo = burstedStores.at(it);
					uint64_t otherBase = otherInfo.baseAddress;
#ifdef VAR_WSIZE
					uint64_t otherEnd = otherBase + otherInfo.offset * otherInfo.wordSize;
#else
					// XXX: As always, assuming 32-bit
					uint64_t otherEnd = otherBase + otherInfo.offset * 4;
#endif

					if(otherEnd >= readBase && readEnd >= otherBase)
						activeWritesDoNotOverlap = false;
				}
			}

			finalCond = (!curReadActive && !curWriteActive) || (allActiveReadsAreUnbursted && activeWritesDoNotOverlap);
		}
		else if(ArgPack::DDR_POLICY_CAN_OVERLAP == args.ddrSched) {
			// Read requests are issued when:

			// - No active transactions;
			// XXX: readActive and writeActive already handle this

			// - If there are active reads, they must be for different regions
			bool activeReadsDoNotOverlap = true;
			if(curReadActive) {
				for(auto &it : curActiveReads) {
					burstInfoTy otherInfo = burstedLoads.at(it);
					uint64_t otherBase = otherInfo.baseAddress;
#ifdef VAR_WSIZE
					uint64_t otherEnd = otherBase + otherInfo.offset * otherInfo.wordSize;
#else
					// XXX: As always, assuming 32-bit
					uint64_t otherEnd = otherBase + otherInfo.offset * 4;
#endif

					if(otherEnd >= readBase && readEnd >= otherBase)
						activeReadsDoNotOverlap = false;
				}
			}

			// - If there are active writes, they must be for different regions
			bool activeWritesDoNotOverlap = true;
			if(curWriteActive) {
				for(auto &it : curActiveWrites) {
					burstInfoTy otherInfo = burstedStores.at(it);
					uint64_t otherBase = otherInfo.baseAddress;
#ifdef VAR_WSIZE
					uint64_t otherEnd = otherBase + otherInfo.offset * otherInfo.wordSize;
#else
					// XXX: As always, assuming 32-bit
					uint64_t otherEnd = otherBase + otherInfo.offset * 4;
#endif

					if(otherEnd >= readBase && readEnd >= otherBase)
						activeWritesDoNotOverlap = false;
				}
			}

			finalCond = (!curReadActive && !curWriteActive) || (activeReadsDoNotOverlap && activeWritesDoNotOverlap);
		}
		else {
			assert(false && "Invalid DDR scheduling policy selected (this assert should never execute)");
		}

		if(finalCond) {
			if(commit) {
				activeReads[readArrayName].insert(nodeToRootLS);
				readActive[readArrayName] = true;
			}
		}

		return finalCond;
	}
	else if(LLVM_IR_DDRRead == opcode || LLVM_IR_DDRSilentRead == opcode) {
		// If no DDR banking, we deactivate filtering by array (i.e. "")
		std::string readArrayName = ddrBanking? loadNodes.at(node).first : "";

		// Read transactions can always be issued, because their issuing is conditional to ReadReq

		// Since LLVM_IR_DDRRead takes only one cycle, its release logic is located here (MemoryModel::release() is not executed)
		if(commit) {
			// If this is the last load, we must close this burst
			for(auto &it : burstedLoads) {
				if(it.second.participants.back() == node)
					activeReads[readArrayName].erase(it.first);
			}

			if(!(activeReads[readArrayName].size()))
				readActive[readArrayName] = false;
		}

		return true;
	}
	else if(LLVM_IR_DDRWriteReq == opcode || LLVM_IR_DDRSilentWriteReq == opcode) {
		unsigned nodeToRootLS = ddrNodesToRootLS.at(node);
		burstInfoTy writeInfo = burstedStores.at(nodeToRootLS);
		// If no DDR banking, we deactivate filtering by array (i.e. "")
		std::string writeArrayName = ddrBanking? storeNodes.at(nodeToRootLS).first : "";
		uint64_t writeBase = writeInfo.baseAddress;
#ifdef VAR_WSIZE
		uint64_t writeEnd = writeBase + writeInfo.offset * writeInfo.wordSize;
#else
		// XXX: As always, assuming 32-bit
		uint64_t writeEnd = writeBase + writeInfo.offset * 4;
#endif

		std::set<unsigned> &curActiveReads = activeReads[writeArrayName];
		std::set<unsigned> &curActiveWrites = activeWrites[writeArrayName];
		bool &curReadActive = readActive[writeArrayName];
		bool &curWriteActive = writeActive[writeArrayName];

		bool finalCond;
		if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
			// Write requests are issued when:

			// - No other active write.
			finalCond = !curWriteActive;
		}
		else if(ArgPack::DDR_POLICY_CAN_OVERLAP == args.ddrSched) {
			// Write requests are issued when:

			// - No active transactions;
			// XXX: readActive and writeActive already handle this

			// - If there are active reads, they must be for different regions
			bool activeReadsDoNotOverlap = true;
			if(curReadActive) {
				for(auto &it : curActiveReads) {
					burstInfoTy otherInfo = burstedLoads.at(it);
					uint64_t otherBase = otherInfo.baseAddress;
#ifdef VAR_WSIZE
					uint64_t otherEnd = otherBase + otherInfo.offset * otherInfo.wordSize;
#else
					// XXX: As always, assuming 32-bit
					uint64_t otherEnd = otherBase + otherInfo.offset * 4;
#endif

					if(otherEnd >= writeBase && writeEnd >= otherBase)
						activeReadsDoNotOverlap = false;
				}
			}

			// - If there are active writes, they must be for different regions
			bool activeWritesDoNotOverlap = true;
			if(curWriteActive) {
				for(auto &it : curActiveWrites) {
					burstInfoTy otherInfo = burstedStores.at(it);
					uint64_t otherBase = otherInfo.baseAddress;
#ifdef VAR_WSIZE
					uint64_t otherEnd = otherBase + otherInfo.offset * otherInfo.wordSize;
#else
					// XXX: As always, assuming 32-bit
					uint64_t otherEnd = otherBase + otherInfo.offset * 4;
#endif

					if(otherEnd >= writeBase && writeEnd >= otherBase)
						activeWritesDoNotOverlap = false;
				}
			}

			finalCond = (!curReadActive && !curWriteActive) || (activeReadsDoNotOverlap && activeWritesDoNotOverlap);
		}

		if(finalCond) {
			if(commit) {
				activeWrites[writeArrayName].insert(nodeToRootLS);
				writeActive[writeArrayName] = true;
			}
		}

		return finalCond;
	}
	else if(LLVM_IR_DDRWrite == opcode || LLVM_IR_DDRSilentWrite == opcode) {
		// Write transactions can always be issued, because their issuing is conditional to WriteReq
		return true;
	}
	else if(LLVM_IR_DDRWriteResp == opcode || LLVM_IR_DDRSilentWriteResp == opcode) {
		unsigned nodeToRootLS = ddrNodesToRootLS.at(node);
		burstInfoTy writeInfo = burstedStores.at(nodeToRootLS);
		// If no DDR banking, we deactivate filtering by array (i.e. "")
		std::string writeArrayName = ddrBanking? storeNodes.at(nodeToRootLS).first : "";
		uint64_t writeBase = writeInfo.baseAddress;
#ifdef VAR_WSIZE
		uint64_t writeEnd = writeBase + writeInfo.offset * writeInfo.wordSize;
#else
		// XXX: As always, assuming 32-bit
		uint64_t writeEnd = writeBase + writeInfo.offset * 4;
#endif

		std::set<unsigned> &curActiveReads = activeReads[writeArrayName];
		bool &curReadActive = readActive[writeArrayName];

		bool finalCond;
		if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
			// Write responses (when the data is actually sent to DDR) are issued when:

			// - No active read
			// XXX: readActive already handles this

			// - If there are active reads, they must be for different regions
			bool activeReadsDoNotOverlap = true;
			if(curReadActive) {
				for(auto &it : curActiveReads) {
					burstInfoTy otherInfo = burstedLoads.at(it);
					uint64_t otherBase = otherInfo.baseAddress;
#ifdef VAR_WSIZE
					uint64_t otherEnd = otherBase + otherInfo.offset * otherInfo.wordSize;
#else
					// XXX: As always, assuming 32-bit
					uint64_t otherEnd = otherBase + otherInfo.offset * 4;
#endif

					if(otherEnd >= writeBase && writeEnd >= otherBase)
						activeReadsDoNotOverlap = false;
				}
			}

			finalCond = !curReadActive || activeReadsDoNotOverlap;
		}
		else if(ArgPack::DDR_POLICY_CAN_OVERLAP == args.ddrSched) {
			// Write responses can always be issued, because their issuing is conditional to WriteReq
			// XXX: Should the overlap check logic from LLVM_IR_DDRWriteReq be here as well, or only here?
			finalCond = true;
		}

		return finalCond;
	}
	else {
		return true;
	}
}

void XilinxZCUMemoryModel::release(unsigned node, int opcode) {
	if(LLVM_IR_DDRWriteResp == opcode || LLVM_IR_DDRSilentWriteResp == opcode) {
		unsigned nodeToRootLS = ddrNodesToRootLS.at(node);
		// If no DDR banking, we deactivate filtering by array (i.e. "")
		std::string writeArrayName = ddrBanking? storeNodes.at(nodeToRootLS).first : "";
		activeWrites[writeArrayName].erase(nodeToRootLS);

		if(!(activeWrites[writeArrayName].size()))
			writeActive[writeArrayName] = false;
	}
}

bool XilinxZCUMemoryModel::canOutBurstsOverlap() {
	return MemoryModel::canOutBurstsOverlap(nodesToBeforeDDDG, nodesToAfterDDDG);
}

void XilinxZCUMemoryModel::setUpWithContext(ContextManager &CtxM) {
	CtxM.getOutBurstsInfo(wholeLoopName, datapath->getDatapathType(), &loadOutBurstsFoundCached, &storeOutBurstsFoundCached);
#ifdef CROSS_DDDG_PACKING
	CtxM.getRemainingBudgetInfo(wholeLoopName, datapath->getDatapathType(), &loadRemainingBudget, &storeRemainingBudget);
#endif

	std::unordered_map<std::string, std::vector<ddrInfoTy>>::iterator found = globalDDRMap.find(datapath->getTargetLoopName());
	assert(found != globalDDRMap.end() && "Loop not found in global DDR map");
	filteredDDRMap = found->second;

	importedFromContext = true;
}

void XilinxZCUMemoryModel::saveToContext(ContextManager &CtxM) {
	CtxM.saveOutBurstsInfo(wholeLoopName, datapath->getDatapathType(), loadOutBurstsFoundCached, storeOutBurstsFoundCached);
#ifdef CROSS_DDDG_PACKING
	CtxM.saveRemainingBudgetInfo(wholeLoopName, datapath->getDatapathType(), loadRemainingBudget, storeRemainingBudget);
#endif
}

void XilinxZCUMemoryModel::importNode(nodeExportTy &exportedNode) {
	if(LLVM_IR_DDRSilentReadReq == exportedNode.node.opcode) {
		artificialNodeTy newNode = datapath->createArtificialNode(exportedNode.node, LLVM_IR_DDRSilentRead);
		importedReadReqs.insert(newNode.ID);

		std::vector<unsigned> nodes;
		nodes.push_back(newNode.ID);
#ifdef VAR_WSIZE
		importedLoads[newNode.ID] = burstInfoTy(exportedNode.baseAddress, exportedNode.offset, exportedNode.wordSize, nodes);
#else
		importedLoads[newNode.ID] = burstInfoTy(exportedNode.baseAddress, exportedNode.offset, nodes);
#endif
		genFromImpLoadNodes[newNode.ID] = std::make_pair(exportedNode.arrayName, exportedNode.baseAddress);
	}
	else if(LLVM_IR_DDRSilentWriteReq == exportedNode.node.opcode) {
		artificialNodeTy newNode = datapath->createArtificialNode(exportedNode.node, LLVM_IR_DDRSilentWrite);
		importedWriteReqs.insert(newNode.ID);

		std::vector<unsigned> nodes;
		nodes.push_back(newNode.ID);
#ifdef VAR_WSIZE
		importedStores[newNode.ID] = burstInfoTy(exportedNode.baseAddress, exportedNode.offset, exportedNode.wordSize, nodes);
#else
		importedStores[newNode.ID] = burstInfoTy(exportedNode.baseAddress, exportedNode.offset, nodes);
#endif
		genFromImpStoreNodes[newNode.ID] = std::make_pair(exportedNode.arrayName, exportedNode.baseAddress);
	}
	else if(LLVM_IR_DDRSilentWriteResp == exportedNode.node.opcode) {
		artificialNodeTy newNode = datapath->createArtificialNode(exportedNode.node, LLVM_IR_DDRSilentWrite);
		importedWriteResps.insert(newNode.ID);

		std::vector<unsigned> nodes;
		nodes.push_back(newNode.ID);
#ifdef VAR_WSIZE
		importedStores[newNode.ID] = burstInfoTy(exportedNode.baseAddress, exportedNode.offset, exportedNode.wordSize, nodes);
#else
		importedStores[newNode.ID] = burstInfoTy(exportedNode.baseAddress, exportedNode.offset, nodes);
#endif
		genFromImpStoreNodes[newNode.ID] = std::make_pair(exportedNode.arrayName, exportedNode.baseAddress);
	}
	else {
		assert(false && "Invalid type of node imported to the memory model");
	}
}

std::vector<MemoryModel::nodeExportTy> &XilinxZCUMemoryModel::getNodesToBeforeDDDG() {
	return nodesToBeforeDDDG;
}

std::vector<MemoryModel::nodeExportTy> &XilinxZCUMemoryModel::getNodesToAfterDDDG() {
	return nodesToAfterDDDG;
}

// TODO TODO TODO TODO
// TODO TODO TODO TODO
// TODO TODO TODO TODO
// TODO TODO TODO TODO
// TODO TODO TODO TODO
//#ifdef DBG_PRINT_ALL
#if 1
void XilinxZCUMemoryModel::printDatabase() {
	errs() << "-- loadNodes\n";
	for(auto const &x : loadNodes)
		errs() << "-- " << std::to_string(x.first) << ": <" << x.second.first << ", " << std::to_string(x.second.second) << ">\n";
	errs() << "-- ---------\n";

	errs() << "-- burstedLoads\n";
	for(auto const &x : burstedLoads) {
#ifdef VAR_WSIZE
		errs() << "-- " << x.first << ": <" << std::to_string(x.second.baseAddress) << ", " << std::to_string(x.second.offset) << ", " << std::to_string(x.second.wordSize) << ",\n";
#else
		errs() << "-- " << x.first << ": <" << std::to_string(x.second.baseAddress) << ", " << std::to_string(x.second.offset) << ", " << ",\n";
#endif
		for(auto const &y : x.second.participants)
			errs() << "-- -- " << std::to_string(y) << "\n";
		errs() << "-- >\n";
	}
	errs() << "-- ------------\n";

	errs() << "-- storeNodes\n";
	for(auto const &x : storeNodes)
		errs() << "-- " << std::to_string(x.first) << ": <" << x.second.first << ", " << std::to_string(x.second.second) << ">\n";
	errs() << "-- ----------\n";

	errs() << "-- burstedStores\n";
	for(auto const &x : burstedStores) {
#ifdef VAR_WSIZE
		errs() << "-- " << x.first << ": <" << std::to_string(x.second.baseAddress) << ", " << std::to_string(x.second.offset) << ", " << std::to_string(x.second.wordSize) << ",\n";
#else
		errs() << "-- " << x.first << ": <" << std::to_string(x.second.baseAddress) << ", " << std::to_string(x.second.offset) << ", " << ",\n";
#endif
		for(auto const &y : x.second.participants)
			errs() << "-- -- " << std::to_string(y) << "\n";
		errs() << "-- >\n";
	}
	errs() << "-- -------------\n";

	errs() << "-- memoryTraceMap\n";
	for(auto const &x : memoryTraceMap) {
		errs() << "-- <" << x.first.first << ", " << x.first.second << ">\n";
		for(auto const &y : x.second)
			errs() << "---- " << y << "\n";
	}
	errs() << "-----------------\n";
}
#endif
