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

void XilinxHardwareProfile::clear() {
	arrayNameToNumOfPartitions.clear();
	usedDSP = 0;
	usedFF = 0;
	usedLUT = 0;
	fAddCount = 0;
	fSubCount = 0;
	fMulCount = 0;
	fDivCount = 0;
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

void XilinxHardwareProfile::fAddAddUnit() {
	usedDSP += DSP_FADD;
	usedFF += FF_FADD;
	usedLUT += LUT_FADD;
	fAddCount++;
}

void XilinxHardwareProfile::fSubAddUnit() {
	usedDSP += DSP_FSUB;
	usedFF += FF_FSUB;
	usedLUT += LUT_FSUB;
	fSubCount++;
}

void XilinxHardwareProfile::fMulAddUnit() {
	usedDSP += DSP_FMUL;
	usedFF += FF_FMUL;
	usedLUT += LUT_FMUL;
	fMulCount++;
}

void XilinxHardwareProfile::fDivAddUnit() {
	usedDSP += DSP_FDIV;
	usedFF += FF_FDIV;
	usedLUT += LUT_FDIV;
	fDivCount++;
}
