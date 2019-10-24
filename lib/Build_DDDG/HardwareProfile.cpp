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
	ddrReadPortInUse = false;
	ddrWritePortInUse = false;

	fAddThreshold = 0;
	fSubThreshold = 0;
	fMulThreshold = 0;
	fDivThreshold = 0;

	isConstrained = false;
	thresholdSet = false;

	memmodel = nullptr;
}

HardwareProfile *HardwareProfile::createInstance() {
	switch(args.target) {
		case ArgPack::TARGET_XILINX_VC707:
			assert(args.fNoTCS && "Time-constrained scheduling is currently not supported with the selected platform. Please activate the \"--fno-tcs\" flag");
			return new XilinxVC707HardwareProfile();
		case ArgPack::TARGET_XILINX_ZCU102:
			return new XilinxZCU102HardwareProfile();
		case ArgPack::TARGET_XILINX_ZCU104:
			return new XilinxZCU104HardwareProfile();
		case ArgPack::TARGET_XILINX_ZC702:
		default:
			assert(args.fNoTCS && "Time-constrained scheduling is currently not supported with the selected platform. Please activate the \"--fno-tcs\" flag");
			return new XilinxZC702HardwareProfile();
	}
}

void HardwareProfile::setMemoryModel(MemoryModel *memmodel) {
	this->memmodel = memmodel;
}

void HardwareProfile::clear() {
	arrayNameToNumOfPartitions.clear();
	fAddCount = 0;
	fSubCount = 0;
	fMulCount = 0;
	fDivCount = 0;
}

void HardwareProfile::performMemoryModelAnalysis() {
	memmodel->analyseAndTransform();
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
	ddrReadPortInUse = false;
	ddrWritePortInUse = false;

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

void HardwareProfile::fillPack(Pack &P) {
	P.addDescriptor("fAdd units", Pack::MERGE_MAX, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("fAdd units", fAddGetAmount());
	P.addDescriptor("fSub units", Pack::MERGE_MAX, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("fSub units", fSubGetAmount());
	P.addDescriptor("fMul units", Pack::MERGE_MAX, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("fMul units", fMulGetAmount());
	P.addDescriptor("fDiv units", Pack::MERGE_MAX, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("fDiv units", fDivGetAmount());

	for(auto &it : arrayGetNumOfPartitions()) {
		P.addDescriptor("Number of partitions for array \"" + mangledName2ArrayNameMap.at(it.first) + "\"", Pack::MERGE_EQUAL, Pack::TYPE_UNSIGNED);
		P.addElement<uint64_t>("Number of partitions for array \"" + mangledName2ArrayNameMap.at(it.first) + "\"", it.second);
	}
	for(auto &it : arrayGetEfficiency()) {
		P.addDescriptor("Memory efficiency for array \"" + mangledName2ArrayNameMap.at(it.first) + "\"", Pack::MERGE_EQUAL, Pack::TYPE_FLOAT);
		P.addElement<float>("Memory efficiency for array \"" + mangledName2ArrayNameMap.at(it.first) + "\"", it.second);
	}
}

unsigned HardwareProfile::arrayGetNumOfPartitions(std::string arrayName) {
	// XXX: If the arrayName doesn't exist, this access will add it automatically
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
	// For now, fCmp is not constrained
	return true;
}

bool HardwareProfile::loadTryAllocate(std::string arrayPartitionName, bool commit) {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	std::map<std::string, unsigned>::iterator found = arrayPartitionToReadPorts.find(arrayPartitionName);
	std::map<std::string, unsigned>::iterator found2 = arrayPartitionToReadPortsInUse.find(arrayPartitionName);
	assert(found != arrayPartitionToReadPorts.end() && "Array has no storage allocated for it");
	assert(found2 != arrayPartitionToReadPortsInUse.end() && "Array has no storage allocated for it");

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
	assert(found != arrayPartitionToWritePorts.end() && "Array has no storage allocated for it");
	assert(found2 != arrayPartitionToWritePortsInUse.end() && "Array has no storage allocated for it");

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
			if(commit)
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

bool HardwareProfile::intOpTryAllocate(int opcode, bool commit) {
	// For now, int ops are not constrained
	return true;
}

bool HardwareProfile::callTryAllocate(bool commit) {
	// For now, calls are not constrained
	return true;
}

bool HardwareProfile::ddrOpTryAllocate(unsigned node, int opcode, bool commit) {
	// First, we check if the respective read or write AXI port is in use. If positive, we fail right away
	// TODO: Maybe the read port should not be separated from the write port. We must check that!
	if(ddrReadPortInUse && (LLVM_IR_DDRReadReq == opcode || LLVM_IR_DDRRead == opcode))
		return false;
	if(ddrWritePortInUse && (LLVM_IR_DDRWriteReq == opcode || LLVM_IR_DDRWrite == opcode || LLVM_IR_DDRWriteResp == opcode))
		return false;

	// If not, we let MemoryModel decide its fate
	bool allocationResult = memmodel->tryAllocate(node, opcode, commit);

	// If positive, we set the port usage accordingly
	if(allocationResult) {
		if(LLVM_IR_DDRReadReq == opcode || LLVM_IR_DDRRead == opcode)
			ddrReadPortInUse = true;
		if(LLVM_IR_DDRWriteReq == opcode || LLVM_IR_DDRWrite == opcode || LLVM_IR_DDRWriteResp == opcode)
			ddrWritePortInUse = true;
	}

	return allocationResult;
}

void HardwareProfile::pipelinedRelease() {
	// Release constrained pipelined functional units
	if(isPipelined(LLVM_IR_FAdd))
		fAddInUse = 0;
	if(isPipelined(LLVM_IR_FSub))
		fSubInUse = 0;
	if(isPipelined(LLVM_IR_FMul))
		fMulInUse = 0;
	if(isPipelined(LLVM_IR_FDiv))
		fDivInUse = 0;

	// Release memory ports if load/store are pipelined
	if(isPipelined(LLVM_IR_Load)) {
		for(auto &it : arrayPartitionToReadPortsInUse)
			it.second = 0;
	}
	if(isPipelined(LLVM_IR_Store)) {
		for(auto &it : arrayPartitionToWritePortsInUse)
			it.second = 0;
	}

	// Release DDR ports (we are assuming that these ports are always pipelined)
	ddrReadPortInUse = false;
	ddrWritePortInUse = false;
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
	//assert(false && "fCmp is not constrained");
}

void HardwareProfile::loadRelease(std::string arrayPartitionName) {
	std::map<std::string, unsigned>::iterator found = arrayPartitionToReadPortsInUse.find(arrayPartitionName);
	assert(found != arrayPartitionToReadPortsInUse.end() && "No array/partition found with the provided name");
	assert(found->second && "Attempt to release read port when none is allocated for this array/partition");
	(found->second)--;
}

void HardwareProfile::storeRelease(std::string arrayPartitionName) {
	std::map<std::string, unsigned>::iterator found = arrayPartitionToWritePortsInUse.find(arrayPartitionName);
	assert(found != arrayPartitionToWritePortsInUse.end() && "No array/partition found with the provided name");
	assert(found->second && "Attempt to release write port when none is allocated for this array/partition");
	(found->second)--;
}

void HardwareProfile::intOpRelease(int opcode) {
	//assert(false && "Integer ops are not constrained");
}

void HardwareProfile::callRelease() {
	//assert(false && "Calls are not constrained");
}

void HardwareProfile::ddrOpRelease(unsigned node, int opcode) {
	// MemoryModel is responsible for this
	return memmodel->release(node, opcode);
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

	fAddDSP = DSP_FADD;
	fAddFF = FF_FADD;
	fAddLUT = LUT_FADD;
	fSubDSP = DSP_FSUB;
	fSubFF = FF_FSUB;
	fSubLUT = LUT_FSUB;
	fMulDSP = DSP_FMUL;
	fMulFF = FF_FMUL;
	fMulLUT = LUT_FMUL;
	fDivDSP = DSP_FDIV;
	fDivFF = FF_FDIV;
	fDivLUT = LUT_FDIV;
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
			// This fILL comes from Lin-Analyzer. According to them, if partitioning is
			// enabled without pipelining, load uses 2 cycles. When no partitioning,
			// load uses 1. I am not quite sure if this is right. Currently fILL is
			// hard-enabled, so this is being quite ignored.
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

double XilinxHardwareProfile::getInCycleLatency(unsigned opcode) {
	assert(args.fNoTCS && "Time-constrained scheduling is currently not supported with the selected platform. Please activate the \"--fno-tcs\" flag");
	return 0;
}

bool XilinxHardwareProfile::isPipelined(unsigned opcode) {
	// TODO: what about the DDR transactions?
	switch(opcode) {
		case LLVM_IR_FAdd:
		case LLVM_IR_FSub:
		case LLVM_IR_FMul:
		case LLVM_IR_FDiv:
		case LLVM_IR_Load:
		case LLVM_IR_Store:
			return true;
		// XXX: fCmp, integer ops and call are here but because we are not constraining those resources!
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

	// In current implementation, only floating point operations are considered

	unsigned fAddSubTotalCount = 0;
	unsigned fMulTotalCount = 0;
	unsigned fDivTotalCount = 0;

	std::map<std::string, unsigned> arrayNameToReadPorts;
	std::map<std::string, unsigned> arrayNameToWritePorts;

	for(auto &it : arrayInfoCfgMap) {
		// Ignore if the array is allocated off-chip
		if(ConfigurationManager::arrayInfoCfgTy::ARRAY_TYPE_OFFCHIP == it.second.type)
			continue;

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
				std::string arrayName = baseAddress[it2].first;
				arrayNameToReadPorts[arrayName]++;
				if(arrayNameToReadPorts[arrayName] > XilinxHardwareProfile::PER_PARTITION_PORTS_R * arrayGetNumOfPartitions(arrayName))
					arrayAddPartition(arrayName);
			}

			if(isStoreOp(opcode)) {
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

	unsigned totalDSP = fAddCount * fAddDSP + fSubCount * fSubDSP + fMulCount * fMulDSP + fDivCount * fDivDSP;

	if(totalDSP > maxDSP) {
		float scale = (float) totalDSP / (float) maxDSP;
		fAddThreshold = (unsigned) std::ceil((float) fAddCount / scale);
		fSubThreshold = (unsigned) std::ceil((float) fSubCount / scale);
		fMulThreshold = (unsigned) std::ceil((float) fMulCount / scale);
		fDivThreshold = (unsigned) std::ceil((float) fDivCount / scale);

		std::vector<unsigned> scaledValues;
		scaledValues.push_back(fAddThreshold * fAddDSP);
		scaledValues.push_back(fSubThreshold * fSubDSP);
		scaledValues.push_back(fMulThreshold * fMulDSP);
		scaledValues.push_back(fDivThreshold * fDivDSP);

		unsigned maxValue = *std::max_element(scaledValues.begin(), scaledValues.end());

		if(maxValue == fAddThreshold * fAddDSP)
			limitedBy.insert(LIMITED_BY_FADD);
		if(maxValue == fSubThreshold * fSubDSP)
			limitedBy.insert(LIMITED_BY_FSUB);
		if(maxValue == fMulThreshold * fMulDSP)
			limitedBy.insert(LIMITED_BY_FMUL);
		if(maxValue == fDivThreshold * fDivDSP)
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
	// If a partition has less than this threshold, it is implemented as distributed RAM instead of BRAM
	const size_t bramThresholdInBits = 512;

	// Create array configuratiom map
	for(auto &it : arrayInfoCfgMap) {
		// Ignore if the array is allocated off-chip
		if(ConfigurationManager::arrayInfoCfgTy::ARRAY_TYPE_OFFCHIP == it.second.type)
			continue;

		std::string arrayName = it.first;
		uint64_t sizeInByte = it.second.totalSize;
		size_t wordSizeInByte = it.second.wordSize;

		ConfigurationManager::partitionCfgMapTy::const_iterator found = completePartitionCfgMap.find(arrayName);
		ConfigurationManager::partitionCfgMapTy::const_iterator found2 = partitionCfgMap.find(arrayName);

		if(found != completePartitionCfgMap.end())
			arrayNameToConfig.insert(std::make_pair(arrayName, std::make_tuple(0, found->second.size, wordSizeInByte))); 
		else if(found2 != partitionCfgMap.end())
			arrayNameToConfig.insert(std::make_pair(arrayName, std::make_tuple(found2->second.pFactor, found2->second.size, found2->second.wordSize)));
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
			// XXX: Complete partition splits array into registers, thus no BRAM is used
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
			// TODO: THIS SEEMS A BUG!!! THE ORIGINAL CODE IS USING WORDSIZE AS THE TOTAL SIZE. Keeping just for equality purposes, but this is very likely wrong!
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

		assert(usedBRAM18k <= maxBRAM18k && "Current BRAM18k exceeds the available amount of selected board even with partitioning disabled");
	}
	// BRAM18k setting with partitioning fits in current device
	else {
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

void XilinxHardwareProfile::fillPack(Pack &P) {
	P.addDescriptor("DSPs", Pack::MERGE_MAX, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("DSPs", resourcesGetDSPs());
	P.addDescriptor("FFs", Pack::MERGE_MAX, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("FFs", resourcesGetFFs());
	P.addDescriptor("LUTs", Pack::MERGE_MAX, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("LUTs", resourcesGetLUTs());
	P.addDescriptor("BRAM18k", Pack::MERGE_EQUAL, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("BRAM18k", resourcesGetBRAM18k());

	HardwareProfile::fillPack(P);

	for(auto &it : arrayGetUsedBRAM18k()) {
		P.addDescriptor("Used BRAM18k for array \"" + mangledName2ArrayNameMap.at(it.first) + "\"", Pack::MERGE_EQUAL, Pack::TYPE_UNSIGNED);
		P.addElement<uint64_t>("Used BRAM18k for array \"" + mangledName2ArrayNameMap.at(it.first) + "\"", it.second);
	}
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
		if((usedDSP + fAddDSP) > maxDSP || (usedFF + fAddFF) > maxFF || (usedLUT + fAddLUT) > maxLUT)
			return false;
	}

	if(commit) {
		usedDSP += fAddDSP;
		usedFF += fAddFF;
		usedLUT += fAddLUT;
		fAddCount++;
	}

	return true;
}

bool XilinxHardwareProfile::fSubAddUnit(bool commit) {
	// Hardware is constrained, we must first check if it is possible to add a new unit
	if(isConstrained) {
		if((usedDSP + fSubDSP) > maxDSP || (usedFF + fSubFF) > maxFF || (usedLUT + fSubLUT) > maxLUT)
			return false;
	}

	if(commit) {
		usedDSP += fSubDSP;
		usedFF += fSubFF;
		usedLUT += fSubLUT;
		fSubCount++;
	}

	return true;
}

bool XilinxHardwareProfile::fMulAddUnit(bool commit) {
	// Hardware is constrained, we must first check if it is possible to add a new unit
	if(isConstrained) {
		if((usedDSP + fMulDSP) > maxDSP || (usedFF + fMulFF) > maxFF || (usedLUT + fMulLUT) > maxLUT)
			return false;
	}

	if(commit) {
		usedDSP += fMulDSP;
		usedFF += fMulFF;
		usedLUT += fMulLUT;
		fMulCount++;
	}

	return true;
}

bool XilinxHardwareProfile::fDivAddUnit(bool commit) {
	// Hardware is constrained, we must first check if it is possible to add a new unit
	if(isConstrained) {
		if((usedDSP + fDivDSP) > maxDSP || (usedFF + fDivFF) > maxFF || (usedLUT + fDivLUT) > maxLUT)
			return false;
	}

	if(commit) {
		usedDSP += fDivDSP;
		usedFF += fDivFF;
		usedLUT += fDivLUT;
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

XilinxZCUHardwareProfile::XilinxZCUHardwareProfile() {
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

		/* Check if this instruction is resource constrained. If positive, also save the resource count for this FU configuration */
		std::unordered_map<unsigned, std::map<unsigned, unsigned>>::const_iterator foundDSPs = timeConstrainedDSPs.find(it.first);
		std::unordered_map<unsigned, std::map<unsigned, unsigned>>::const_iterator foundFFs = timeConstrainedFFs.find(it.first);
		std::unordered_map<unsigned, std::map<unsigned, unsigned>>::const_iterator foundLUTs = timeConstrainedLUTs.find(it.first);
		if(LATENCY_FADD32 == it.first) {
			assert(
				timeConstrainedDSPs.end() != foundDSPs &&
				timeConstrainedFFs.end() != foundFFs &&
				timeConstrainedLUTs.end() != foundLUTs &&
				"fadd is resource-constrained but hardware profile library has no information about its resources"
			);

			fAddDSP = foundDSPs->second.at(currLatency);
			fAddFF = foundFFs->second.at(currLatency);
			fAddLUT = foundLUTs->second.at(currLatency);
		}
		else if(LATENCY_FSUB32 == it.first) {
			assert(
				timeConstrainedDSPs.end() != foundDSPs &&
				timeConstrainedFFs.end() != foundFFs &&
				timeConstrainedLUTs.end() != foundLUTs &&
				"fsub is resource-constrained but hardware profile library has no information about its resources"
			);

			fSubDSP = foundDSPs->second.at(currLatency);
			fSubFF = foundFFs->second.at(currLatency);
			fSubLUT = foundLUTs->second.at(currLatency);
		}
		else if(LATENCY_FMUL32 == it.first) {
			assert(
				timeConstrainedDSPs.end() != foundDSPs &&
				timeConstrainedFFs.end() != foundFFs &&
				timeConstrainedLUTs.end() != foundLUTs &&
				"fmul is resource-constrained but hardware profile library has no information about its resources"
			);

			fMulDSP = foundDSPs->second.at(currLatency);
			fMulFF = foundFFs->second.at(currLatency);
			fMulLUT = foundLUTs->second.at(currLatency);
		}
		else if(LATENCY_FDIV32 == it.first) {
			assert(
				timeConstrainedDSPs.end() != foundDSPs &&
				timeConstrainedFFs.end() != foundFFs &&
				timeConstrainedLUTs.end() != foundLUTs &&
				"fdiv is resource-constrained but hardware profile library has no information about its resources"
			);

			fDivDSP = foundDSPs->second.at(currLatency);
			fDivFF = foundFFs->second.at(currLatency);
			fDivLUT = foundLUTs->second.at(currLatency);
		}
	}
}

unsigned XilinxZCUHardwareProfile::getLatency(unsigned opcode) {
	switch(opcode) {
		case LLVM_IR_Shl:
		case LLVM_IR_LShr:
		case LLVM_IR_AShr:
		case LLVM_IR_And:
		case LLVM_IR_Or:
		case LLVM_IR_Xor:
		case LLVM_IR_ICmp:
		case LLVM_IR_Br:
			return 0;
		case LLVM_IR_IndexAdd:
			// Even though a normal add/sub is performed here, it is not registered, so no latency (but it has an in-cycle latency!)
			return 0;
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
		case LLVM_IR_DDRReadReq:
			return effectiveLatencies[LATENCY_DDRREADREQ].first;
		case LLVM_IR_DDRRead:
			return effectiveLatencies[LATENCY_DDRREAD].first;
		case LLVM_IR_DDRWriteReq:
			return effectiveLatencies[LATENCY_DDRWRITEREQ].first;
		case LLVM_IR_DDRWrite:
			return effectiveLatencies[LATENCY_DDRWRITE].first;
		case LLVM_IR_DDRWriteResp:
			return effectiveLatencies[LATENCY_DDRWRITERESP].first;
		default: 
			return 0;
	}
}

double XilinxZCUHardwareProfile::getInCycleLatency(unsigned opcode) {
	switch(opcode) {
		case LLVM_IR_Shl:
		case LLVM_IR_LShr:
		case LLVM_IR_AShr:
		case LLVM_IR_And:
		case LLVM_IR_Or:
		case LLVM_IR_Xor:
		case LLVM_IR_ICmp:
		case LLVM_IR_Br:
			return 0;
		case LLVM_IR_IndexAdd:
			return effectiveLatencies[LATENCY_ADD].second;
		case LLVM_IR_IndexSub:
			return effectiveLatencies[LATENCY_SUB].second;
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
			// XXX: Must fILL also affect here?
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
		case LLVM_IR_DDRReadReq:
			return effectiveLatencies[LATENCY_DDRREADREQ].second;
		case LLVM_IR_DDRRead:
			return effectiveLatencies[LATENCY_DDRREAD].second;
		case LLVM_IR_DDRWriteReq:
			return effectiveLatencies[LATENCY_DDRWRITEREQ].second;
		case LLVM_IR_DDRWrite:
			return effectiveLatencies[LATENCY_DDRWRITE].second;
		case LLVM_IR_DDRWriteResp:
			return effectiveLatencies[LATENCY_DDRWRITERESP].second;
		default: 
			return 0;
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

void XilinxZCU104HardwareProfile::setResourceLimits() {
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
