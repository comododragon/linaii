#include "profile_h/NodeLatency.h"

#include "profile_h/opcodes.h"

NodeLatency *NodeLatency::createInstance() {
	// XXX: Right now only Xilinx boards are supported, therefore this is the only constructor available for now
	switch(args.target) {
		case ArgPack::TARGET_XILINX_VC707:
		case ArgPack::TARGET_XILINX_ZC702:
		default:
			return new XilinxNodeLatency();
	}
}

unsigned XilinxNodeLatency::getLatency(unsigned opcode) {
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
