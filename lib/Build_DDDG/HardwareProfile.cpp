#include "profile_h/HardwareProfile.h"

#include "profile_h/opcodes.h"

HardwareProfile::HardwareProfile() {
	fAddCount = 0;
	fSubCount = 0;
	fMulCount = 0;
	fDivCount = 0;

	unrFAddCount = 0;
	unrFSubCount = 0;
	unrFMulCount = 0;
	unrFDivCount = 0;

	fAddInUse = 0;
	fSubInUse = 0;
	fMulInUse = 0;
	fDivInUse = 0;

	fAddThreshold = 0;
	fSubThreshold = 0;
	fMulThreshold = 0;
	fDivThreshold = 0;

	isConstrained = false;
	thresholdSet = false;
}

HardwareProfile *HardwareProfile::createInstance() {
	// XXX: Right now only Xilinx boards are supported, therefore this is the only constructor available for now
	switch(args.target) {
		case ArgPack::TARGET_XILINX_VC707:
			assert(args.fNoTCS && "Time-constrained scheduling is currently not supported with the selected platform. Please activate the \"--fno-tcs\" flag");
			return new XilinxVC707HardwareProfile();
		case ArgPack::TARGET_XILINX_ZCU102:
			return new XilinxZCU102HardwareProfile();
		case ArgPack::TARGET_XILINX_ZC702:
		default:
			assert(args.fNoTCS && "Time-constrained scheduling is currently not supported with the selected platform. Please activate the \"--fno-tcs\" flag");
			return new XilinxZC702HardwareProfile();
	}
}

void HardwareProfile::clear() {
	arrayNameToNumOfPartitions.clear();
	fAddCount = 0;
	fSubCount = 0;
	fMulCount = 0;
	fDivCount = 0;
}

void HardwareProfile::constrainHardware(
	const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
	const ConfigurationManager::partitionCfgMapTy &partitionCfgMap,
	const ConfigurationManager::partitionCfgMapTy &completePartitionCfgMap
) {
	isConstrained = true;

	fAddInUse = 0;
	fSubInUse = 0;
	fMulInUse = 0;
	fDivInUse = 0;

	arrayNameToConfig.clear();
	arrayNameToWritePortsPerPartition.clear();
	arrayNameToEfficiency.clear();
	arrayPartitionToReadPorts.clear();
	arrayPartitionToReadPortsInUse.clear();
	arrayPartitionToWritePorts.clear();
	arrayPartitionToWritePortsInUse.clear();

	limitedBy.clear();
	fAddThreshold = INFINITE_RESOURCES;
	fSubThreshold = INFINITE_RESOURCES;
	fMulThreshold = INFINITE_RESOURCES;
	fDivThreshold = INFINITE_RESOURCES;
	if(!(args.fNoFPUThresOpt)) {
		thresholdSet = true;
		setThresholdWithCurrentUsage();
	}

	unrFAddCount = fAddCount;
	unrFSubCount = fSubCount;
	unrFMulCount = fMulCount;
	unrFDivCount = fDivCount;

	clear();

	setMemoryCurrentUsage(arrayInfoCfgMap, partitionCfgMap, completePartitionCfgMap);
}

std::tuple<std::string, uint64_t> HardwareProfile::calculateResIIOp() {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	uint64_t resIIOp = 0;
	std::string resIIOpName = "none";

	if(fAddCount) {
		resIIOp = std::ceil(unrFAddCount / (float) fAddCount);
		resIIOpName = "fadd";
	}

	if(fSubCount) {
		uint64_t resIIOpCandidate = std::ceil(unrFSubCount / (float) fSubCount);
		if(resIIOpCandidate > resIIOp) {
			resIIOp = resIIOpCandidate;
			resIIOpName = "fsub";
		}
	}

	if(fMulCount) {
		uint64_t resIIOpCandidate = std::ceil(unrFMulCount / (float) fMulCount);
		if(resIIOpCandidate > resIIOp) {
			resIIOp = resIIOpCandidate;
			resIIOpName = "fmul";
		}
	}

	if(fDivCount) {
		uint64_t resIIOpCandidate = std::ceil(unrFDivCount / (float) fDivCount);
		if(resIIOpCandidate > resIIOp) {
			resIIOp = resIIOpCandidate;
			resIIOpName = "fdiv";
		}
	}

	// XXX: If more resources are to be constrained, add equivalent logic here

	if(resIIOp > 1)
		return std::make_tuple(resIIOpName, resIIOp);
	else
		return std::make_tuple("none", 1);
}

unsigned HardwareProfile::arrayGetNumOfPartitions(std::string arrayName) {
	// XXX: If the arrayName doesn't exist, this access will add it automatically. Is this desired behaviour?
	return arrayNameToNumOfPartitions[arrayName];
}

unsigned HardwareProfile::arrayGetPartitionReadPorts(std::string partitionName) {
	std::map<std::string, unsigned>::iterator found = arrayPartitionToReadPorts.find(partitionName);
	assert(found != arrayPartitionToReadPorts.end() && "Array has no storage allocated for it");
	return found->second;
}

unsigned HardwareProfile::arrayGetPartitionWritePorts(std::string partitionName) {
	std::map<std::string, unsigned>::iterator found = arrayPartitionToWritePorts.find(partitionName);
	assert(found != arrayPartitionToWritePorts.end() && "Array has no storage allocated for it");
	return found->second;
}


bool HardwareProfile::fAddTryAllocate(bool commit) {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	// There are fAdd available for use, just allocate it
	if(fAddInUse < fAddCount) {
		if(commit)
			fAddInUse++;
		return true;
	}
	// All fAdd are in use, try to allocate a new unit
	else {
		if(thresholdSet && fAddCount) {
			if(fAddCount >= fAddThreshold)
				return false;
		}

		// Try to allocate a new unit
		bool success = fAddAddUnit(commit);
		// If successful, mark this unit as allocated
		if(success && commit)
			fAddInUse++;

		return success;
	}
}

bool HardwareProfile::fSubTryAllocate(bool commit) {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	// There are fSub available for use, just allocate it
	if(fSubInUse < fSubCount) {
		if(commit)
			fSubInUse++;
		return true;
	}
	// All fSub are in use, try to allocate a new unit
	else {
		if(thresholdSet && fSubCount) {
			if(fSubCount >= fSubThreshold)
				return false;
		}

		// Try to allocate a new unit
		bool success = fSubAddUnit(commit);
		// If successful, mark this unit as allocated
		if(success && commit)
			fSubInUse++;

		return success;
	}
}

bool HardwareProfile::fMulTryAllocate(bool commit) {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	// There are fMul available for use, just allocate it
	if(fMulInUse < fMulCount) {
		if(commit)
			fMulInUse++;
		return true;
	}
	// All fMul are in use, try to allocate a new unit
	else {
		if(thresholdSet && fMulCount) {
			if(fMulCount >= fMulThreshold)
				return false;
		}

		// Try to allocate a new unit
		bool success = fMulAddUnit(commit);
		// If successful, mark this unit as allocated
		if(success && commit)
			fMulInUse++;

		return success;
	}
}

bool HardwareProfile::fDivTryAllocate(bool commit) {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	// There are fDiv available for use, just allocate it
	if(fDivInUse < fDivCount) {
		if(commit)
			fDivInUse++;
		return true;
	}
	// All fDiv are in use, try to allocate a new unit
	else {
		if(thresholdSet && fDivCount) {
			if(fDivCount >= fDivThreshold)
				return false;
		}

		// Try to allocate a new unit
		bool success = fDivAddUnit(commit);
		// If successful, mark this unit as allocated
		if(success && commit)
			fDivInUse++;

		return success;
	}
}

bool HardwareProfile::fCmpTryAllocate(bool commit) {
	// XXX: For now, fCmp is not constrained
	return true;
}

bool HardwareProfile::loadTryAllocate(std::string arrayPartitionName, bool commit) {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	std::map<std::string, unsigned>::iterator found = arrayPartitionToReadPorts.find(arrayPartitionName);
	std::map<std::string, unsigned>::iterator found2 = arrayPartitionToReadPortsInUse.find(arrayPartitionName);
	//std::cout << ">>>>>> " << arrayPartitionName << std::endl;
	assert(found != arrayPartitionToReadPorts.end() && "Array has no storage allocated for it");
	assert(found2 != arrayPartitionToReadPortsInUse.end() && "Array has no storage allocated for it");

	//std::cout << "~~ ~~ loadTryAllocate start\n";
	//for(auto &it : arrayPartitionToReadPorts)
	//	std::cout << "~~ ~~ " << it.first << " | " << std::to_string(it.second) << "\n";
	//std::cout << "~~ ~~~\n";
	//for(auto &it : arrayPartitionToReadPortsInUse)
	//	std::cout << "~~ ~~ " << it.first << " | " << std::to_string(it.second) << "\n";
	//std::cout << "~~ ~~ loadTryAllocate end\n";

	// All ports are being used, not able to allocate right now
	if(found2->second >= found->second)
		return false;

	// Allocate a port
	if(commit)
		(found2->second)++;

	return true;
}

bool HardwareProfile::storeTryAllocate(std::string arrayPartitionName, bool commit) {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	std::map<std::string, unsigned>::iterator found = arrayPartitionToWritePorts.find(arrayPartitionName);
	std::map<std::string, unsigned>::iterator found2 = arrayPartitionToWritePortsInUse.find(arrayPartitionName);
	//std::cout << ">>>>>> " << arrayPartitionName << std::endl;
	assert(found != arrayPartitionToWritePorts.end() && "Array has no storage allocated for it");
	assert(found2 != arrayPartitionToWritePortsInUse.end() && "Array has no storage allocated for it");

	//std::cout << "~~ ~~ storeTryAllocate start\n";
	//for(auto &it : arrayPartitionToWritePorts)
	//	std::cout << "~~ ~~ " << it.first << " | " << std::to_string(it.second) << "\n";
	//std::cout << "~~ ~~~\n";
	//for(auto &it : arrayPartitionToWritePortsInUse)
	//	std::cout << "~~ ~~ " << it.first << " | " << std::to_string(it.second) << "\n";
	//std::cout << "~~ ~~ storeTryAllocate end\n";

	// All ports are being used
	if(found2->second >= found->second) {
		// If RW ports are enabled, attempt to allocate a new port
		if(args.fRWRWMem && found->second < arrayGetMaximumWritePortsPerPartition()) {
			if(commit) {
				(found->second)++;
				(found2->second)++;
			}

#ifdef LEGACY_SEPARATOR
			size_t tagPos = arrayPartitionName.find("-");
#else
			size_t tagPos = arrayPartitionName.find(GLOBAL_SEPARATOR);
#endif
			if(commit) {
				// TODO: euacho que isso funciona sem o if
				(arrayNameToWritePortsPerPartition[arrayPartitionName.substr(0, tagPos)])++;
			}

			return true;
		}
		// Attempt to allocate a new port failed
		else {
			return false;
		}
	}

	// Allocate a port
	(found2->second)++;

	return true;
}

bool HardwareProfile::intOpTryAllocate(unsigned opcode, bool commit) {
	// XXX: For now, int ops are not constrained
	return true;
}

bool HardwareProfile::callTryAllocate(bool commit) {
	// XXX: For now, calls are not constrained
	return true;
}

void HardwareProfile::fAddRelease() {
	assert(fAddInUse && "Attempt to release fAdd unit when none is allocated");
	fAddInUse--;
}

void HardwareProfile::fSubRelease() {
	assert(fSubInUse && "Attempt to release fSub unit when none is allocated");
	fSubInUse--;
}

void HardwareProfile::fMulRelease() {
	assert(fMulInUse && "Attempt to release fMul unit when none is allocated");
	fMulInUse--;
}

void HardwareProfile::fDivRelease() {
	assert(fDivInUse && "Attempt to release fDiv unit when none is allocated");
	fDivInUse--;
}

void HardwareProfile::fCmpRelease() {
	assert(false && "fCmp is not constrained");
}

void HardwareProfile::loadRelease(std::string arrayPartitionName) {
	//std::cout << "~~ ~~ loadRelease start\n";
	//for(auto &it : arrayPartitionToReadPorts)
	//	std::cout << "~~ ~~ " << it.first << " | " << std::to_string(it.second) << "\n";
	//std::cout << "~~ ~~~\n";
	//for(auto &it : arrayPartitionToReadPortsInUse)
	//	std::cout << "~~ ~~ " << it.first << " | " << std::to_string(it.second) << "\n";
	//std::cout << "~~ ~~ loadRelease end\n";
	std::map<std::string, unsigned>::iterator found = arrayPartitionToReadPortsInUse.find(arrayPartitionName);
	assert(found != arrayPartitionToReadPortsInUse.end() && "No array/partition found with the provided name");
	assert(found->second && "Attempt to release read port when none is allocated for this array/partition");
	(found->second)--;
}

void HardwareProfile::storeRelease(std::string arrayPartitionName) {
	//std::cout << "~~ ~~ storeRelease start\n";
	//for(auto &it : arrayPartitionToWritePorts)
	//	std::cout << "~~ ~~ " << it.first << " | " << std::to_string(it.second) << "\n";
	//std::cout << "~~ ~~~\n";
	//for(auto &it : arrayPartitionToWritePortsInUse)
	//	std::cout << "~~ ~~ " << it.first << " | " << std::to_string(it.second) << "\n";
	//std::cout << "~~ ~~ storeRelease end\n";
	std::map<std::string, unsigned>::iterator found = arrayPartitionToWritePortsInUse.find(arrayPartitionName);
	assert(found != arrayPartitionToWritePortsInUse.end() && "No array/partition found with the provided name");
	assert(found->second && "Attempt to release write port when none is allocated for this array/partition");
	(found->second)--;
}

void HardwareProfile::intOpRelease(unsigned opcode) {
	assert(false && "Integer ops are not constrained");
}

void HardwareProfile::callRelease() {
	assert(false && "Calls are not constrained");
}

XilinxHardwareProfile::XilinxHardwareProfile() {
	maxDSP = 0;
	maxFF = 0;
	maxLUT = 0;
	maxBRAM18k = 0;

	usedDSP = 0;
	usedFF = 0;
	usedLUT = 0;
	usedBRAM18k = 0;
}

void XilinxHardwareProfile::clear() {
	HardwareProfile::clear();

	arrayNameToUsedBRAM18k.clear();
	usedDSP = 0;
	usedFF = 0;
	usedLUT = 0;
	usedBRAM18k = 0;
}

unsigned XilinxHardwareProfile::getLatency(unsigned opcode) {
	switch(opcode) {
		case LLVM_IR_Shl:
		case LLVM_IR_LShr:
		case LLVM_IR_AShr:
		case LLVM_IR_And:
		case LLVM_IR_Or:
		case LLVM_IR_Xor:
		case LLVM_IR_ICmp:
		case LLVM_IR_Br:
		case LLVM_IR_IndexAdd:
		case LLVM_IR_IndexSub:
			return 0;
		case LLVM_IR_Add:
			return LATENCY_ADD;
		case LLVM_IR_Sub: 
			return LATENCY_SUB;
		case LLVM_IR_Call:
			return 0;
		case LLVM_IR_Store:
			return LATENCY_STORE;
		case LLVM_IR_SilentStore:
			return 0;
		case LLVM_IR_Load:
			// Observation from Vivado HLS: 
			//   1. When enabling partitioning without pipelining, a load operation 
			//      from BRAM needs 2 access latency
			//   2. When no partitioning, a load operation from  BRAM needs 1 access 
			//      latency
			// FIXME: In current implementation, we apply 2 load latency to all arrays 
			//        for simplicity. But to further improving accuracy, it is better
			//        to ONLY associate 2 load latency with partitioned arrays and use
			//        1 load latency for normal arrays.
			return (args.fILL)? LATENCY_LOAD : LATENCY_LOAD - 1;
		case LLVM_IR_Mul:
			// 64 bits -- 18 cycles
			// 50 bits -- 11 cycles
			// 32 bits -- 6 cycles
			// 24 bits -- 3 cycles
			// 20 bits -- 3 cycles
			// 18 bits -- 1 cycles
			// 16 bits -- 1 cycles
			// 10 bits -- 1 cycles
			// 8  bits -- 1 cycles
			// Currently, we only consider 32-bit applications
			return LATENCY_MUL32;
		case LLVM_IR_UDiv:
		case LLVM_IR_SDiv:
			// 64 bits -- 68 cycles
			// 50 bits -- 54 cycles
			// 32 bits -- 36 cycles
			// 24 bits -- 28 cycles
			// 16 bits -- 20 cycles
			// 10 bits -- 14 cycles
			// 8  bits -- 12 cycles
			// Currently, we only consider 32-bit applications
			return LATENCY_DIV32;
		case LLVM_IR_FAdd:
			return LATENCY_FADD32;
		case LLVM_IR_FSub:
			// 32/64 bits -- 5 cycles
			return LATENCY_FSUB32;
		case LLVM_IR_FMul:
			// 64 bits -- 6 cycles
			// 32 bits -- 4 cycles
			return LATENCY_FMUL32;
		case LLVM_IR_FDiv:
			// 64 bits -- 31 cycles
			// 32 bits -- 16 cycles
			return LATENCY_FDIV32;
		case LLVM_IR_FCmp:
			return LATENCY_FCMP;
		default: 
			return 0;
	}
}

unsigned XilinxHardwareProfile::getSchedulingLatency(unsigned opcode) {
	// Some nodes can have different latencies for execution and scheduling
	// Load, for example, takes 2 cycles to finish but it can be enqueued at every cycle
	if(LLVM_IR_Load == opcode)
		return SCHEDULING_LATENCY_LOAD;
	else
		return getLatency(opcode);
}

double XilinxHardwareProfile::getInCycleLatency(unsigned opcode) {
	assert(args.fNoTCS && "Time-constrained scheduling is currently not supported with the selected platform. Please activate the \"--fno-tcs\" flag");
	return 0;
}

bool XilinxHardwareProfile::isPipelined(unsigned opcode) {
	switch(opcode) {
		case LLVM_IR_FAdd:
		case LLVM_IR_FSub:
		case LLVM_IR_FMul:
		case LLVM_IR_FDiv:
		case LLVM_IR_Load:
		case LLVM_IR_Store:
			return true;
		// TODO: fCmp, integer ops and call are here but because we are not constraining those resources!
		// Perhaps if we constrain them, we shold double-check if they're pipelined or not
		default: 
			return false;
	}
}

void XilinxHardwareProfile::calculateRequiredResources(
	std::vector<int> &microops,
	const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
	std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress,
	std::map<uint64_t, std::vector<unsigned>> &maxTimesNodesMap
) {
	clear();

	// XXX: In current implementation, only floating point operations are considered
	unsigned fAddSubTotalCount = 0;
	unsigned fMulTotalCount = 0;
	unsigned fDivTotalCount = 0;

	std::map<std::string, unsigned> arrayNameToReadPorts;
	std::map<std::string, unsigned> arrayNameToWritePorts;

	for(auto &it : arrayInfoCfgMap) {
		std::string arrayName = it.first;
		arrayNameToReadPorts.insert(std::make_pair(arrayName, 0));
		arrayNameToWritePorts.insert(std::make_pair(arrayName, 0));
		arrayAddPartition(arrayName);
	}

	for(auto &it : maxTimesNodesMap) {
		unsigned fAddSubCount = 0;
		unsigned fMulCount = 0;
		unsigned fDivCount = 0;

		for(auto &it2 : arrayNameToReadPorts)
			it2.second = 0;
		for(auto &it2 : arrayNameToWritePorts)
			it2.second = 0;

		for(auto &it2 : it.second) {
			unsigned opcode = microops.at(it2);

			if(isFAddOp(opcode) || isFSubOp(opcode)) {
				fAddSubCount++;
				if(fAddSubCount > fAddSubTotalCount) {
					fAddSubTotalCount++;

					switch(opcode) {
						case LLVM_IR_FAdd:
							fAddAddUnit();
							break;
						case LLVM_IR_FSub:
							fSubAddUnit();
							break;
						default:
							assert(false && "isFAddOp() || isFSubOp() lead to invalid opcode");
					}
				}
			}

			if(isFMulOp(opcode)) {
				fMulCount++;
				if(fMulCount > fMulTotalCount) {
					fMulTotalCount++;
					fMulAddUnit();
				}
			}

			if(isFDivOp(opcode)) {
				fDivCount++;
				if(fDivCount > fDivTotalCount) {
					fDivTotalCount++;
					fDivAddUnit();
				}
			}

			if(isLoadOp(opcode)) {
				// TODO: According to original code this fails if scratchpadPartition() is called before this function
				std::string arrayName = baseAddress[it2].first;
				arrayNameToReadPorts[arrayName]++;
				if(arrayNameToReadPorts[arrayName] > XilinxHardwareProfile::PER_PARTITION_PORTS_R * arrayGetNumOfPartitions(arrayName))
					arrayAddPartition(arrayName);
			}

			if(isStoreOp(opcode)) {
				// TODO: According to original code this fails if scratchpadPartition() is called before this function
				std::string arrayName = baseAddress[it2].first;
				arrayNameToWritePorts[arrayName]++;
				if(arrayNameToWritePorts[arrayName] > XilinxHardwareProfile::PER_PARTITION_PORTS_W * arrayGetNumOfPartitions(arrayName))
					arrayAddPartition(arrayName);
			}
		}
	}
}

void XilinxHardwareProfile::setThresholdWithCurrentUsage() {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	unsigned totalDSP = fAddCount * DSP_FADD + fSubCount * DSP_FSUB + fMulCount * DSP_FMUL + fDivCount * DSP_FDIV;

	if(totalDSP > maxDSP) {
		float scale = (float) totalDSP / (float) maxDSP;
		fAddThreshold = (unsigned) std::ceil((float) fAddCount / scale);
		fSubThreshold = (unsigned) std::ceil((float) fSubCount / scale);
		fMulThreshold = (unsigned) std::ceil((float) fMulCount / scale);
		fDivThreshold = (unsigned) std::ceil((float) fDivCount / scale);

		std::vector<unsigned> scaledValues;
		scaledValues.push_back(fAddThreshold * DSP_FADD);
		scaledValues.push_back(fSubThreshold * DSP_FSUB);
		scaledValues.push_back(fMulThreshold * DSP_FMUL);
		scaledValues.push_back(fDivThreshold * DSP_FDIV);

		unsigned maxValue = *std::max_element(scaledValues.begin(), scaledValues.end());

		if(maxValue == fAddThreshold * DSP_FADD)
			limitedBy.insert(LIMITED_BY_FADD);
		if(maxValue == fSubThreshold * DSP_FSUB)
			limitedBy.insert(LIMITED_BY_FSUB);
		if(maxValue == fMulThreshold * DSP_FMUL)
			limitedBy.insert(LIMITED_BY_FMUL);
		if(maxValue == fDivThreshold * DSP_FDIV)
			limitedBy.insert(LIMITED_BY_FDIV);
	}
	else {
		fAddThreshold = fAddCount? fAddCount : INFINITE_RESOURCES;
		fSubThreshold = fSubCount? fSubCount : INFINITE_RESOURCES;
		fMulThreshold = fMulCount? fMulCount : INFINITE_RESOURCES;
		fDivThreshold = fDivCount? fDivCount : INFINITE_RESOURCES;
	}
}

void XilinxHardwareProfile::setMemoryCurrentUsage(
	const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
	const ConfigurationManager::partitionCfgMapTy &partitionCfgMap,
	const ConfigurationManager::partitionCfgMapTy &completePartitionCfgMap
) {
	const size_t size18kInBits = 18 * 1024;
	// XXX: If a partition has less than this threshold, it is implemented as distributed RAM instead of BRAM
	const size_t bramThresholdInBits = 512;

	// Create array configuratiom map
	for(auto &it : arrayInfoCfgMap) {
		std::string arrayName = it.first;
		uint64_t sizeInByte = it.second.totalSize;
		size_t wordSizeInByte = it.second.wordSize;

		ConfigurationManager::partitionCfgMapTy::const_iterator found = completePartitionCfgMap.find(arrayName);
		ConfigurationManager::partitionCfgMapTy::const_iterator found2 = partitionCfgMap.find(arrayName);

		if(found != completePartitionCfgMap.end())
			arrayNameToConfig.insert(std::make_pair(arrayName, std::make_tuple(0, found->second->size, wordSizeInByte))); 
		else if(found2 != partitionCfgMap.end())
			arrayNameToConfig.insert(std::make_pair(arrayName, std::make_tuple(found2->second->pFactor, found2->second->size, found2->second->wordSize)));
		else
			arrayNameToConfig.insert(std::make_pair(arrayName, std::make_tuple(1, sizeInByte, wordSizeInByte)));
	}

	// Attempt to allocate the BRAM18k resources without worrying with resources constraint
	for(auto &it : arrayNameToConfig) {
		std::string arrayName = it.first;
		uint64_t numOfPartitions = std::get<0>(it.second);
		uint64_t totalSizeInBytes = std::get<1>(it.second);

		// Partial partitioning or no partition
		if(numOfPartitions) {
			float sizeInBitsPerPartition = (totalSizeInBytes * 8) / (float) numOfPartitions;
			uint64_t numOfBRAM18kPerPartition = 0;
			float efficiencyPerPartition = 1;

			if(sizeInBitsPerPartition > (float) bramThresholdInBits) {
				numOfBRAM18kPerPartition = nextPowerOf2((uint64_t) std::ceil(sizeInBitsPerPartition / (float) size18kInBits));
				efficiencyPerPartition = sizeInBitsPerPartition / (float) (numOfBRAM18kPerPartition * size18kInBits);
			}

			unsigned bram18kUsage = numOfPartitions * numOfBRAM18kPerPartition;
			usedBRAM18k += bram18kUsage;
			arrayNameToUsedBRAM18k.insert(std::make_pair(arrayName, bram18kUsage));
			arrayNameToEfficiency.insert(std::make_pair(arrayName, efficiencyPerPartition));
		}
		// Complete partition
		else {
			// TODO: Complete partition splits array into registers, thus no BRAM is used
			// However, LUTs and FF are used but are not being accounted here.
			arrayNameToUsedBRAM18k.insert(std::make_pair(arrayName, 0));
			arrayNameToEfficiency.insert(std::make_pair(arrayName, 0));
		}

		arrayAddPartitions(arrayName, numOfPartitions);
		arrayNameToWritePortsPerPartition[arrayName] = PER_PARTITION_PORTS_W;
	}

	// BRAM18k setting with partitioning does not fit in current device, will attempt without partitioning
	if(usedBRAM18k > maxBRAM18k) {
		errs() << "WARNING: Current BRAM18k exceeds the available amount of selected board. Ignoring partitioning information\n";
		usedBRAM18k = 0;
		arrayNameToNumOfPartitions.clear();
		arrayNameToUsedBRAM18k.clear();
		arrayNameToEfficiency.clear();

		for(auto &it : arrayNameToConfig) {
			std::string arrayName = it.first;
			uint64_t numOfPartitions = std::get<0>(it.second);
			// TODO: THIS SEEMS A BUG!!! THE ORIGINAL CODE IS USING WORDSIZE AS THE TOTAL SIZE.
			// keeping just for equality purposes, but this is very likely wrong!
			uint64_t totalSizeInBytes = std::get<2>(it.second);
			// TODO: The two following lines is what I consider the correct code
			//uint64_t totalSizeInBytes = std::get<1>(it.second);
			//size_t wordSizeInBytes = std::get<2>(it.second);

			// Partial partitioning or no partition
			if(numOfPartitions) {
				float sizeInBits = (totalSizeInBytes * 8);
				uint64_t numOfBRAM18k = nextPowerOf2((uint64_t) std::ceil(sizeInBits / (float) size18kInBits));
				float efficiency = sizeInBits / (float) (numOfBRAM18k * size18kInBits);

				usedBRAM18k += numOfBRAM18k;
				arrayNameToUsedBRAM18k.insert(std::make_pair(arrayName, numOfBRAM18k));
				arrayNameToEfficiency.insert(std::make_pair(arrayName, efficiency));
				arrayPartitionToReadPorts.insert(std::make_pair(arrayName, PER_PARTITION_PORTS_R));
				arrayPartitionToWritePorts.insert(std::make_pair(arrayName, PER_PARTITION_PORTS_W));
			}
			// Complete partition
			else {
				// TODO: Compared to the same code above, no "-register" string is added. Is this intentional or a bug???
#ifdef LEGACY_SEPARATOR
				arrayNameToUsedBRAM18k.insert(std::make_pair(arrayName + "-register", 0));
				arrayNameToEfficiency.insert(std::make_pair(arrayName + "-register", 0));
#else
				arrayNameToUsedBRAM18k.insert(std::make_pair(arrayName + GLOBAL_SEPARATOR "register", 0));
				arrayNameToEfficiency.insert(std::make_pair(arrayName + GLOBAL_SEPARATOR "register", 0));
#endif
				arrayPartitionToReadPorts.insert(std::make_pair(arrayName, INFINITE_RESOURCES));
				arrayPartitionToWritePorts.insert(std::make_pair(arrayName, INFINITE_RESOURCES));
			}

			arrayPartitionToReadPortsInUse.insert(std::make_pair(arrayName, 0));
			arrayPartitionToWritePortsInUse.insert(std::make_pair(arrayName, 0));
		}

		// TODO: This is a silent warning on the original code, but I'll put here as an error
		assert(usedBRAM18k <= maxBRAM18k && "Current BRAM18k exceeds the available amount of selected board even with partitioning disabled");
	}
	// BRAM18k setting with partitioning fits in current device
	else {
		// XXX: I don't know if these clears are needed, but...
		arrayPartitionToReadPorts.clear();
		arrayPartitionToWritePorts.clear();

		for(auto &it : arrayNameToConfig) {
			std::string arrayName = it.first;
			uint64_t numOfPartitions = std::get<0>(it.second);

			// Partial partitioning
			if(numOfPartitions > 1) {
				for(unsigned i = 0; i < numOfPartitions; i++) {
#ifdef LEGACY_SEPARATOR
					std::string partitionName = arrayName + "-" + std::to_string(i);
#else
					std::string partitionName = arrayName + GLOBAL_SEPARATOR + std::to_string(i);
#endif
					arrayPartitionToReadPorts.insert(std::make_pair(partitionName, PER_PARTITION_PORTS_R));
					arrayPartitionToWritePorts.insert(std::make_pair(partitionName, PER_PARTITION_PORTS_W));
					arrayPartitionToReadPortsInUse.insert(std::make_pair(partitionName, 0));
					arrayPartitionToWritePortsInUse.insert(std::make_pair(partitionName, 0));
				}
			}
			// No partitioning
			else if(numOfPartitions) {
				arrayPartitionToReadPorts.insert(std::make_pair(arrayName, PER_PARTITION_PORTS_R));
				arrayPartitionToWritePorts.insert(std::make_pair(arrayName, PER_PARTITION_PORTS_W));
				arrayPartitionToReadPortsInUse.insert(std::make_pair(arrayName, 0));
				arrayPartitionToWritePortsInUse.insert(std::make_pair(arrayName, 0));
			}
			// Complete partitioning
			else {
				arrayPartitionToReadPorts.insert(std::make_pair(arrayName, INFINITE_RESOURCES));
				arrayPartitionToWritePorts.insert(std::make_pair(arrayName, INFINITE_RESOURCES));
				arrayPartitionToReadPortsInUse.insert(std::make_pair(arrayName, 0));
				arrayPartitionToWritePortsInUse.insert(std::make_pair(arrayName, 0));
			}
		}
	}
}

void XilinxHardwareProfile::constrainHardware(
	const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
	const ConfigurationManager::partitionCfgMapTy &partitionCfgMap,
	const ConfigurationManager::partitionCfgMapTy &completePartitionCfgMap
) {
	setResourceLimits();

	HardwareProfile::constrainHardware(arrayInfoCfgMap, partitionCfgMap, completePartitionCfgMap);
}

void XilinxHardwareProfile::arrayAddPartition(std::string arrayName) {
	std::map<std::string, unsigned>::iterator found = arrayNameToNumOfPartitions.find(arrayName);
	if(arrayNameToNumOfPartitions.end() == found)
		arrayNameToNumOfPartitions.insert(std::make_pair(arrayName, 1));
	else
		found->second++;
}

void XilinxHardwareProfile::arrayAddPartitions(std::string arrayName, unsigned amount) {
	std::map<std::string, unsigned>::iterator found = arrayNameToNumOfPartitions.find(arrayName);
	if(arrayNameToNumOfPartitions.end() == found)
		arrayNameToNumOfPartitions.insert(std::make_pair(arrayName, amount));
	else
		found->second = amount;
}

unsigned XilinxHardwareProfile::arrayGetMaximumWritePortsPerPartition() {
	return PER_PARTITION_MAX_PORTS_W;
}

bool XilinxHardwareProfile::fAddAddUnit(bool commit) {
	// Hardware is constrained, we must first check if it is possible to add a new unit
	if(isConstrained) {
		if((usedDSP + DSP_FADD) > maxDSP || (usedFF + FF_FADD) > maxFF || (usedLUT + LUT_FADD) > maxLUT)
			return false;
	}

	if(commit) {
		usedDSP += DSP_FADD;
		usedFF += FF_FADD;
		usedLUT += LUT_FADD;
		fAddCount++;
	}

	return true;
}

bool XilinxHardwareProfile::fSubAddUnit(bool commit) {
	// Hardware is constrained, we must first check if it is possible to add a new unit
	if(isConstrained) {
		if((usedDSP + DSP_FSUB) > maxDSP || (usedFF + FF_FSUB) > maxFF || (usedLUT + LUT_FSUB) > maxLUT)
			return false;
	}

	if(commit) {
		usedDSP += DSP_FSUB;
		usedFF += FF_FSUB;
		usedLUT += LUT_FSUB;
		fSubCount++;
	}

	return true;
}

bool XilinxHardwareProfile::fMulAddUnit(bool commit) {
	// Hardware is constrained, we must first check if it is possible to add a new unit
	if(isConstrained) {
		if((usedDSP + DSP_FMUL) > maxDSP || (usedFF + FF_FMUL) > maxFF || (usedLUT + LUT_FMUL) > maxLUT)
			return false;
	}

	if(commit) {
		usedDSP += DSP_FMUL;
		usedFF += FF_FMUL;
		usedLUT += LUT_FMUL;
		fMulCount++;
	}

	return true;
}

bool XilinxHardwareProfile::fDivAddUnit(bool commit) {
	// Hardware is constrained, we must first check if it is possible to add a new unit
	if(isConstrained) {
		if((usedDSP + DSP_FDIV) > maxDSP || (usedFF + FF_FDIV) > maxFF || (usedLUT + LUT_FDIV) > maxLUT)
			return false;
	}

	if(commit) {
		usedDSP += DSP_FDIV;
		usedFF += FF_FDIV;
		usedLUT += LUT_FDIV;
		fDivCount++;
	}

	return true;
}

void XilinxVC707HardwareProfile::setResourceLimits() {
	if(args.fNoFPUThresOpt) {
		maxDSP = HardwareProfile::INFINITE_RESOURCES;
		maxFF = HardwareProfile::INFINITE_RESOURCES;
		maxLUT = HardwareProfile::INFINITE_RESOURCES;
		maxBRAM18k = HardwareProfile::INFINITE_RESOURCES;
	}
	else {
		maxDSP = MAX_DSP;
		maxFF = MAX_FF;
		maxLUT = MAX_LUT;
		maxBRAM18k = MAX_BRAM18K;
	}
}

void XilinxZC702HardwareProfile::setResourceLimits() {
	if(args.fNoFPUThresOpt) {
		maxDSP = HardwareProfile::INFINITE_RESOURCES;
		maxFF = HardwareProfile::INFINITE_RESOURCES;
		maxLUT = HardwareProfile::INFINITE_RESOURCES;
		maxBRAM18k = HardwareProfile::INFINITE_RESOURCES;
	}
	else {
		maxDSP = MAX_DSP;
		maxFF = MAX_FF;
		maxLUT = MAX_LUT;
		maxBRAM18k = MAX_BRAM18K;
	}
}

XilinxZCU102HardwareProfile::XilinxZCU102HardwareProfile() {
	effectivePeriod = (1000 / args.frequency) - (10 * args.uncertainty / args.frequency);

	/* Even if time-constrained scheduling is disabled, we still need to define the latencies of each instruction according to effective clock */
	for(auto &it : timeConstrainedLatencies) {
		unsigned currLatency;
		double currInCycleLatency;
		for(auto &it2 : it.second) {
			currLatency = it2.first;
			currInCycleLatency = it2.second;

			/* Found FU configuration that fits inside the target effective clock */
			if(it2.second <= effectivePeriod)
				break;
		}

		/* Save selected latency for this instruction */
		effectiveLatencies.insert(std::make_pair(it.first, std::make_pair(currLatency, currInCycleLatency)));
	}
}

void XilinxZCU102HardwareProfile::setResourceLimits() {
	if(args.fNoFPUThresOpt) {
		maxDSP = HardwareProfile::INFINITE_RESOURCES;
		maxFF = HardwareProfile::INFINITE_RESOURCES;
		maxLUT = HardwareProfile::INFINITE_RESOURCES;
		maxBRAM18k = HardwareProfile::INFINITE_RESOURCES;
	}
	else {
		maxDSP = MAX_DSP;
		maxFF = MAX_FF;
		maxLUT = MAX_LUT;
		maxBRAM18k = MAX_BRAM18K;
	}
}

unsigned XilinxZCU102HardwareProfile::getLatency(unsigned opcode) {
	switch(opcode) {
		case LLVM_IR_Shl:
		case LLVM_IR_LShr:
		case LLVM_IR_AShr:
		case LLVM_IR_And:
		case LLVM_IR_Or:
		case LLVM_IR_Xor:
		case LLVM_IR_ICmp:
		case LLVM_IR_Br:
		case LLVM_IR_IndexAdd:
		case LLVM_IR_IndexSub:
			return 0;
		case LLVM_IR_Add:
			return effectiveLatencies[LATENCY_ADD].first;
		case LLVM_IR_Sub: 
			return effectiveLatencies[LATENCY_SUB].first;
		case LLVM_IR_Call:
			return 0;
		case LLVM_IR_Store:
			return effectiveLatencies[LATENCY_STORE].first;
		case LLVM_IR_SilentStore:
			return 0;
		case LLVM_IR_Load:
			return (args.fILL)? effectiveLatencies[LATENCY_LOAD].first : effectiveLatencies[LATENCY_LOAD].first - 1;
		case LLVM_IR_Mul:
			return effectiveLatencies[LATENCY_MUL32].first;
		case LLVM_IR_UDiv:
		case LLVM_IR_SDiv:
			return effectiveLatencies[LATENCY_DIV32].first;
		case LLVM_IR_FAdd:
			return effectiveLatencies[LATENCY_FADD32].first;
		case LLVM_IR_FSub:
			return effectiveLatencies[LATENCY_FSUB32].first;
		case LLVM_IR_FMul:
			return effectiveLatencies[LATENCY_FMUL32].first;
		case LLVM_IR_FDiv:
			return effectiveLatencies[LATENCY_FDIV32].first;
		case LLVM_IR_FCmp:
			return effectiveLatencies[LATENCY_FCMP].first;
		default: 
			return 0;
	}
}

double XilinxZCU102HardwareProfile::getInCycleLatency(unsigned opcode) {
	switch(opcode) {
		case LLVM_IR_Shl:
		case LLVM_IR_LShr:
		case LLVM_IR_AShr:
		case LLVM_IR_And:
		case LLVM_IR_Or:
		case LLVM_IR_Xor:
		case LLVM_IR_ICmp:
		case LLVM_IR_Br:
		case LLVM_IR_IndexAdd:
		case LLVM_IR_IndexSub:
			return 0;
		case LLVM_IR_Add:
			return effectiveLatencies[LATENCY_ADD].second;
		case LLVM_IR_Sub: 
			return effectiveLatencies[LATENCY_SUB].second;
		case LLVM_IR_Call:
			return 0;
		case LLVM_IR_Store:
			return effectiveLatencies[LATENCY_STORE].second;
		case LLVM_IR_SilentStore:
			return 0;
		case LLVM_IR_Load:
			// XXX: fILL deve ter efeito aqui?
			return effectiveLatencies[LATENCY_LOAD].second;
		case LLVM_IR_Mul:
			return effectiveLatencies[LATENCY_MUL32].second;
		case LLVM_IR_UDiv:
		case LLVM_IR_SDiv:
			return effectiveLatencies[LATENCY_DIV32].second;
		case LLVM_IR_FAdd:
			return effectiveLatencies[LATENCY_FADD32].second;
		case LLVM_IR_FSub:
			return effectiveLatencies[LATENCY_FSUB32].second;
		case LLVM_IR_FMul:
			return effectiveLatencies[LATENCY_FMUL32].second;
		case LLVM_IR_FDiv:
			return effectiveLatencies[LATENCY_FDIV32].second;
		case LLVM_IR_FCmp:
			return effectiveLatencies[LATENCY_FCMP].second;
		default: 
			return 0;
	}
}
