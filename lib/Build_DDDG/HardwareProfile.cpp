#include "profile_h/HardwareProfile.h"

#include "profile_h/opcodes.h"

HardwareProfile *HardwareProfile::createInstance() {
	// XXX: Right now only Xilinx boards are supported, therefore this is the only constructor available for now
	switch(args.target) {
		case ArgPack::TARGET_XILINX_VC707:
			return new XilinxVC707HardwareProfile();
		case ArgPack::TARGET_XILINX_ZC702:
		default:
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

	setMemoryCurrentUsage(arrayInfoCfgMap, partitionCfgMap, completePartitionCfgMap);

	clear();
}

bool HardwareProfile::fAddTryAllocate() {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	// There are fAdd available for use, just allocate it
	if(fAddInUse < fAddCount) {
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
		bool success = fAddAddUnit();
		// If successful, mark this unit as allocated
		if(success)
			fAddInUse++;

		return success;
	}
}

bool HardwareProfile::fSubTryAllocate() {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	// There are fSub available for use, just allocate it
	if(fSubInUse < fSubCount) {
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
		bool success = fSubAddUnit();
		// If successful, mark this unit as allocated
		if(success)
			fSubInUse++;

		return success;
	}
}

bool HardwareProfile::fMulTryAllocate() {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	// There are fMul available for use, just allocate it
	if(fMulInUse < fMulCount) {
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
		bool success = fMulAddUnit();
		// If successful, mark this unit as allocated
		if(success)
			fMulInUse++;

		return success;
	}
}

bool HardwareProfile::fDivTryAllocate() {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	// There are fDiv available for use, just allocate it
	if(fDivInUse < fDivCount) {
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
		bool success = fDivAddUnit();
		// If successful, mark this unit as allocated
		if(success)
			fDivInUse++;

		return success;
	}
}

bool HardwareProfile::loadTryAllocate(std::string arrayPartitionName) {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	std::map<std::string, unsigned>::iterator found = arrayPartitionToReadPorts.find(arrayPartitionName);
	std::map<std::string, unsigned>::iterator found2 = arrayPartitionToReadPortsInUse.find(arrayPartitionName);
	assert(found != arrayPartitionToReadPorts.end() && "Array has no storage allocated for it");
	assert(found2 != arrayPartitionToReadPortsInUse.end() && "Array has no storage allocated for it");

	// All ports are being used, not able to allocate right now
	if(found2->second >= found->second)
		return false;

	// Allocate a port
	(found2->second)++;

	return true;
}

bool HardwareProfile::storeTryAllocate(std::string arrayPartitionName) {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	std::map<std::string, unsigned>::iterator found = arrayPartitionToWritePorts.find(arrayPartitionName);
	std::map<std::string, unsigned>::iterator found2 = arrayPartitionToWritePortsInUse.find(arrayPartitionName);
	assert(found != arrayPartitionToWritePorts.end() && "Array has no storage allocated for it");
	assert(found2 != arrayPartitionToWritePortsInUse.end() && "Array has no storage allocated for it");

	// All ports are being used
	if(found2->second >= found->second) {
		// If RW ports are enabled, attempt to allocate a new port
		if(args.fRWRWMem && found->second < arrayGetMaximumPortsPerPartition()) {
			(found->second)++;
			(found2->second)++;

#ifdef LEGACY_SEPARATOR
			size_t tagPos = arrayPartitionName.find("-");
#else
			size_t tagPos = arrayPartitionName.find(GLOBAL_SEPARATOR);
#endif
			// TODO: euacho que isso funciona sem o if
			(arrayNameToWritePortsPerPartition[arrayPartitionName.substr(0, tagPos)])++;

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

void XilinxHardwareProfile::clear() {
	HardwareProfile::clear();

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
		fAddThreshold = fMulCount? fMulCount : INFINITE_RESOURCES;
		fAddThreshold = fDivCount? fDivCount : INFINITE_RESOURCES;
	}
}

void XilinxHardwareProfile::setMemoryCurrentUsage(
	const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
	const ConfigurationManager::partitionCfgMapTy &partitionCfgMap,
	const ConfigurationManager::partitionCfgMapTy &completePartitionCfgMap
) {
	//arrayNameToWritePortsPerPartition.clear();
	//arrayPartitionToReadPorts.clear();
	//arrayParitionToReadPortsInUse.clear();
	//arrayPartitionToWritePorts.clear();
	//arrayPartitionToWritePortsInUse.clear();

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

	// TODO: parei aqui (aqui é o começo da função setBRAM18K_usage()
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

unsigned XilinxHardwareProfile::arrayGetNumOfPartitions(std::string arrayName) {
	// XXX: If the arrayName doesn't exist, this access will add it automatically. Is this desired behaviour?
	return arrayNameToNumOfPartitions[arrayName];
}

unsigned XilinxHardwareProfile::arrayGetMaximumPortsPerPartition() {
	return PER_PARTITION_MAX_PORTS_W;
}

bool XilinxHardwareProfile::fAddAddUnit() {
	// Hardware is constrained, we must first check if it is possible to add a new unit
	if(isConstrained) {
		if((usedDSP + DSP_FADD) > maxDSP || (usedFF + FF_FADD) > maxFF || (usedLUT + LUT_FADD) > maxLUT)
			return false;
	}

	usedDSP += DSP_FADD;
	usedFF += FF_FADD;
	usedLUT += LUT_FADD;
	fAddCount++;

	return true;
}

bool XilinxHardwareProfile::fSubAddUnit() {
	// Hardware is constrained, we must first check if it is possible to add a new unit
	if(isConstrained) {
		if((usedDSP + DSP_FSUB) > maxDSP || (usedFF + FF_FSUB) > maxFF || (usedLUT + LUT_FSUB) > maxLUT)
			return false;
	}

	usedDSP += DSP_FSUB;
	usedFF += FF_FSUB;
	usedLUT += LUT_FSUB;
	fSubCount++;

	return true;
}

bool XilinxHardwareProfile::fMulAddUnit() {
	// Hardware is constrained, we must first check if it is possible to add a new unit
	if(isConstrained) {
		if((usedDSP + DSP_FMUL) > maxDSP || (usedFF + FF_FMUL) > maxFF || (usedLUT + LUT_FMUL) > maxLUT)
			return false;
	}

	usedDSP += DSP_FMUL;
	usedFF += FF_FMUL;
	usedLUT += LUT_FMUL;
	fMulCount++;

	return true;
}

bool XilinxHardwareProfile::fDivAddUnit() {
	// Hardware is constrained, we must first check if it is possible to add a new unit
	if(isConstrained) {
		if((usedDSP + DSP_FDIV) > maxDSP || (usedFF + FF_FDIV) > maxFF || (usedLUT + LUT_FDIV) > maxLUT)
			return false;
	}

	usedDSP += DSP_FDIV;
	usedFF += FF_FDIV;
	usedLUT += LUT_FDIV;
	fDivCount++;

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
