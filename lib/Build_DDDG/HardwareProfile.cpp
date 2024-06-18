#include "profile_h/HardwareProfile.h"

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

	totalFAddCount = 0;
	totalFSubCount = 0;
	totalFMulCount = 0;
	totalFDivCount = 0;

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
#ifdef CONSTRAIN_INT_OP
	intOpCount.clear();
#endif

	totalFAddCount = 0;
	totalFSubCount = 0;
	totalFMulCount = 0;
	totalFDivCount = 0;
#ifdef CONSTRAIN_INT_OP
	totalIntOpCount.clear();
#endif
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
#ifdef CONSTRAIN_INT_OP
	intOpThreshold.clear();
	for(auto &it : constrainedIntOps)
		intOpThreshold[it] = INFINITE_RESOURCES;
#endif
	if(!(args.fNoFPUThresOpt)) {
		thresholdSet = true;
		setThresholdWithCurrentUsage();
	}

	unrFAddCount = fAddCount;
	unrFSubCount = fSubCount;
	unrFMulCount = fMulCount;
	unrFDivCount = fDivCount;
#ifdef CONSTRAIN_INT_OP
	for(auto &it : constrainedIntOps)
		unrIntOpCount[it] = intOpCount[it];
#endif

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

#ifdef CONSTRAIN_INT_OP
	for(auto &it : constrainedIntOps) {
		if(intOpCount[it]) {
			uint64_t resIIOpCandidate = std::ceil(unrIntOpCount[it] / (float) intOpCount[it]);
			if(resIIOpCandidate > resIIOp) {
				resIIOp = resIIOpCandidate;
				resIIOpName = reverseOpcodeMap.at(it);
			}
		}
	}
#endif

	// XXX: If more resources are to be constrained, add equivalent logic here

	if(resIIOp > 1)
		return std::make_tuple(resIIOpName, resIIOp);
	else
		return std::make_tuple("none", 1);
}

void HardwareProfile::fillPack(Pack &P, unsigned loopLevel, unsigned datapathType, uint64_t targetII) {
	if(targetII)
		assert(DatapathType::NORMAL_LOOP == datapathType && "Attempt to calculate resources with II > 1 for a non-perfect body DDDG");

	P.addDescriptor("fAdd units", Pack::MERGE_MAX, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("fAdd units", fAddGetAmount());
	P.addDescriptor("fSub units", Pack::MERGE_MAX, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("fSub units", fSubGetAmount());
	P.addDescriptor("fMul units", Pack::MERGE_MAX, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("fMul units", fMulGetAmount());
	P.addDescriptor("fDiv units", Pack::MERGE_MAX, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("fDiv units", fDivGetAmount());

#ifdef CONSTRAIN_INT_OP
	for(auto &it : constrainedIntOps) {
		std::string key = reverseOpcodeMap.at(it) + " units";
		P.addDescriptor(key, Pack::MERGE_MAX, Pack::TYPE_UNSIGNED);
		P.addElement<uint64_t>(key, intOpGetAmount(it));
	}
#endif

	for(auto &it : arrayGetNumOfPartitions()) {
		P.addDescriptor("Number of partitions for array \"" + demangleArrayName(it.first) + "\"", Pack::MERGE_EQUAL, Pack::TYPE_UNSIGNED);
		P.addElement<uint64_t>("Number of partitions for array \"" + demangleArrayName(it.first) + "\"", it.second);
	}
	for(auto &it : arrayGetEfficiency()) {
		P.addDescriptor("Memory efficiency for array \"" + demangleArrayName(it.first) + "\"", Pack::MERGE_EQUAL, Pack::TYPE_FLOAT);
		P.addElement<float>("Memory efficiency for array \"" + demangleArrayName(it.first) + "\"", it.second);
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
		if(commit) {
			fAddInUse++;
			totalFAddCount++;
		}

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
		if(success && commit) {
			fAddInUse++;
			totalFAddCount++;
		}

		return success;
	}
}

bool HardwareProfile::fSubTryAllocate(bool commit) {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	// There are fSub available for use, just allocate it
	if(fSubInUse < fSubCount) {
		if(commit) {
			fSubInUse++;
			totalFSubCount++;
		}

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
		if(success && commit) {
			fSubInUse++;
			totalFSubCount++;
		}

		return success;
	}
}

bool HardwareProfile::fMulTryAllocate(bool commit) {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	// There are fMul available for use, just allocate it
	if(fMulInUse < fMulCount) {
		if(commit) {
			fMulInUse++;
			totalFMulCount++;
		}

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
		if(success && commit) {
			fMulInUse++;
			totalFMulCount++;
		}

		return success;
	}
}

bool HardwareProfile::fDivTryAllocate(bool commit) {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	// There are fDiv available for use, just allocate it
	if(fDivInUse < fDivCount) {
		if(commit) {
			fDivInUse++;
			totalFDivCount++;
		}

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
		if(success && commit) {
			fDivInUse++;
			totalFDivCount++;
		}

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

			return true;
		}
		// Attempt to allocate a new port failed
		else {
			return false;
		}
	}

	// Allocate a port
	if(commit)
		(found2->second)++;

	return true;
}

bool HardwareProfile::intOpTryAllocate(int opcode, bool commit) {
#ifdef CONSTRAIN_INT_OP
	assert(isConstrained && "This hardware profile is not resource-constrained");

	// There are available for use, just allocate it
	if(intOpInUse[opcode] < intOpCount[opcode]) {
		if(commit) {
			(intOpInUse[opcode])++;
			(totalIntOpCount[opcode])++;
		}

		return true;
	}
	// All are in use, try to allocate a new unit
	else {
		if(thresholdSet && intOpCount[opcode]) {
			if(intOpCount[opcode] >= intOpThreshold[opcode])
				return false;
		}

		// Try to allocate a new unit
		bool success = intOpAddUnit(opcode, commit);
		// If successful, mark this unit as allocated
		if(success && commit) {
			(intOpInUse[opcode])++;
			(totalIntOpCount[opcode])++;
		}

		return success;
	}
#else
	// For now, int ops are not constrained
	return true;
#endif
}

bool HardwareProfile::callTryAllocate(bool commit) {
	// For now, calls are not constrained
	return true;
}

bool HardwareProfile::ddrOpTryAllocate(unsigned node, int opcode, bool commit) {
	// First, we check if the respective read or write AXI port is in use. If positive, we fail right away
	// (please note that silent operations are not actually executed, so there is no port limitation for them)
	if((LLVM_IR_DDRReadReq == opcode || LLVM_IR_DDRRead == opcode) && ddrReadPortInUse)
		return false;
	if((LLVM_IR_DDRWriteReq == opcode || LLVM_IR_DDRWrite == opcode || LLVM_IR_DDRWriteResp == opcode) && ddrWritePortInUse)
		return false;

	// If not, we let MemoryModel decide its fate
	bool allocationResult = memmodel->tryAllocate(node, opcode, commit);

	// If positive, we set the port usage accordingly
	if(commit && allocationResult) {
		// Once again, silent operations do not affect port usage

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

#ifdef CONSTRAIN_INT_OP
	for(auto &it : constrainedIntOps) {
		if(isPipelined(it))
			intOpInUse[it] = 0;
	}
#endif

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
#ifdef CONSTRAIN_INT_OP
	assert(intOpInUse[opcode] && "Attempt to release int unit when none is allocated");
	(intOpInUse[opcode])--;
#endif
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

	memLogicFF = 0;
	memLogicLUT = 0;

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
#ifdef CONSTRAIN_INT_OP
	for(auto &it : constrainedIntOps)
		intOpResources[it] = intOpStandardResources.at(it);
#endif
}

void XilinxHardwareProfile::clear() {
	HardwareProfile::clear();

	arrayNameToUsedBRAM18k.clear();
	usedDSP = 0;
	usedFF = 0;
	usedLUT = 0;
	usedBRAM18k = 0;

	memLogicFF = 0;
	memLogicLUT = 0;
}

unsigned XilinxHardwareProfile::getLatency(unsigned opcode) {
	// XXX: Please check XilinxZCUHardwareProfile::getLatency() for more info about
	// why some opcodes have latency 0 and other 1
	switch(opcode) {
		case LLVM_IR_Shl:
		case LLVM_IR_LShr:
		case LLVM_IR_AShr:
		case LLVM_IR_And:
		case LLVM_IR_Or:
		case LLVM_IR_Xor:
		case LLVM_IR_ICmp:
			return 0;
		case LLVM_IR_Br:
			return 0;
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
		case LLVM_IR_Trunc:
		case LLVM_IR_SExt:
		case LLVM_IR_ZExt:
		case LLVM_IR_GetElementPtr:
			return 0;
		default:
			DBG_DUMP("Detected opcode (" << opcode << ", " << reverseOpcodeMap.at(opcode) << ") with unknown latency. Setting to default of 1\n");
			return 1;
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
#ifdef CONSTRAIN_INT_OP
		case LLVM_IR_Add:
		case LLVM_IR_Sub:
		case LLVM_IR_Mul:
		case LLVM_IR_SDiv:
		case LLVM_IR_UDiv:
		case LLVM_IR_And:
		case LLVM_IR_Or:
		case LLVM_IR_Xor:
		case LLVM_IR_Shl:
		case LLVM_IR_AShr:
		case LLVM_IR_LShr:
#ifdef BYTE_OPS
		case LLVM_IR_Add8:
		case LLVM_IR_Sub8:
		case LLVM_IR_Mul8:
		case LLVM_IR_SDiv8:
		case LLVM_IR_UDiv8:
		case LLVM_IR_And8:
		case LLVM_IR_Or8:
		case LLVM_IR_Xor8:
		case LLVM_IR_Shl8:
		case LLVM_IR_AShr8:
		case LLVM_IR_LShr8:
#endif
#ifdef CUSTOM_OPS
		case LLVM_IR_APAdd:
		case LLVM_IR_APSub:
		case LLVM_IR_APMul:
		case LLVM_IR_APDiv:
#endif
#endif
			return true;
		default: 
			return false;
	}
}

void XilinxHardwareProfile::calculateRequiredResources(
	std::vector<int> &microops,
	const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
	std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress,
	std::map<uint64_t, std::set<unsigned>> &maxTimesNodesMap
) {
	clear();

	unsigned fAddSubTotalCount = 0;
	unsigned fMulTotalCount = 0;
	unsigned fDivTotalCount = 0;
#ifdef CONSTRAIN_INT_OP
	fuCountTy intOpTotalCount;
#endif

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
#ifdef CONSTRAIN_INT_OP
		fuCountTy intOpCount;
#endif

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

#ifdef CONSTRAIN_INT_OP
			if(constrainedIntOps.count(opcode)) {
				(intOpCount[opcode])++;
				if(intOpCount[opcode] > intOpTotalCount[opcode]) {
					(intOpTotalCount[opcode])++;
					intOpAddUnit(opcode);
				}
			}
#endif
		}
	}
}

void XilinxHardwareProfile::setThresholdWithCurrentUsage() {
	assert(isConstrained && "This hardware profile is not resource-constrained");

	unsigned totalDSP = fAddCount * fAddDSP + fSubCount * fSubDSP + fMulCount * fMulDSP + fDivCount * fDivDSP;
#ifdef CONSTRAIN_INT_OP
	for(auto &it : constrainedIntOps)
		totalDSP += intOpCount[it] * intOpResources[it].dsp;
#endif

	if(totalDSP > maxDSP) {
		float scale = (float) totalDSP / (float) maxDSP;
		fAddThreshold = (unsigned) std::ceil((float) fAddCount / scale);
		fSubThreshold = (unsigned) std::ceil((float) fSubCount / scale);
		fMulThreshold = (unsigned) std::ceil((float) fMulCount / scale);
		fDivThreshold = (unsigned) std::ceil((float) fDivCount / scale);
#ifdef CONSTRAIN_INT_OP
		for(auto &it : constrainedIntOps)
			intOpThreshold[it] = (unsigned) std::ceil((float) intOpCount[it] / scale);
#endif

		std::vector<unsigned> scaledValues;
		scaledValues.push_back(fAddThreshold * fAddDSP);
		scaledValues.push_back(fSubThreshold * fSubDSP);
		scaledValues.push_back(fMulThreshold * fMulDSP);
		scaledValues.push_back(fDivThreshold * fDivDSP);
#ifdef CONSTRAIN_INT_OP
		for(auto &it : constrainedIntOps)
			scaledValues.push_back(intOpThreshold[it] * intOpResources[it].dsp);
#endif

		unsigned maxValue = *std::max_element(scaledValues.begin(), scaledValues.end());

		if(maxValue == fAddThreshold * fAddDSP)
			limitedBy.insert(LIMITED_BY_FADD);
		if(maxValue == fSubThreshold * fSubDSP)
			limitedBy.insert(LIMITED_BY_FSUB);
		if(maxValue == fMulThreshold * fMulDSP)
			limitedBy.insert(LIMITED_BY_FMUL);
		if(maxValue == fDivThreshold * fDivDSP)
			limitedBy.insert(LIMITED_BY_FDIV);
#ifdef CONSTRAIN_INT_OP
		for(auto &it : constrainedIntOps) {
			if(maxValue == intOpThreshold[it] * intOpResources[it].dsp) {
				limitedBy.insert(LIMITED_BY_INTOP);
				break;
			}
		}
#endif
	}
	else {
		fAddThreshold = fAddCount? fAddCount : INFINITE_RESOURCES;
		fSubThreshold = fSubCount? fSubCount : INFINITE_RESOURCES;
		fMulThreshold = fMulCount? fMulCount : INFINITE_RESOURCES;
		fDivThreshold = fDivCount? fDivCount : INFINITE_RESOURCES;
#ifdef CONSTRAIN_INT_OP
		for(auto &it : constrainedIntOps)
			intOpThreshold[it] = (intOpCount[it])? intOpCount[it] : INFINITE_RESOURCES;
#endif
	}
}

void XilinxHardwareProfile::setMemoryCurrentUsage(
	const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
	const ConfigurationManager::partitionCfgMapTy &partitionCfgMap,
	const ConfigurationManager::partitionCfgMapTy &completePartitionCfgMap
) {
	const size_t size18kInBits = 18 * 1024;
	// If a partition has less than this threshold, it is implemented as distributed RAM instead of BRAM
	const size_t bramThresholdInBits = 1024;

	// Create array configuratiom map
	for(auto &it : arrayInfoCfgMap) {
		// Ignore if the array is allocated off-chip
		if(ConfigurationManager::arrayInfoCfgTy::ARRAY_TYPE_OFFCHIP == it.second.type)
			continue;

		std::string arrayName = it.first;
		uint64_t sizeInByte = it.second.totalSize;
		size_t wordSizeInByte = it.second.wordSize;
		unsigned scope = it.second.scope;

		ConfigurationManager::partitionCfgMapTy::const_iterator found = completePartitionCfgMap.find(arrayName);
		ConfigurationManager::partitionCfgMapTy::const_iterator found2 = partitionCfgMap.find(arrayName);

		if(found != completePartitionCfgMap.end())
			arrayNameToConfig.insert(std::make_pair(arrayName, std::make_tuple(0, found->second.size, wordSizeInByte, scope)));
		else if(found2 != partitionCfgMap.end())
			arrayNameToConfig.insert(std::make_pair(arrayName, std::make_tuple(found2->second.pFactor, found2->second.size, found2->second.wordSize, scope)));
		else
			arrayNameToConfig.insert(std::make_pair(arrayName, std::make_tuple(1, sizeInByte, wordSizeInByte, scope)));
	}

	// Attempt to allocate the BRAM18k resources without worrying with resources constraint
	for(auto &it : arrayNameToConfig) {
		std::string arrayName = it.first;
		uint64_t numOfPartitions = std::get<0>(it.second);
		uint64_t totalSizeInBytes = std::get<1>(it.second);
		size_t wordSizeInBytes = std::get<2>(it.second);
		unsigned scope = std::get<3>(it.second);
		bool shouldCount = (ConfigurationManager::arrayInfoCfgTy::ARRAY_SCOPE_ROVAR == scope || ConfigurationManager::arrayInfoCfgTy::ARRAY_SCOPE_RWVAR == scope)
			|| (args.fArgRes && scope != ConfigurationManager::arrayInfoCfgTy::ARRAY_SCOPE_NOCOUNT);

		// Partial partitioning or no partition
		if(numOfPartitions) {
			float sizeInBitsPerPartition = (totalSizeInBytes * 8) / (float) numOfPartitions;

			if(shouldCount) {
				if(ConfigurationManager::arrayInfoCfgTy::ARRAY_SCOPE_ARG == scope || sizeInBitsPerPartition > (float) bramThresholdInBits) {
					uint64_t numOfBRAM18kPerPartition = nextPowerOf2((uint64_t) std::ceil(sizeInBitsPerPartition / (float) size18kInBits));
					float efficiencyPerPartition = sizeInBitsPerPartition / (float) (numOfBRAM18kPerPartition * size18kInBits);
					unsigned bram18kUsage = numOfPartitions * numOfBRAM18kPerPartition;

					usedBRAM18k += bram18kUsage;
					memLogicFF += numOfPartitions * logNextPowerOf2((sizeInBitsPerPartition / 8) / wordSizeInBytes);

					arrayNameToUsedBRAM18k.insert(std::make_pair(arrayName, bram18kUsage));
					arrayNameToEfficiency.insert(std::make_pair(arrayName, efficiencyPerPartition));
				}
				else {
					// For ROM: (1 * wordSizeInBytes * 8)
					// For RAM: (2 * wordSizeInBytes * 8)
					unsigned scopeFactor = (ConfigurationManager::arrayInfoCfgTy::ARRAY_SCOPE_ROVAR == scope)? 1 : 2;
					memLogicFF += numOfPartitions * (logNextPowerOf2((sizeInBitsPerPartition / 8) / wordSizeInBytes) + (scopeFactor * wordSizeInBytes * 8));
					memLogicLUT += numOfPartitions * std::ceil(sizeInBitsPerPartition / 64);

					arrayNameToUsedBRAM18k.insert(std::make_pair(arrayName, 0));
					arrayNameToEfficiency.insert(std::make_pair(arrayName, 0));
				}
			}
			else {
				arrayNameToUsedBRAM18k.insert(std::make_pair(arrayName, 0));
				arrayNameToEfficiency.insert(std::make_pair(arrayName, 0));
			}
		}
		// Complete partition
		else {
			// Complete partition splits array into registers, thus no BRAM is used
			arrayNameToUsedBRAM18k.insert(std::make_pair(arrayName, 0));
			arrayNameToEfficiency.insert(std::make_pair(arrayName, 0));

			if(shouldCount)
				memLogicFF += totalSizeInBytes * 8;
		}

		arrayAddPartitions(arrayName, numOfPartitions);
	}

	// BRAM18k setting with partitioning does not fit in current device, will attempt without partitioning
	if(usedBRAM18k > maxBRAM18k) {
		// XXX For now I am failing because this block requires better and careful analysis.
		assert(false && "Current BRAM18k exceeds the available amount of selected board");
#if 0
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
#endif
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

		usedFF += memLogicFF;
		usedLUT += memLogicLUT;
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

void XilinxHardwareProfile::fillPack(Pack &P, unsigned loopLevel, unsigned datapathType, uint64_t targetII) {
	P.addDescriptor("DSPs", Pack::MERGE_MAX, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("DSPs", resourcesGetDSPs());
	P.addDescriptor("FFs", Pack::MERGE_MAX, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("FFs", resourcesGetFFs());
	P.addDescriptor("LUTs", Pack::MERGE_MAX, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("LUTs", resourcesGetLUTs());
	P.addDescriptor("BRAM18k", Pack::MERGE_EQUAL, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("BRAM18k", resourcesGetBRAM18k());

	HardwareProfile::fillPack(P, loopLevel, datapathType, targetII);

	for(auto &it : arrayGetUsedBRAM18k()) {
		P.addDescriptor("Used BRAM18k for array \"" + demangleArrayName(it.first) + "\"", Pack::MERGE_EQUAL, Pack::TYPE_UNSIGNED);
		P.addElement<uint64_t>("Used BRAM18k for array \"" + demangleArrayName(it.first) + "\"", it.second);
	}

	/* Change FU count if pipeline is active */
	unsigned finalFAddCount = targetII? std::ceil((float) totalFAddCount / (float) targetII) : fAddCount;
	unsigned finalFSubCount = targetII? std::ceil((float) totalFSubCount / (float) targetII) : fSubCount;
	unsigned finalFMulCount = targetII? std::ceil((float) totalFMulCount / (float) targetII) : fMulCount;
	unsigned finalFDivCount = targetII? std::ceil((float) totalFDivCount / (float) targetII) : fDivCount;

	// Count resources for FUs that are shared among DDDGs (floating point ops)
#ifdef LEGACY_SEPARATOR
	P.addDescriptor("_shared~fadd", Pack::MERGE_RESOURCELISTMAX, Pack::TYPE_RESOURCENET);
	P.addElement<Pack::resourceNodeTy>("_shared~fadd", Pack::resourceNodeTy(finalFAddCount, finalFAddCount * fAddDSP, finalFAddCount * fAddFF, finalFAddCount * fAddLUT));
	P.addDescriptor("_shared~fsub", Pack::MERGE_RESOURCELISTMAX, Pack::TYPE_RESOURCENET);
	P.addElement<Pack::resourceNodeTy>("_shared~fsub", Pack::resourceNodeTy(finalFSubCount, finalFSubCount * fSubDSP, finalFSubCount * fSubFF, finalFSubCount * fSubLUT));
	P.addDescriptor("_shared~fmul", Pack::MERGE_RESOURCELISTMAX, Pack::TYPE_RESOURCENET);
	P.addElement<Pack::resourceNodeTy>("_shared~fmul", Pack::resourceNodeTy(finalFMulCount, finalFMulCount * fMulDSP, finalFMulCount * fMulFF, finalFMulCount * fMulLUT));
	P.addDescriptor("_shared~fdiv", Pack::MERGE_RESOURCELISTMAX, Pack::TYPE_RESOURCENET);
	P.addElement<Pack::resourceNodeTy>("_shared~fdiv", Pack::resourceNodeTy(finalFDivCount, finalFDivCount * fDivDSP, finalFDivCount * fDivFF, finalFDivCount * fDivLUT));
#else
	P.addDescriptor("_shared" GLOBAL_SEPARATOR "fadd", Pack::MERGE_RESOURCELISTMAX, Pack::TYPE_RESOURCENET);
	P.addElement<Pack::resourceNodeTy>("_shared" GLOBAL_SEPARATOR "fadd", Pack::resourceNodeTy(
		finalFAddCount, finalFAddCount * fAddDSP, finalFAddCount * fAddFF, finalFAddCount * fAddLUT
	));
	P.addDescriptor("_shared" GLOBAL_SEPARATOR "fsub", Pack::MERGE_RESOURCELISTMAX, Pack::TYPE_RESOURCENET);
	P.addElement<Pack::resourceNodeTy>("_shared" GLOBAL_SEPARATOR "fsub", Pack::resourceNodeTy(
		finalFSubCount, finalFSubCount * fSubDSP, finalFSubCount * fSubFF, finalFSubCount * fSubLUT
	));
	P.addDescriptor("_shared" GLOBAL_SEPARATOR "fmul", Pack::MERGE_RESOURCELISTMAX, Pack::TYPE_RESOURCENET);
	P.addElement<Pack::resourceNodeTy>("_shared" GLOBAL_SEPARATOR "fmul", Pack::resourceNodeTy(
		finalFMulCount, finalFMulCount * fMulDSP, finalFMulCount * fMulFF, finalFMulCount * fMulLUT
	));
	P.addDescriptor("_shared" GLOBAL_SEPARATOR "fdiv", Pack::MERGE_RESOURCELISTMAX, Pack::TYPE_RESOURCENET);
	P.addElement<Pack::resourceNodeTy>("_shared" GLOBAL_SEPARATOR "fdiv", Pack::resourceNodeTy(
		finalFDivCount, finalFDivCount * fDivDSP, finalFDivCount * fDivFF, finalFDivCount * fDivLUT
	));
#endif

	// Count resources for FUs that are not shared among DDDGs (int ops)
	// We assume that Vivado HLS allocates the FU needed to solve each DDDG separately
	// and then sum it up (before0 + after0 + before1 + after1 + ... + inner).
	// We only consider betweenX if it is larger than beforeX + afterX!
#ifdef CONSTRAIN_INT_OP
	for(auto &it : constrainedIntOps) {
		unsigned finalIntOpCount = targetII? std::ceil((float) totalIntOpCount[it] / (float) targetII) : intOpCount[it];

#ifdef LEGACY_SEPARATOR
		std::string descriptor = "_unshared~" + reverseOpcodeMap.at(it);
#else
		std::string descriptor = "_unshared" GLOBAL_SEPARATOR + reverseOpcodeMap.at(it);
#endif

		P.addDescriptor(descriptor, Pack::MERGE_RESOURCETREEMAX, Pack::TYPE_RESOURCENET);
		P.addElement<Pack::resourceNodeTy>(descriptor, Pack::resourceNodeTy(
			loopLevel, datapathType,
			finalIntOpCount,
			finalIntOpCount * intOpResources[it].dsp,
			finalIntOpCount * intOpResources[it].ff,
			finalIntOpCount * intOpResources[it].lut
		));
	}
#endif

	// Save LUT and FF used for array partitioning logic
	P.addDescriptor("_memlogicFF", Pack::MERGE_EQUAL, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("_memlogicFF", memLogicFF);
	P.addDescriptor("_memlogicLUT", Pack::MERGE_EQUAL, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("_memlogicLUT", memLogicLUT);
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

#ifdef CONSTRAIN_INT_OP
bool XilinxHardwareProfile::intOpAddUnit(unsigned opcode, bool commit) {
	fuResourcesTy res = intOpResources[opcode];

	// Hardware is constrained, we must first check if it is possible to add a new unit
	if(isConstrained) {
		if((usedDSP + res.dsp) > maxDSP || (usedFF + res.ff) > maxFF || (usedLUT + res.lut) > maxLUT)
			return false;
	}

	if(commit) {
		usedDSP += res.dsp;
		usedFF += res.ff;
		usedLUT += res.lut;
		(intOpCount[opcode])++;
	}

	return true;
}
#endif

void XilinxVC707HardwareProfile::setResourceLimits() {
	if(args.fNoFPUThresOpt) {
		maxDSP = INFINITE_RESOURCES;
		maxFF = INFINITE_RESOURCES;
		maxLUT = INFINITE_RESOURCES;
		maxBRAM18k = INFINITE_RESOURCES;
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
		maxDSP =INFINITE_RESOURCES;
		maxFF = INFINITE_RESOURCES;
		maxLUT = INFINITE_RESOURCES;
		maxBRAM18k = INFINITE_RESOURCES;
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

			// If the current instruction has its delay defined as double::max, we have a special condition
			if(std::numeric_limits<double>::max() == currInCycleLatency) {
				// If currInCycleLatency is defined as double::max, it means that this instruction is constrained
				// by the effective target period, regardless of its value. This can happen for example with
				// offchip transactions.
				currInCycleLatency = effectivePeriod;
				break;
			}

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
		if(LLVM_IR_FAdd == it.first) {
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
		else if(LLVM_IR_FSub == it.first) {
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
		else if(LLVM_IR_FMul == it.first) {
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
		else if(LLVM_IR_FDiv == it.first) {
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
#ifdef CONSTRAIN_INT_OP
		else if(constrainedIntOps.count(it.first)) {
			assert(
				timeConstrainedDSPs.end() != foundDSPs &&
				timeConstrainedFFs.end() != foundFFs &&
				timeConstrainedLUTs.end() != foundLUTs &&
				"int op is resource-constrained but hardware profile library has no information about its resources"
			);

			intOpResources[it.first] = fuResourcesTy(foundDSPs->second.at(currLatency), foundFFs->second.at(currLatency), foundLUTs->second.at(currLatency), 0);
		}
#endif
	}
}

unsigned XilinxZCUHardwareProfile::getLatency(unsigned opcode) {
	// Combinational nodes are mapped as follows:
	// Nodes that are abstract and don't generate critical path (e.g. truncate): latency 0, in-cycle delay 0.0
	// Nodes that are combinational however take some time to execute: latency 1, in-cycle delay != 0
	switch(opcode) {
		case LLVM_IR_And:
		case LLVM_IR_Or:
		case LLVM_IR_Xor:
		case LLVM_IR_Shl:
		case LLVM_IR_AShr:
		case LLVM_IR_LShr:
#ifdef BYTE_OPS
		case LLVM_IR_Add8:
		case LLVM_IR_Sub8:
		case LLVM_IR_Mul8:
		case LLVM_IR_UDiv8:
		case LLVM_IR_SDiv8:
		case LLVM_IR_And8:
		case LLVM_IR_Or8:
		case LLVM_IR_Xor8:
		case LLVM_IR_Shl8:
		case LLVM_IR_AShr8:
		case LLVM_IR_LShr8:
#endif
#ifdef CUSTOM_OPS
		case LLVM_IR_APAdd:
		case LLVM_IR_APSub:
		case LLVM_IR_APMul:
		case LLVM_IR_APDiv:
#endif
#ifdef CONSTRAIN_INT_OP
			return effectiveLatencies[opcode].first;
#endif
		case LLVM_IR_ICmp:
			return 0;
		case LLVM_IR_Br:
			return 0;
		case LLVM_IR_IndexAdd:
			return 0;
		case LLVM_IR_IndexSub:
			return 0;
		case LLVM_IR_Add:
			return effectiveLatencies[LLVM_IR_Add].first;
		case LLVM_IR_Sub: 
			return effectiveLatencies[LLVM_IR_Sub].first;
		case LLVM_IR_Call:
			return 0;
		case LLVM_IR_Store:
			return effectiveLatencies[LLVM_IR_Store].first;
		case LLVM_IR_SilentStore:
			return 0;
		case LLVM_IR_Load:
			return (args.fILL)? effectiveLatencies[LLVM_IR_Load].first : effectiveLatencies[LLVM_IR_Load].first - 1;
		case LLVM_IR_Mul:
			return effectiveLatencies[LLVM_IR_Mul].first;
		case LLVM_IR_UDiv:
		case LLVM_IR_SDiv:
			return effectiveLatencies[LLVM_IR_UDiv].first;
		case LLVM_IR_FAdd:
			return effectiveLatencies[LLVM_IR_FAdd].first;
		case LLVM_IR_FSub:
			return effectiveLatencies[LLVM_IR_FSub].first;
		case LLVM_IR_FMul:
			return effectiveLatencies[LLVM_IR_FMul].first;
		case LLVM_IR_FDiv:
			return effectiveLatencies[LLVM_IR_FDiv].first;
		case LLVM_IR_FCmp:
			return effectiveLatencies[LLVM_IR_FCmp].first;
		case LLVM_IR_DDRReadReq:
			return effectiveLatencies[LLVM_IR_DDRReadReq].first;
		case LLVM_IR_DDRRead:
			return effectiveLatencies[LLVM_IR_DDRRead].first;
		case LLVM_IR_DDRWriteReq:
			return effectiveLatencies[LLVM_IR_DDRWriteReq].first;
		case LLVM_IR_DDRWrite:
			return effectiveLatencies[LLVM_IR_DDRWrite].first;
		case LLVM_IR_DDRWriteResp:
			return effectiveLatencies[LLVM_IR_DDRWriteResp].first;
		case LLVM_IR_DDRSilentReadReq:
		case LLVM_IR_DDRSilentRead:
		case LLVM_IR_DDRSilentWriteReq:
		case LLVM_IR_DDRSilentWrite:
		case LLVM_IR_DDRSilentWriteResp:
			return 0;
		case LLVM_IR_Trunc:
		case LLVM_IR_SExt:
		case LLVM_IR_ZExt:
		case LLVM_IR_GetElementPtr:
			return 0;
		case LLVM_IR_Dummy:
			return 0;
		default: 
			DBG_DUMP("Detected opcode (" << opcode << ", " << reverseOpcodeMap.at(opcode) << ") with unknown latency. Setting to default of 1\n");
			return 1;
	}
}

double XilinxZCUHardwareProfile::getInCycleLatency(unsigned opcode) {
	// Instructions that have latency 0.001 are combinational operations
	// that DO take some time. There are stuff, for example truncate,
	// that is simply a question of "cutting" some wires, so there is no delay in this
	switch(opcode) {
		case LLVM_IR_And:
		case LLVM_IR_Or:
		case LLVM_IR_Xor:
		case LLVM_IR_Shl:
		case LLVM_IR_AShr:
		case LLVM_IR_LShr:
#ifdef BYTE_OPS
		case LLVM_IR_Add8:
		case LLVM_IR_Sub8:
		case LLVM_IR_Mul8:
		case LLVM_IR_UDiv8:
		case LLVM_IR_SDiv8:
		case LLVM_IR_And8:
		case LLVM_IR_Or8:
		case LLVM_IR_Xor8:
		case LLVM_IR_Shl8:
		case LLVM_IR_AShr8:
		case LLVM_IR_LShr8:
#endif
#ifdef CUSTOM_OPS
		case LLVM_IR_APAdd:
		case LLVM_IR_APSub:
		case LLVM_IR_APMul:
		case LLVM_IR_APDiv:
#endif
#ifdef CONSTRAIN_INT_OP
			return effectiveLatencies[opcode].second;
#endif
		case LLVM_IR_ICmp:
			return 0.001;
		case LLVM_IR_Br:
			return 0;
		case LLVM_IR_IndexAdd:
			return effectiveLatencies[LLVM_IR_Add].second;
		case LLVM_IR_IndexSub:
			return effectiveLatencies[LLVM_IR_Sub].second;
		case LLVM_IR_Add:
			return effectiveLatencies[LLVM_IR_Add].second;
		case LLVM_IR_Sub: 
			return effectiveLatencies[LLVM_IR_Sub].second;
		case LLVM_IR_Call:
			return 0;
		case LLVM_IR_Store:
			return effectiveLatencies[LLVM_IR_Store].second;
		case LLVM_IR_SilentStore:
			return 0;
		case LLVM_IR_Load:
			return effectiveLatencies[LLVM_IR_Load].second;
		case LLVM_IR_Mul:
			return effectiveLatencies[LLVM_IR_Mul].second;
		case LLVM_IR_UDiv:
		case LLVM_IR_SDiv:
			return effectiveLatencies[LLVM_IR_UDiv].second;
		case LLVM_IR_FAdd:
			return effectiveLatencies[LLVM_IR_FAdd].second;
		case LLVM_IR_FSub:
			return effectiveLatencies[LLVM_IR_FSub].second;
		case LLVM_IR_FMul:
			return effectiveLatencies[LLVM_IR_FMul].second;
		case LLVM_IR_FDiv:
			return effectiveLatencies[LLVM_IR_FDiv].second;
		case LLVM_IR_FCmp:
			return effectiveLatencies[LLVM_IR_FCmp].second;
		case LLVM_IR_DDRReadReq:
			return effectiveLatencies[LLVM_IR_DDRReadReq].second;
		case LLVM_IR_DDRRead:
			return effectiveLatencies[LLVM_IR_DDRRead].second;
		case LLVM_IR_DDRWriteReq:
			return effectiveLatencies[LLVM_IR_DDRWriteReq].second;
		case LLVM_IR_DDRWrite:
			return effectiveLatencies[LLVM_IR_DDRWrite].second;
		case LLVM_IR_DDRWriteResp:
			return effectiveLatencies[LLVM_IR_DDRWriteResp].second;
		case LLVM_IR_DDRSilentReadReq:
		case LLVM_IR_DDRSilentRead:
		case LLVM_IR_DDRSilentWriteReq:
		case LLVM_IR_DDRSilentWrite:
		case LLVM_IR_DDRSilentWriteResp:
			return 0;
		case LLVM_IR_Trunc:
		case LLVM_IR_SExt:
		case LLVM_IR_ZExt:
		case LLVM_IR_GetElementPtr:
			return 0;
		case LLVM_IR_Dummy:
			return 0;
		default: 
			return 0.001;
	}
}

void XilinxZCU102HardwareProfile::setResourceLimits() {
	if(args.fNoFPUThresOpt) {
		maxDSP = INFINITE_RESOURCES;
		maxFF = INFINITE_RESOURCES;
		maxLUT = INFINITE_RESOURCES;
		maxBRAM18k = INFINITE_RESOURCES;
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
		maxDSP = INFINITE_RESOURCES;
		maxFF = INFINITE_RESOURCES;
		maxLUT = INFINITE_RESOURCES;
		maxBRAM18k = INFINITE_RESOURCES;
	}
	else {
		maxDSP = MAX_DSP;
		maxFF = MAX_FF;
		maxLUT = MAX_LUT;
		maxBRAM18k = MAX_BRAM18K;
	}
}
