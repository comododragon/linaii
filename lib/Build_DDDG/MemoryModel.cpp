#include "profile_h/MemoryModel.h"

#include "profile_h/BaseDatapath.h"

extern memoryTraceMapTy memoryTraceMap;
extern bool memoryTraceGenerated;

std::unordered_map<std::string, std::vector<ddrInfoTy>> globalDDRMap;
std::unordered_map<std::string, std::vector<globalOutBurstsInfoTy>> globalOutBurstsInfo;
std::unordered_map<arrayPackSzPairTy, std::vector<packInfoTy>, boost::hash<arrayPackSzPairTy>> globalPackInfo;
std::unordered_map<std::string, std::pair<unsigned, unsigned>> globalPackSizes;

// Static attributes
const std::string Reporter::warnReasonMap[] = {
	"detected reads for the same array",
	"detected other reads",
	"detected writes for the same array",
	"detected other writes",
	"cannot align write with pack size",
	"cannot align read with pack size",
	"misaligned read (left and/or right) comprises more than one element"
};
std::ofstream Reporter::rptFile;
std::string Reporter::loopName;

void Reporter::open(std::string loopName) {
	Reporter::loopName = loopName;
	rptFile.open(loopName + FILE_MEM_ANALYSIS_RPT_SUFFIX, std::ios::trunc);
}

void Reporter::reopen(unsigned loopLevel, unsigned datapathType, ParsedTraceContainer *PC, std::vector<int> *microops) {
	setCurrent(loopLevel, datapathType, PC, microops);
	rptFile.open(loopName + FILE_MEM_ANALYSIS_RPT_SUFFIX, std::ios::app);
}

bool Reporter::isOpen() {
	return rptFile.is_open();
}

void Reporter::close() {
	rptFile.close();
}

void Reporter::setCurrent(unsigned loopLevel, unsigned datapathType, ParsedTraceContainer *PC, std::vector<int> *microops) {
	this->loopLevel = loopLevel;
	this->datapathType = datapathType;
	this->PC = PC;
	this->microops = microops;
}

void Reporter::header() {
	rptFile << "===========================================================================================\n";
	rptFile << "Memory model analysis report for loop nest " << loopName << "\n";
	rptFile << "===========================================================================================\n";
	rptFile << "Preprocess report for this loop nest\n";
}

void Reporter::currentHeader() {
	rptFile << "Memory model analysis for loop level " << loopLevel << " " << translateDatapathType(datapathType) << "\n";
}

std::string Reporter::translateDatapathType(unsigned datapathType) {
	if(DatapathType::NON_PERFECT_BEFORE == datapathType)
		return "at region before the loop nest";
	else if(DatapathType::NON_PERFECT_AFTER == datapathType)
		return "at region after the loop nest";
	else if(DatapathType::NON_PERFECT_BETWEEN == datapathType)
		return "at region between unrolled loop nests";
	else
		return "within the loop nest";
}

void Reporter::infoReadOutBurstFound(std::string arrayName) {
	rptFile << "\n[INFO-001] Found possibility of read burst between loop iterations\n";
	rptFile << "           for array " << arrayName << "\n";
	rptFile << "           at loop level " << loopLevel << "\n";
	rptFile << "           " << translateDatapathType(datapathType) << "\n";
}

void Reporter::infoWriteOutBurstFound(std::string arrayName) {
	rptFile << "\n[INFO-002] Found possibility of write burst between loop iterations\n";
	rptFile << "           for array " << arrayName << "\n";
	rptFile << "           at loop level " << loopLevel << "\n";
	rptFile << "           " << translateDatapathType(datapathType) << "\n";
}

void Reporter::infoPackAttempt(std::string arrayName, unsigned sz) {
	rptFile << "\n[INFO-003] Vectorisation attempt successful\n";
	rptFile << "           for array " << arrayName << "\n";
	rptFile << "           with " << sz << " elements vectorised\n";
}

void Reporter::infoInBurstFound(std::string arrayName, uint64_t offset, std::vector<unsigned> &participants) {
	const std::vector<int> &lineNoList = PC->getLineNoList();
	std::string type = (LLVM_IR_DDRRead == microops->at(participants[0]))? "read" : "write";

	rptFile << "\n[INFO-004] Found possibility of " << type << " burst inside this loop iteration\n";
	rptFile << "           for array " << arrayName << "\n";
	rptFile << "           at loop level " << loopLevel << "\n";
	rptFile << "           " << translateDatapathType(datapathType) << "\n";
	rptFile << "           # of merged transactions: " << (offset + 1) << "\n";
	rptFile << "           participants:\n";
	for(auto &it : participants)
		rptFile << "                     line " << lineNoList.at(it) << "\n";
}

void Reporter::warnPackAttemptFailed(
	std::string arrayName, unsigned sz, unsigned warnReason,
	unsigned lmisalign, unsigned rmisalign, unsigned otherLoopLevel, unsigned otherDatapathType
) {
	// XXX warnReason + 4 will generate a separate WARN-00X ID for every reason
	rptFile << "\n[WARN-" << stringFormat<unsigned>(warnReason + 4, 3) << "] Vectorisation attempt failed\n";
	rptFile << "           for array " << arrayName << "\n";
	rptFile << "           with " << sz << " elements vectorised\n";
	rptFile << "           Reason: " << warnReasonMap[warnReason] << "\n";
	if(lmisalign || rmisalign) {
		if(lmisalign)
			rptFile << "                   due to " << lmisalign << " unused element(s) before\n";
		if(rmisalign)
			rptFile << "                   due to " << rmisalign << " unused element(s) after\n";
		rptFile << "                   at loop level " << otherLoopLevel << "\n";
		rptFile << "                   " << translateDatapathType(otherDatapathType) << "\n";
	}
}

void Reporter::warnOutBurstFailed(std::string arrayName, unsigned warnReason, unsigned otherLoopLevel, unsigned otherDatapathType) {
	// XXX warnReason + 11 will generate a separate WARN-0XX ID for every reason while not overlapping with the ones defined above
	rptFile << "\n[WARN-" << stringFormat<unsigned>(warnReason + 11, 3) << "] Burst possibility between loop iterations failed\n";
	rptFile << "           for array " << arrayName << "\n";
	rptFile << "           at loop level " << loopLevel << "\n";
	rptFile << "           " << translateDatapathType(datapathType) << "\n";
	rptFile << "           Reason: " << warnReasonMap[warnReason] << "\n";
	rptFile << "                   at loop level " << otherLoopLevel << "\n";
	rptFile << "                   " << translateDatapathType(otherDatapathType) << "\n";
}

void Reporter::warnOutBurstNotPossible(std::string arrayName) {
	rptFile << "\n[WARN-001] Burst possibility between loop iterations likely not possible\n";
	rptFile << "       for array " << arrayName << "\n";
	rptFile << "       at loop level " << loopLevel << "\n";
	rptFile << "       " << translateDatapathType(datapathType) << "\n";
	rptFile << "       Reason: more than one transaction for the same array\n";
	rptFile << "               found in this loop level. Use \"--f-burstaggr\"\n";
	rptFile << "               to attempt to merge these transactions if they're contiguous.\n";
	rptFile << "               Note that such burst inference with Vivado HLS will likely\n";
	rptFile << "               require explicit code intervention\n";
}

void Reporter::warnReadAfterWrite(std::string readArrayName, unsigned readNode, std::string writeArrayName, unsigned writeNode) {
	const std::vector<int> &lineNoList = PC->getLineNoList();

	rptFile << "\n[WARN-002] Detected read after a write transaction\n";
	rptFile << "       Vivado will likely serialise these transactions and severely hurt performance\n";
	rptFile << "       Read at array " << readArrayName << " at line " << lineNoList.at(readNode) << "\n";
	rptFile << "       after write at array " << writeArrayName << " at line " << lineNoList.at(writeNode) << "\n";
	rptFile << "       Consider (if possible) reordering transactions such that all reads are placed\n";
	rptFile << "       before any write (with the use of temporary variables)\n";
	rptFile << "       If this issue is caused by loop unrolling, consider implementing the unroll\n";
	rptFile << "       manually\n";
}

void Reporter::warnReadAfterWriteDueUnroll() {
	rptFile << "\n[WARN-003] Detected reads and writes transactions in an unrolled code segment\n";
	rptFile << "       Vivado will likely replicate the code and put reads after writes,\n";
	rptFile << "       potentially hurting performance due to serialisation\n";
	rptFile << "       Consider (if possible) manually implementing the loop unroll and explicitly\n";
	rptFile << "       placing all reads before any write with the help of temporary variables\n";
}

void Reporter::footer() {
	rptFile << "\n===========================================================================================\n";
}

// Static attributes
bool MemoryModel::shouldRpt = false;
Reporter MemoryModel::reporter;
std::string MemoryModel::preprocessedLoopName = "";

MemoryModel::MemoryModel(BaseDatapath *datapath) :
	datapath(datapath), microops(datapath->getMicroops()), graph(datapath->getDDDG()),
	nameToVertex(datapath->getNameToVertex()), vertexToName(datapath->getVertexToName()), edgeToWeight(datapath->getEdgeToWeight()),
	baseAddress(datapath->getBaseAddress()), CM(datapath->getConfigurationManager()), PC(datapath->getParsedTraceContainer()), memoryTraceList(PC.getMemoryTraceList()) {
	wholeLoopName = appendDepthToLoopName(datapath->getTargetLoopName(), datapath->getTargetLoopLevel());
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

bool MemoryModel::preprocess(std::string loopName) {
	if(loopName != preprocessedLoopName) {
		preprocessedLoopName = loopName;

		// Setup reporter
		if(reporter.isOpen())
			reporter.close();
		reporter.open(preprocessedLoopName);
		reporter.header();

		return true;
	}

	return false;
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
}

void MemoryModel::enableReport() {
	shouldRpt = true;
}

void MemoryModel::finishReport() {
	if(reporter.isOpen()) {
		if(shouldRpt)
			reporter.footer();
		reporter.close();
	}
	shouldRpt = false;
}

void MemoryModel::analyseAndTransform() { }

// Static attributes
std::vector<ddrInfoTy> XilinxZCUMemoryModel::filteredDDRMap;
std::unordered_map<std::string, std::vector<globalOutBurstsInfoTy>>::iterator XilinxZCUMemoryModel::filteredOutBurstsInfo;
bool XilinxZCUMemoryModel::ddrBanking = false;
std::unordered_map<std::string, unsigned> XilinxZCUMemoryModel::packSizes;

void XilinxZCUMemoryModel::preprocess(std::string loopName, ConfigurationManager &CM) {
	// Run parent preprocess. It will return false if preprocess was already executed for this loop nest
	if(MemoryModel::preprocess(loopName)) {
		// If MMA mode is set to USE, we use the context-imported cache information to improve out-bursts
		if(ArgPack::MMA_MODE_USE == args.mmaMode) {
			std::unordered_map<std::string, std::vector<ddrInfoTy>>::iterator found = globalDDRMap.find(loopName);
			assert(found != globalDDRMap.end() && "Loop not found in global DDR map");
			filteredDDRMap = found->second;

			filteredOutBurstsInfo = globalOutBurstsInfo.find(loopName);
			assert(filteredOutBurstsInfo != globalOutBurstsInfo.end() && "Loop not found in global out-bursts info");

			ddrBanking = CM.getGlobalCfg<bool>(ConfigurationManager::globalCfgTy::GLOBAL_DDRBANKING);

			for(unsigned loopLevel = LpName2numLevelMap.at(loopName); loopLevel; loopLevel--) {
				for(auto &it : filteredOutBurstsInfo->second) {
					if(it.loopLevel == loopLevel) {
						for(auto it2 : it.loadOutBurstsFound) {
							if(it2.second.canOutBurst) {
								reporter.setCurrent(it.loopLevel, it.datapathType);
								reporter.infoReadOutBurstFound(it2.first);
							}
						}
						for(auto it2 : it.storeOutBurstsFound) {
							if(it2.second.canOutBurst) {
								reporter.setCurrent(it.loopLevel, it.datapathType);
								reporter.infoWriteOutBurstFound(it2.first);
							}
						}

						// There is no need to block anything from the deepest level
						if(loopLevel < LpName2numLevelMap.at(loopName)) {
							// Perform global out-burst analysis using context-imported information
							if(DatapathType::NON_PERFECT_BEFORE == it.datapathType || DatapathType::NON_PERFECT_AFTER == it.datapathType) {
								blockInvalidOutBursts(loopLevel, it.datapathType, it.loadOutBurstsFound, &XilinxZCUMemoryModel::analyseLoadOutBurstFeasabilityGlobal);
								blockInvalidOutBursts(loopLevel, it.datapathType, it.storeOutBurstsFound, &XilinxZCUMemoryModel::analyseStoreOutBurstFeasabilityGlobal);
							}
						}
					}
				}
			}

			// Perform global pack analysis for every array
			for(auto &it : CM.getArrayInfoCfgMap()) {
				unsigned szCandidate = 1;

				for(unsigned sz = 16; sz - 1; sz >>= 1) {
					std::unordered_map<arrayPackSzPairTy, std::vector<packInfoTy>, boost::hash<arrayPackSzPairTy>>::iterator found = globalPackInfo.find(std::make_pair(it.first, sz));

					if(found != globalPackInfo.end()) {
						// Iterate thru all DDDGs using this array
						bool invalid = false;
						for(auto &it2 : found->second) {
							// Filter 1: Invalid this size if there are any stores with left or right alignments != 0
							for(auto &it3 : it2.storeAlignments) {
								if(it3.second.first || it3.second.second) {
									reporter.warnPackAttemptFailed(
										it.first, sz, Reporter::WARN_PACK_WRITE_MISALIGN,
										it3.second.first, it3.second.second, it2.loopLevel, it2.datapathType
									);
									invalid = true;
									break;
								}
							}
							if(invalid)
								break;

							// Filter 2: Invalid this size if there are any validated out-bursts and left or right alignments are != 0
							//           If there is no out-burst, we invalid this size if left + right != (sz - 1) when at least one is != 0
							//           (in other words, w/o out-burst only a transaction that uses one word is allowed)
							for(auto &it3 : filteredOutBurstsInfo->second) {
								if(it3.loopLevel == it2.loopLevel && it3.datapathType == it2.datapathType) {
									std::unordered_map<std::string, outBurstInfoTy>::iterator found2 = it3.loadOutBurstsFound.find(it.first);
									if(found2 != it3.loadOutBurstsFound.end() && found2->second.canOutBurst) {
										for(auto &it4 : it2.loadAlignments) {
											if(it4.second.first || it4.second.second) {
												reporter.warnPackAttemptFailed(
													it.first, sz, Reporter::WARN_PACK_READ_MISALIGN,
													it4.second.first, it4.second.second, it2.loopLevel, it2.datapathType
												);
												invalid = true;
												break;
											}
										}
									}
									else {
										for(auto &it4 : it2.loadAlignments) {
											if((it4.second.first || it4.second.second) && (it4.second.first + it4.second.second != (sz - 1))) {
												reporter.warnPackAttemptFailed(it.first, sz, Reporter::WARN_PACK_NOT_POSSIBLE, 0, 0, 0, 0);
												invalid = true;
												break;
											}
										}
									}
								}
								if(invalid)
									break;
							}
							if(invalid)
								break;
						}

						// If no invalid cases was found for this pack size. Congratulations we have a valid pack size!
						if(!invalid) {
							szCandidate = sz;
							break;
						}
					}
				}

				// Save pack size
				packSizes[it.first] = szCandidate;
				if(szCandidate > 1) reporter.infoPackAttempt(it.first, szCandidate);
			}
		}

		if(reporter.isOpen()) {
			reporter.footer();
			reporter.close();
		}
	}
}

void XilinxZCUMemoryModel::blockInvalidOutBursts(
	unsigned loopLevel, unsigned datapathType,
	std::unordered_map<std::string, outBurstInfoTy> &outBurstsFound,
	bool (*analyseOutBurstFeasabilityGlobal)(std::string, unsigned, unsigned)
) {
	// The same logic performed locally by findOutBursts(), will be performed globally here
	for(auto &it : outBurstsFound) {
		// If it is already false, no need to analyse then
		if(it.second.canOutBurst) {
			std::string arrayName = it.first;

			// If with global information we assume that is not feasible, too bad, cancel this out-burst
			if(!((*analyseOutBurstFeasabilityGlobal)(it.first, loopLevel, datapathType)))
				it.second.canOutBurst = false;
		}
	}
}

bool XilinxZCUMemoryModel::analyseLoadOutBurstFeasabilityGlobal(std::string arrayName, unsigned loopLevel, unsigned datapathType) {
	reporter.setCurrent(loopLevel, datapathType);

	if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
		for(auto &it : filteredDDRMap) {
			// Ignore elements that are from upper loops
			if(it.loopLevel < loopLevel)
				continue;
			// If it refers to this DDDG itself, also ignore
			if(it.loopLevel == loopLevel && datapathType == it.datapathType)
				continue;
			// Also ignore BETWEEN DDDGs
			if(DatapathType::NON_PERFECT_BETWEEN == it.datapathType)
				continue;

			if(ddrBanking) {
				// No reads for the same array
				if(it.arraysLoaded.count(arrayName)) {
					reporter.warnOutBurstFailed(arrayName, Reporter::WARN_OUTBURST_READS_SAME_ARRAY, it.loopLevel, it.datapathType);
					return false;
				}
			}
			else {
				// No reads at all
				if(it.arraysLoaded.size()) {
					reporter.warnOutBurstFailed(arrayName, Reporter::WARN_OUTBURST_READS, it.loopLevel, it.datapathType);
					return false;
				}
			}

			// No writes for the same array
			if(it.arraysStored.count(arrayName)) {
				reporter.warnOutBurstFailed(arrayName, Reporter::WARN_OUTBURST_WRITES_SAME_ARRAY, it.loopLevel, it.datapathType);
				return false;
			}
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
			if(DatapathType::NON_PERFECT_BETWEEN == it.datapathType)
				continue;

			// No reads for the same array
			if(it.arraysLoaded.count(arrayName)) {
				reporter.warnOutBurstFailed(arrayName, Reporter::WARN_OUTBURST_READS_SAME_ARRAY, it.loopLevel, it.datapathType);
				return false;
			}

			// No writes for the same array
			if(it.arraysStored.count(arrayName)) {
				reporter.warnOutBurstFailed(arrayName, Reporter::WARN_OUTBURST_WRITES_SAME_ARRAY, it.loopLevel, it.datapathType);
				return false;
			}
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
			if(DatapathType::NON_PERFECT_BETWEEN == it.datapathType)
				continue;

			if(ddrBanking) {
				// No writes for the same array
				if(it.arraysStored.count(arrayName)) {
					reporter.warnOutBurstFailed(arrayName, Reporter::WARN_OUTBURST_WRITES_SAME_ARRAY, it.loopLevel, it.datapathType);
					return false;
				}
			}
			else {
				// No writes at all
				if(it.arraysStored.size()) {
					reporter.warnOutBurstFailed(arrayName, Reporter::WARN_OUTBURST_WRITES, it.loopLevel, it.datapathType);
					return false;
				}
			}

			// No reads for the same array
			if(it.arraysLoaded.count(arrayName)) {
				reporter.warnOutBurstFailed(arrayName, Reporter::WARN_OUTBURST_READS_SAME_ARRAY, it.loopLevel, it.datapathType);
				return false;
			}
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
			if(DatapathType::NON_PERFECT_BETWEEN == it.datapathType)
				continue;

			// No writes for the same array
			if(it.arraysStored.count(arrayName)) {
				reporter.warnOutBurstFailed(arrayName, Reporter::WARN_OUTBURST_WRITES_SAME_ARRAY, it.loopLevel, it.datapathType);
				return false;
			}

			// No reads for the same array
			if(it.arraysLoaded.count(arrayName)) {
				reporter.warnOutBurstFailed(arrayName, Reporter::WARN_OUTBURST_READS_SAME_ARRAY, it.loopLevel, it.datapathType);
				return false;
			}
		}
	}
	else {
		assert(false && "Invalid DDR scheduling policy selected (this assert should never execute)");
	}

	return true;
}


void XilinxZCUMemoryModel::findInBursts(
		std::unordered_map<unsigned, std::pair<std::string, uint64_t>> &foundNodes,
		std::vector<unsigned> &behavedNodes,
		std::unordered_map<unsigned, burstInfoTy> &burstedNodes,
		std::function<bool(unsigned, unsigned)> comparator
) {
	// If burst aggregation is enabled, then we search for potential bursts inside this DDDG
	if(args.fBurstAggr) {
		// Ignore if behavedNodes is empty
		if(0 == behavedNodes.size())
			return;

		// Lina supports burst mix, which means that it can form bursts when there are mixed-array contiguous transactions
		// With DDR banking, burst mix is always disabled
		// XXX BURST MIX IS IMPLEMENTED, BUT NOT SUPPORTED ANYMORE
		bool shouldMix = false;
		//if(args.fBurstMix && !ddrBanking)
		//	shouldMix = true;

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
			//std::string currArrayName = shouldMix? "" : foundNodes[currRootNode].first;
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
					if(shouldRpt && currOffset) reporter.infoInBurstFound(it.first, currOffset, currNodes);

					// Reset variables
					currRootNode = node;
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
			if(shouldRpt && currOffset) reporter.infoInBurstFound(it.first, currOffset, currNodes);
		}
	}
	// Else, we just add all nodes as "single bursts"
	else {
		for(auto &behavedNode : behavedNodes) {
#ifdef VAR_WSIZE
			burstedNodes[behavedNode] = burstInfoTy(foundNodes[behavedNode].second, 0, 4, std::vector<unsigned>{behavedNode});
#else
			burstedNodes[behavedNode] = burstInfoTy(foundNodes[behavedNode].second, 0, std::vector<unsigned>{behavedNode});
#endif
		}
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
	unsigned adjustFactor = (DatapathType::NORMAL_LOOP == datapath->getDatapathType())?
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
				//std::pair<std::string, std::string> wholeLoopNameInstNamePair = std::make_pair(wholeLoopName, instIDList[it2]);

				// Check if all instances of this instruction are well behaved in the memory trace
				// XXX: We do not sort the list here like when searching for inner bursts, since we do not support loop reordering
				// XXX: Memory trace map is generated with no optimisations. This means that an instruction that is originally
				// located at the i-th loop level, if the loop level is fully unrolled, it will not be located in i-th anymore
				// but in i-1-th. In this case wholeLoopName will be inconsistent with the instruction.
				// Below is a quick fix that should work as long that EVERY instruction in LLVM IR has a different ID regardless
				// of loop level (which we hope that --instnamer guarantees). Perhaps we should find a better way of doing this,
				// maybe not even here!
				std::string loopName = datapath->getTargetLoopName();
				unsigned numLevels = LpName2numLevelMap.at(loopName);
				memoryTraceMapTy::iterator found2;
				for(unsigned i = datapath->getTargetLoopLevel(); i <= numLevels; i++) {
					std::pair<std::string, std::string> wholeLoopNameInstNamePair = std::make_pair(appendDepthToLoopName(loopName, i), instIDList[it2]);
					found2 = memoryTraceMap.find(wholeLoopNameInstNamePair);
					if(found2 != memoryTraceMap.end())
						break;
				}
				assert(found2 != memoryTraceMap.end() && "Could not find the respective loop level of the instruction in memory trace map");
				std::vector<uint64_t> addresses = found2->second;
				//std::vector<uint64_t> addresses = memoryTraceMap.at(wholeLoopNameInstNamePair);
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

	return canOutBurst;
}

void XilinxZCUMemoryModel::packBursts(
	std::unordered_map<unsigned, burstInfoTy> &burstedNodes,
	std::unordered_map<unsigned, std::pair<std::string, uint64_t>> &nodes,
	int silentOpcode, bool nonSilentLast
) {
	// XXX Considering words of 32-bit here as always!

	std::unordered_map<arrayPackSzPairTy, std::pair<std::unordered_map<unsigned, std::pair<unsigned, unsigned>>, std::unordered_map<unsigned, std::pair<unsigned, unsigned>>>, boost::hash<arrayPackSzPairTy>> alignmentsPerArray;

	// If silentOpcode == -1, it means that we want to test different bus widths supported by OpenCL under our available budget
	// OpenCL accepts vector types of size 2, 4, 8 and 16 (words, not bytes!)
	if(-1 == silentOpcode) {
		for(auto &burst : burstedNodes) {
			for(unsigned sz = 16; sz - 1; sz >>= 1) {
				unsigned szInBytes = sz * 4;

				// Ignore if current setting does not fit in budget
				if(szInBytes > DDR_DATA_BUS_WIDTH)
					continue;

				// XXX We are assuming here that all arrays are word-aligned respective to the candidate vector sizes!
				// With banking this is easy since all arrays start at 0.
				// Without banking this can be easily solved by adding a padding at the array. In this case we would have to re-calculate all addresses
				// however if we assume that no arrays overlap (WHICH WE DO), we don't need to recalculate anything. We "virtually" assume that all arrays
				// are aligned.

				// Calculate left alignment: how many words it is required before the first transaction to round up a vector word
				unsigned leftAlignment = ((nodes.at(burst.first).second - memoryTraceList.at(burst.first).first) / 4) % sz;
				// Calculate right alignment: how many words it is required after the last transaction to round up a vector word
				unsigned rightAlignment = sz - ((leftAlignment + burst.second.offset + 1) % sz);
				// Avoiding one more % operation with a simple if
				if(sz == rightAlignment)
					rightAlignment = 0;

				// Save information
				if(LLVM_IR_DDRRead == microops.at(burst.first))
					alignmentsPerArray[std::make_pair(nodes.at(burst.first).first, sz)].first[burst.first] = std::make_pair(leftAlignment, rightAlignment);
				else if(LLVM_IR_DDRWrite == microops.at(burst.first))
					alignmentsPerArray[std::make_pair(nodes.at(burst.first).first, sz)].second[burst.first] = std::make_pair(leftAlignment, rightAlignment);
				else
					assert(false && "DDR node found with invalid opcode (neither load nor store)");
			}
		}

		// Save all info to global pack info
		// XXX Here we assume that --f-vec only works when only one loop nest is analysed
		for(auto &it : alignmentsPerArray)
			globalPackInfo[it.first].push_back(packInfoTy(datapath->getTargetLoopLevel(), datapath->getDatapathType(), it.second.first, it.second.second));
	}
	else {
		for(auto &burst : burstedNodes) {
			std::string arrayName = nodes.at(burst.first).first;
			std::vector<unsigned> participants = burst.second.participants;
			unsigned participantsSz = participants.size();
			unsigned packSize = packSizes.at(arrayName);

			// Currently we only silence nodes when pack is perfectly aligned (0 left alignment, 0 right alignment)
			// This was already decided at preprocess().
			// The only case where alignment != 0 is allowed is when the transaction (load only) has only one participant
			// which means that it doesn't need to be silenced
			if(participantsSz != 1) {
				// If nonSilentLast is true, this will pack as SOp>SOp>SOp>Op>...
				// otherwise: Op>SOp>SOp>SOp>...0
				int busBudget = nonSilentLast? (packSize - 1) : 0;
				uint64_t lastVisitedBaseAddress;

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
						busBudget--;

						// If bus budget is over, we start a new read transaction and reset
						if(busBudget < 0)
							busBudget = packSize - 1;
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
			}
		}
	}
}

XilinxZCUMemoryModel::XilinxZCUMemoryModel(BaseDatapath *datapath) : MemoryModel(datapath) {
	importedReadReqs.clear();
	importedWriteReqs.clear();
	importedWriteResps.clear();
	lastWriteAllocated = -1;
}

void XilinxZCUMemoryModel::analyseAndTransform() {
	// This will normally execute at setUp(), but if mma mode is OFF or GEN, it will not.
	// So we execute it here. If setUp() already ran it, this execution will be ignored
	preprocess(datapath->getTargetLoopName(), CM);

	if(shouldRpt) {
		if(reporter.isOpen())
			reporter.close();
		reporter.reopen(datapath->getTargetLoopLevel(), datapath->getDatapathType(), &PC, &microops);
		reporter.currentHeader();
	}

	std::unordered_map<std::string, outBurstInfoTy> loadOutBurstsFound;
	std::unordered_map<std::string, outBurstInfoTy> storeOutBurstsFound;

	// The memory trace map can be generated in two ways:
	// - Running Lina with "--mem-trace" and any other mode than "--mode=estimation"
	// - After running Lina once with the aforementioned configuration, the file "mem_trace.txt" will be available and can be used

	// If memory trace map was not constructed yet, try to generate it from "mem_trace.txt"
	if(!memoryTraceGenerated) {
		if(args.shortMemTrace) {
			std::string traceShortFileName = args.workDir + FILE_MEM_TRACE_SHORT;
			std::ifstream traceShortFile;
			std::string bufferedWholeLoopName = "";

			traceShortFile.open(traceShortFileName, std::ios::binary);
			assert(traceShortFile.is_open() && "No short memory trace found. Please run Lina with \"--short-mem-trace\" or \"--mem-trace\" (short mem trace is recommended) flag (leave it enabled) and any mode other than \"--mode=estimation\" (only once is needed) to generate it; or deactivate inter-iteration burst analysis with \"--fno-mmaburst\"");

			while(!(traceShortFile.eof())) {
				size_t bufferSz;
				char buffer[BUFF_STR_SZ];
				std::vector<uint64_t> addrVec;
				size_t addrVecSize;

				if(!(traceShortFile.read((char *) &bufferSz, sizeof(size_t))))
					break;
				assert(bufferSz < BUFF_STR_SZ && "String buffer not big enough to allocate key read from memory trace binary file. Please change BUFF_STR_SZ");
				traceShortFile.read(buffer, bufferSz);
				buffer[bufferSz] = '\0';
				bufferedWholeLoopName.assign(buffer);

				if(!(traceShortFile.read((char *) &bufferSz, sizeof(size_t))))
					break;
				assert(bufferSz < BUFF_STR_SZ && "String buffer not big enough to allocate key read from memory trace binary file. Please change BUFF_STR_SZ");
				traceShortFile.read(buffer, bufferSz);
				buffer[bufferSz] = '\0';

				traceShortFile.read((char *) &addrVecSize, sizeof(size_t));
				addrVec.resize(addrVecSize);

				std::pair<std::string, std::string> wholeLoopNameInstNamePair = std::make_pair(std::string(bufferedWholeLoopName), std::string(buffer));
				// XXX THIS DOES NOT SEEM TO BE A GOOD IDEA (but works...)
				traceShortFile.read((char *) addrVec.data(), addrVecSize * sizeof(uint64_t));
				memoryTraceMap.insert(std::make_pair(wholeLoopNameInstNamePair, addrVec));
			}

			traceShortFile.close();
		}
		else {
			std::string line;
			std::string traceFileName = args.workDir + FILE_MEM_TRACE;
			std::ifstream traceFile;

			traceFile.open(traceFileName);
			assert(traceFile.is_open() && "No memory trace found. Please run Lina with \"--short-mem-trace\" or \"--mem-trace\" (short mem trace is recommended) flag (leave it enabled) and any mode other than \"--mode=estimation\" (only once is needed) to generate it; or deactivate inter-iteration burst analysis with \"--fno-mmaburst\"");

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
		}

		memoryTraceGenerated = true;
	}

	const ConfigurationManager::arrayInfoCfgMapTy arrayInfoCfgMap = CM.getArrayInfoCfgMap();

	std::vector<std::string> foundArrays;

	// Change all load/stores marked as offchip to DDR read/writes (and mark their locations)
	VertexIterator vi, viEnd;
	for(std::tie(vi, viEnd) = vertices(graph); vi != viEnd; vi++) {
		Vertex currNode = *vi;
		unsigned nodeID = vertexToName[currNode];
		int nodeMicroop = microops.at(nodeID);
		std::string arrayName = baseAddress[nodeID].first;

		if(!isMemoryOp(nodeMicroop))
			continue;

		// Only consider offchip arrays
		if(arrayInfoCfgMap.at(arrayName).type != ConfigurationManager::arrayInfoCfgTy::ARRAY_TYPE_OFFCHIP)
			continue;

		if(isLoadOp(nodeMicroop)) {
			microops.at(nodeID) = LLVM_IR_DDRRead;
			loadNodes[nodeID] = std::make_pair(arrayName, memoryTraceList.at(nodeID).first);
		}

		if(isStoreOp(nodeMicroop)) {
			microops.at(nodeID) = LLVM_IR_DDRWrite;
			storeNodes[nodeID] = std::make_pair(arrayName, memoryTraceList.at(nodeID).first);
		}

		if(shouldRpt) {
			if(1 == std::count(foundArrays.begin(), foundArrays.end(), arrayName))
				reporter.warnOutBurstNotPossible(arrayName);
			foundArrays.push_back(arrayName);
		}
	}

	if(shouldRpt && loadNodes.size() && storeNodes.size() && datapath->getTargetLoopUnrollFactor() > 1)
		reporter.warnReadAfterWriteDueUnroll();

	// TODO use loadDepMap or similar here to filter out loads that are not behaved (i.e. DDR loads that depends on other DDR ops)
	std::vector<unsigned> behavedLoads;
	for(auto const &loadNode : loadNodes)
		behavedLoads.push_back(loadNode.first);
	// Sort the behaved nodes in terms of address
	auto loadComparator = [this](unsigned a, unsigned b) {
		return this->loadNodes[a] < this->loadNodes[b];
	};
	findInBursts(loadNodes, behavedLoads, burstedLoads, loadComparator);

	// TODO use storeDepMap or similar here to filter out loads that are not behaved (i.e. DDR stores that depends on other DDR ops)
	std::vector<unsigned> behavedStores;
	for(auto const &storeNode : storeNodes)
		behavedStores.push_back(storeNode.first);
	// Sort the behaved nodes in terms of address
	auto storeComparator = [this](unsigned a, unsigned b) {
		return this->storeNodes[a] < this->storeNodes[b];
	};
	findInBursts(storeNodes, behavedStores, burstedStores, storeComparator);

	// XXX: Out-burst analysis assumes that no arrays overlap in memory space!
	// This is because for our analysis like it is explained at the beginning of tryAllocate(), we need the
	// region that is read/written to analyse overlap. Within a single DDDG, this information is available.
	// For out-bursts, we would have to know the entire region considering the whole loop execution. This
	// information we don't have now and it is not calculated.
	// - For local out-burst analysis, we can still take into account this information as it is available
	// - For global out-burst analysis, we would need to calculate the region read not only by a single DDDG, but all
	//   the iteration instances of it
	// For now we are considering the worst-case scenario:
	// - If arrays overlap, the worst-case scenario would be the whole memory (would block 99.9% of out-bursts)
	// - If arrays don't overlap, the worst-case scenario is the total array size (as it is guaranteed that arrays will
	//   never overlap)

	assert((!args.fBurstMix || ddrBanking) && "Currently burst mix is not supported when iteration burst analysis is active");

	const std::vector<std::string> &instIDList = PC.getInstIDList();

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

	// Analyse burst packing
	if(args.fVec) {
		// If mode is GEN, we analyse packing possibilities
		if(ArgPack::MMA_MODE_GEN == args.mmaMode) {
			packBursts(burstedLoads, loadNodes);
			packBursts(burstedStores, storeNodes);
		}
		// If mode is USE, we use the performed analysis
		else if(ArgPack::MMA_MODE_USE == args.mmaMode) {
			packBursts(burstedLoads, loadNodes, LLVM_IR_DDRSilentRead);
			packBursts(burstedStores, storeNodes, LLVM_IR_DDRSilentWrite, true);
		}
	}

	// Add the relevant DDDG nodes for offchip load
	for(auto &burst : burstedLoads) {
		std::string arrayName = loadNodes.at(burst.first).first;

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
		std::string arrayName = storeNodes.at(burst.first).first;

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
				falseDeps.insert(std::make_pair(it.first, newNode.ID));
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
			falseDeps.insert(std::make_pair(lastLoad, newNode.ID));
		}

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
					falseDeps.insert(std::make_pair(it.first, newNode.ID));
				}
			}

			// Also, connect to the last node of DDR read transactions
			for(auto &it : burstedLoads) {
				// If DDR banking is active, we only create the edge if this node shares the same memory port as the imported one
				if(ddrBanking && (loadNodes.at(it.first).first != genFromImpStoreNodes.at(burst.first).first))
					continue;

				unsigned lastLoad = it.second.participants.back();
				// XXX: Does the edge weight matter here?
				edgesToAdd.push_back({lastLoad, newNode.ID, 0});
				falseDeps.insert(std::make_pair(lastLoad, newNode.ID));
			}
		}

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
					falseDeps.insert(std::make_pair(newNode.ID, it.first));
				}
				if(LLVM_IR_DDRWriteReq == opcode) {
					// If DDR banking is active, we only create the edge if this node shares the same memory port as the imported one
					if(ddrBanking && (storeNodes.at(it.second).first != genFromImpStoreNodes.at(burst.first).first))
						continue;

					// XXX: Does the edge weight matter here?
					edgesToAdd.push_back({newNode.ID, it.first, 0});
					falseDeps.insert(std::make_pair(newNode.ID, it.first));
				}
			}
		}

		// Update DDDG
		datapath->updateAddDDDGEdges(edgesToAdd);
	}

	// Wrap-up run. On cases for BETWEEN DDDGs, we might have imported nodes from both sides. We have to
	// ensure the order here as well. Here is simple: this logic is reciprocate, so if we guarantee that A comes after B,
	// for sure B will come before A (sounds stupid but hey). Only LLVM_IR_DDRWriteResp comes from the DDDG from that
	// direction. So we only check for this type of node against all the rest
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
					falseDeps.insert(std::make_pair(respID, it.first));
				}
				if(LLVM_IR_DDRWriteReq == opcode) {
					// If DDR banking is active, we only create the edge if this node shares the same memory port as the imported one
					if(ddrBanking && (genFromImpStoreNodes.at(it.second).first != genFromImpStoreNodes.at(burst.first).first))
						continue;

					// XXX: Does the edge weight matter here?
					edgesToAdd.push_back({respID, it.first, 0});
					falseDeps.insert(std::make_pair(respID, it.first));
				}
			}
		}
	}

	// Update DDDG
	datapath->updateAddDDDGEdges(edgesToAdd);

	// After finished updating DDDG, we import all the data to the normal structures of MemoryModel
	burstedLoads.insert(importedLoads.begin(), importedLoads.end());
	burstedStores.insert(importedStores.begin(), importedStores.end());
	ddrNodesToRootLS.insert(ddrNodesToRootLSTmp.begin(), ddrNodesToRootLSTmp.end());
	loadNodes.insert(genFromImpLoadNodes.begin(), genFromImpLoadNodes.end());
	storeNodes.insert(genFromImpStoreNodes.begin(), genFromImpStoreNodes.end());

	datapath->refreshDDDG();

	// Create a dummy last node and connect the DDDG leaves and the imported nodes to it,
	// so that the imported nodes (if any) won't stay disconnected
	if(importedWriteResps.size() || importedReadReqs.size() || importedWriteReqs.size()) {
		datapath->createDummySink();
		datapath->refreshDDDG();
	}

	// Save info to global vars
	if(ArgPack::MMA_MODE_GEN == args.mmaMode) {
		unsigned loopLevel = datapath->getTargetLoopLevel();
		unsigned datapathType = datapath->getDatapathType();

		std::set<std::string> arrayNamesLoaded;
		std::set<std::string> arrayNamesStored;

		for(auto &it : loadNodes)
			arrayNamesLoaded.insert(it.second.first);
		for(auto &it : storeNodes)
			arrayNamesStored.insert(it.second.first);

		globalDDRMap[datapath->getTargetLoopName()].push_back(ddrInfoTy(loopLevel, datapathType, arrayNamesLoaded, arrayNamesStored));

		globalOutBurstsInfo[datapath->getTargetLoopName()].push_back(globalOutBurstsInfoTy(loopLevel, datapathType, loadOutBurstsFoundCached, storeOutBurstsFoundCached));
	}

#ifdef DBG_PRINT_ALL
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
			// Only non-silent!
			if(shouldRpt && LLVM_IR_DDRRead == opcode && lastWriteAllocated != -1)
				reporter.warnReadAfterWrite(loadNodes.at(node).first, node, storeNodes.at(lastWriteAllocated).first, lastWriteAllocated);

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
		// Only non-silent!
		if(commit && LLVM_IR_DDRWrite == opcode) lastWriteAllocated = node;
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

#if 1
unsigned XilinxZCUMemoryModel::getCoalescedReadsFrom(unsigned nodeID) {
	std::unordered_map<unsigned, burstInfoTy>::iterator found = burstedLoads.find(nodeID);

	if(burstedLoads.end() == found)
		return 0;

	// Offset + 1 determines the size of the burst, but does also consider the silent read/writes! So we subtract them
	// We could simply count how many read/writes are not silent and that's it, however redundant nodes are not being
	// silenced for now and that could affect the count, thus we do this way instead
	unsigned notSilentOnes = 0;
	for(unsigned nodeID : found->second.participants) {
		if(LLVM_IR_DDRRead == microops.at(nodeID))
			notSilentOnes++;
	}

	return (notSilentOnes < (found->second.offset + 1))? notSilentOnes : (found->second.offset + 1);
}

unsigned XilinxZCUMemoryModel::getCoalescedWritesFrom(unsigned nodeID) {
	std::unordered_map<unsigned, burstInfoTy>::iterator found = burstedStores.find(nodeID);

	if(burstedStores.end() == found)
		return 0;

	// Offset + 1 determines the size of the burst, but does also consider the silent read/writes! So we subtract them
	// We could simply count how many read/writes are not silent and that's it, however redundant nodes are not being
	// silenced for now and that could affect the count, thus we do this way instead
	unsigned notSilentOnes = 0;
	for(unsigned nodeID : found->second.participants) {
		if(LLVM_IR_DDRWrite == microops.at(nodeID))
			notSilentOnes++;
	}

	return (notSilentOnes < (found->second.offset + 1))? notSilentOnes : (found->second.offset + 1);
}
#endif

void XilinxZCUMemoryModel::setUp(ContextManager &CtxM) {
	std::string loopName = datapath->getTargetLoopName();
	unsigned loopLevel = datapath->getTargetLoopLevel();
	unsigned datapathType = datapath->getDatapathType();

	// If the memory model preprocess stage has not been executed, now it is the time.
	// The preprocess improves loop-nest-wise information. It must be ran once per loop nest
	preprocess(loopName, CM);

	bool foundIt = false;
	for(auto &it : filteredOutBurstsInfo->second) {
		if(it.loopLevel == loopLevel && it.datapathType == datapathType) {
			loadOutBurstsFoundCached = it.loadOutBurstsFound;
			storeOutBurstsFoundCached = it.storeOutBurstsFound;
			foundIt = true;
			break;
		}
	}
	assert(foundIt && "Out-burst info for selected DDDG not found");

	importedFromContext = true;
}

void XilinxZCUMemoryModel::save(ContextManager &CtxM) {
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

void XilinxZCUMemoryModel::inheritLoadDepMap(unsigned targetID, unsigned sourceID) {
	loadDepMap[targetID].insert(loadDepMap[sourceID].begin(), loadDepMap[sourceID].end());
}

void XilinxZCUMemoryModel::inheritStoreDepMap(unsigned targetID, unsigned sourceID) {
	storeDepMap[targetID].insert(storeDepMap[sourceID].begin(), storeDepMap[sourceID].end());
}

void XilinxZCUMemoryModel::addToLoadDepMap(unsigned targetID, unsigned toAddID) {
	// Recall that some edges are inserted in DDDG to maintain transactions ordering, but
	// they do not configure a data dependency itself. These edges should be ignored
	if(!(falseDeps.count(std::make_pair(toAddID, targetID))))
		loadDepMap[targetID].insert(toAddID);
}

void XilinxZCUMemoryModel::addToStoreDepMap(unsigned targetID, unsigned toAddID) {
	if(!(falseDeps.count(std::make_pair(toAddID, targetID))))
		storeDepMap[targetID].insert(toAddID);
}

std::pair<std::string, uint64_t> XilinxZCUMemoryModel::calculateResIIMemRec(std::vector<uint64_t> rcScheduledTime) {
	std::set<unsigned> visited;
	std::set<unsigned> toVisit;
	std::function<void(const unsigned &, std::set<unsigned> &, std::unordered_map<unsigned, std::set<unsigned>> &)> recursiveBlock;
	recursiveBlock = [&recursiveBlock, &visited](const unsigned &v, std::set<unsigned> &connected, std::unordered_map<unsigned, std::set<unsigned>> &depMap) {
		visited.insert(v);
		connected.insert(v);

		for(auto &it : depMap.at(v)) {
			if(!(visited.count(it)))
				recursiveBlock(it, connected, depMap);
		}
	};
	// Using this struct instead of a normal pair in the map customises the default initialiser
	struct minMaxPair {
		std::pair<uint64_t, uint64_t> value = std::make_pair(std::numeric_limits<uint64_t>::max(), 0);
		uint64_t distance() const { return value.second - value.first; }
	};
	std::vector<std::pair<std::string, uint64_t>> loadMaxs, storeMaxs;
	std::unordered_map<std::string, uint64_t> connectedLoadGraphs, connectedStoreGraphs;

	// Logic for loads

	// Consider only loads (and make them symmetric to facilitate calculation)
	for(auto &it : loadDepMap) {
		if(microops.at(it.first) != LLVM_IR_DDRReadReq)
			continue;

		toVisit.insert(it.first);

		for(auto &it2 : it.second)
			loadDepMap[it2].insert(it.first);
	}

	for(auto &it : toVisit) {
		if(!(visited.count(it))) {
			std::set<unsigned> connected;
			recursiveBlock(it, connected, loadDepMap);
			std::set<std::string> consideredInterfaces;

			// Find smallest and largest allocation value for each memory interface
			// (if banking is disabled, all global loads share the same interface)
			std::unordered_map<std::string, minMaxPair> minMaxPerInterface;
			for(auto &it2 : connected) {
				std::string arrayName = ddrBanking? loadNodes.at(ddrNodesToRootLS.at(it2)).first : "gmemloadifc";
				if(rcScheduledTime[it2] < minMaxPerInterface[arrayName].value.first)
					minMaxPerInterface[arrayName].value.first = rcScheduledTime[it2];
				if(rcScheduledTime[it2] > minMaxPerInterface[arrayName].value.second)
					minMaxPerInterface[arrayName].value.second = rcScheduledTime[it2];

				if(!(consideredInterfaces.count(arrayName))) {
					consideredInterfaces.insert(arrayName);
					(connectedLoadGraphs[arrayName])++;
				}
			}

			// Save all distances to the load max vector
			for(auto &it2 : minMaxPerInterface)
				loadMaxs.push_back(std::make_pair(it2.first, it2.second.distance()));
		}
	}

	// Logic for stores

	visited.clear();
	toVisit.clear();

	// Consider only stores (and make them symmetric to facilitate calculation)
	for(auto &it : storeDepMap) {
		if(microops.at(it.first) != LLVM_IR_DDRWriteReq)
			continue;

		toVisit.insert(it.first);

		for(auto &it2 : it.second)
			storeDepMap[it2].insert(it.first);
	}

	for(auto &it : toVisit) {
		if(!(visited.count(it))) {
			std::set<unsigned> connected;
			recursiveBlock(it, connected, storeDepMap);
			std::set<std::string> consideredInterfaces;

			// Find smallest and largest allocation value for each memory interface
			// (if banking is disabled, all global stores share the same interface)
			std::unordered_map<std::string, minMaxPair> minMaxPerInterface;
			for(auto &it2 : connected) {
				std::string arrayName = ddrBanking? storeNodes.at(ddrNodesToRootLS.at(it2)).first : "gmemstoreifc";
				if(rcScheduledTime[it2] < minMaxPerInterface[arrayName].value.first)
					minMaxPerInterface[arrayName].value.first = rcScheduledTime[it2];
				if(rcScheduledTime[it2] > minMaxPerInterface[arrayName].value.second)
					minMaxPerInterface[arrayName].value.second = rcScheduledTime[it2];

				if(!(consideredInterfaces.count(arrayName))) {
					consideredInterfaces.insert(arrayName);
					(connectedStoreGraphs[arrayName])++;
				}
			}

			// Save all distances to the store max vector
			for(auto &it2 : minMaxPerInterface)
				storeMaxs.push_back(std::make_pair(it2.first, it2.second.distance()));
		}
	}

	// There is a key difference between Lina and Vivado regarding allocation of load/stores.
	// While Lina always allocate no before than ALAP, Vivado create some "allocation pits"
	// and attempt to concentrate all loads around these pits. The same applies for stores.
	// Vivado does even input some bubbles to move these load/stores around.
	// The intention is to reduce the distance among dependent loads and therefore reduce the
	// II constraint that is being calculated here. Lina does not perform this type of allocation
	// and problems might arise, for example when two dependence graphs can be allocated at the
	// same window and have the same distance. Naturally they cannot be allocated to the same
	// cycle as it violates port usage. In this case we approximate the correct behaviour by
	// adding the amount of dependence graphs that we found, guaranteeing that the II here
	// calculated will still respect interface usage

	auto prioritiseLoadLargerCompensatedDistance = [&connectedLoadGraphs](const std::pair<std::string, uint64_t> &a, const std::pair<std::string, uint64_t> &b) {
		return (a.second + connectedLoadGraphs.at(a.first)) < (b.second + connectedLoadGraphs.at(b.first));
	};
	std::vector<std::pair<std::string, uint64_t>>::iterator loadMaxIt = std::max_element(loadMaxs.begin(), loadMaxs.end(), prioritiseLoadLargerCompensatedDistance);
	std::pair<std::string, uint64_t> loadMax = (loadMaxs.size() && loadMaxIt->second > 1)?
		std::make_pair(loadMaxIt->first, loadMaxIt->second + connectedLoadGraphs.at(loadMaxIt->first)) : std::make_pair("none", 1);

	auto prioritiseStoreLargerCompensatedDistance = [&connectedStoreGraphs](const std::pair<std::string, uint64_t> &a, const std::pair<std::string, uint64_t> &b) {
		return (a.second + connectedStoreGraphs.at(a.first)) < (b.second + connectedStoreGraphs.at(b.first));
	};
	std::vector<std::pair<std::string, uint64_t>>::iterator storeMaxIt = std::max_element(storeMaxs.begin(), storeMaxs.end(), prioritiseStoreLargerCompensatedDistance);
	std::pair<std::string, uint64_t> storeMax = (storeMaxs.size() && storeMaxIt->second > 1)?
		std::make_pair(storeMaxIt->first, storeMaxIt->second + connectedStoreGraphs.at(storeMaxIt->first)) : std::make_pair("none", 1);

	// At last choose the largest
	return (loadMax.second > storeMax.second)? loadMax : storeMax;
}

#ifdef DBG_PRINT_ALL
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
