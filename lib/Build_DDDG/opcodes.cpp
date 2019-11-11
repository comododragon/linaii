#include "profile_h/auxiliary.h"
#include "profile_h/opcodes.h"

bool isAssociative(unsigned microop) {
	return LLVM_IR_Add == microop;
}

bool isFAssociative(unsigned microop) {
	return LLVM_IR_FAdd == microop;
}

bool isMemoryOp(unsigned microop) {
	return LLVM_IR_Load == microop || LLVM_IR_Store == microop;
}

bool isComputeOp(unsigned microop) {
	switch(microop) {
		case LLVM_IR_Add:
		case LLVM_IR_FAdd:
		case LLVM_IR_Sub:
		case LLVM_IR_FSub:
		case LLVM_IR_Mul:
		case LLVM_IR_FMul:
		case LLVM_IR_UDiv:
		case LLVM_IR_SDiv:
		case LLVM_IR_FDiv:
		case LLVM_IR_URem:
		case LLVM_IR_SRem:
		case LLVM_IR_FRem:
		case LLVM_IR_Shl:
		case LLVM_IR_LShr:
		case LLVM_IR_AShr:
		case LLVM_IR_And:
		case LLVM_IR_Or:
		case LLVM_IR_Xor:
		case LLVM_IR_ICmp:
		case LLVM_IR_FCmp:
			return true;
		default:
			return false;
	}
}

bool isStoreOp(unsigned microop) {
	return LLVM_IR_Store == microop;
}

bool isLoadOp(unsigned microop) {
	return LLVM_IR_Load == microop;
}

bool isBitOp(unsigned microop) {
	switch(microop) {
		case LLVM_IR_Shl:
		case LLVM_IR_LShr:
		case LLVM_IR_AShr:
		case LLVM_IR_And:
		case LLVM_IR_Or:
		case LLVM_IR_Xor:
			return true;
		default:
			return false;
	}
}

bool isControlOp(unsigned microop) {
	return LLVM_IR_PHI == microop || isBranchOp(microop);
}

bool isPhiOp(unsigned microop) {
	return LLVM_IR_PHI == microop;
}

bool isBranchOp(unsigned microop) {
	return LLVM_IR_Br == microop || LLVM_IR_Switch == microop || isCallOp(microop);
}

bool isCallOp(unsigned microop) {
	return LLVM_IR_Call == microop || isDMAOp(microop);
}

bool isIndexOp(unsigned microop) {
	return LLVM_IR_IndexAdd == microop || LLVM_IR_IndexSub == microop;
}

bool isDMALoad(unsigned microop) {
	return LLVM_IR_DMALoad == microop;
}

bool isDMAStore(unsigned microop) {
	return LLVM_IR_DMAStore == microop;
}

bool isDMAOp(unsigned microop) {
	return isDMALoad(microop) || isDMAStore(microop);
}

bool isMulOp(unsigned microop) {
	switch(microop) {
		case LLVM_IR_Mul:
		case LLVM_IR_UDiv:
		case LLVM_IR_SDiv:
		case LLVM_IR_URem:
		case LLVM_IR_SRem:
			return true;
		default:
			return false;
	}
}

bool isAddOp(unsigned microop) {
	return LLVM_IR_Add == microop || LLVM_IR_Sub == microop;
}

bool isFAddOp(unsigned microop) {
	return LLVM_IR_FAdd == microop;
}

bool isFSubOp(unsigned microop) {
	return LLVM_IR_FSub == microop;
}

bool isFMulOp(unsigned microop) {
	return LLVM_IR_FMul == microop;
}

bool isFDivOp(unsigned microop) {
	return LLVM_IR_FDiv == microop;
}

bool isFCmpOp(unsigned microop) {
	return LLVM_IR_FCmp == microop;
}

bool isFloatOp(unsigned microop) {
	return LLVM_IR_FAdd == microop || LLVM_IR_FSub == microop || LLVM_IR_FMul == microop || LLVM_IR_FDiv == microop;
}

bool isDDRMemoryOp(unsigned microop) {
	return LLVM_IR_DDRReadReq == microop ||
		LLVM_IR_DDRRead == microop ||
		LLVM_IR_DDRWriteReq == microop ||
		LLVM_IR_DDRWrite == microop ||
		LLVM_IR_DDRWriteResp == microop;
}

bool isDDRReadOp(unsigned microop) {
	return LLVM_IR_DDRReadReq == microop ||
		LLVM_IR_DDRRead == microop;
}

bool isDDRWriteOp(unsigned microop) {
	return LLVM_IR_DDRWriteReq == microop ||
		LLVM_IR_DDRWrite == microop ||
		LLVM_IR_DDRWriteResp == microop;
}

bool isDDRLoad(unsigned microop) {
	return LLVM_IR_DDRRead == microop;
}

bool isDDRStore(unsigned microop) {
	return LLVM_IR_DDRWrite == microop;
}

unsigned getNonSilentOpcode(unsigned opcode) {
	switch(opcode) {
		case LLVM_IR_DDRSilentReadReq:
			return LLVM_IR_DDRReadReq;
		case LLVM_IR_DDRSilentRead:
			return LLVM_IR_DDRRead;
		case LLVM_IR_DDRSilentWriteReq:
			return LLVM_IR_DDRWriteReq;
		case LLVM_IR_DDRSilentWrite:
			return LLVM_IR_DDRWrite;
		case LLVM_IR_DDRSilentWriteResp:
			return LLVM_IR_DDRWriteResp;
		default:
			return opcode;
	}
}
