#ifndef OPCODES_H
#define OPCODES_H

#define LLVM_IR_Move               0
#define LLVM_IR_Ret                1
#define LLVM_IR_Br                 2
#define LLVM_IR_Switch             3
#define LLVM_IR_IndirectBr         4
#define LLVM_IR_Invoke             5
#define LLVM_IR_Resume             6
#define LLVM_IR_Unreachable        7
#define LLVM_IR_Add                8
#define LLVM_IR_FAdd               9
#define LLVM_IR_Sub                 10
#define LLVM_IR_FSub                11
#define LLVM_IR_Mul                 12
#define LLVM_IR_FMul                13
#define LLVM_IR_UDiv                14
#define LLVM_IR_SDiv                15
#define LLVM_IR_FDiv                16
#define LLVM_IR_URem                17
#define LLVM_IR_SRem                18
#define LLVM_IR_FRem                19
#define LLVM_IR_Shl                 20
#define LLVM_IR_LShr                21
#define LLVM_IR_AShr                22
#define LLVM_IR_And                 23
#define LLVM_IR_Or                  24
#define LLVM_IR_Xor                 25
#define LLVM_IR_Alloca              26
#define LLVM_IR_Load                27
#define LLVM_IR_Store               28
#define LLVM_IR_GetElementPtr       29
#define LLVM_IR_Fence               30
#define LLVM_IR_AtomicCmpXchg       31
#define LLVM_IR_AtomicRMW           32
#define LLVM_IR_Trunc               33
#define LLVM_IR_ZExt                34
#define LLVM_IR_SExt                35
#define LLVM_IR_FPToUI              36
#define LLVM_IR_FPToSI              37
#define LLVM_IR_UIToFP              38
#define LLVM_IR_SIToFP              39
#define LLVM_IR_FPTrunc             40
#define LLVM_IR_FPExt               41
#define LLVM_IR_PtrToInt            42
#define LLVM_IR_IntToPtr            43
#define LLVM_IR_BitCast             44
#define LLVM_IR_AddrSpaceCast       45
#define LLVM_IR_ICmp                46
#define LLVM_IR_FCmp                47
#define LLVM_IR_PHI                 48
#define LLVM_IR_Call                49
#define LLVM_IR_Select              50
#define LLVM_IR_VAArg               53
#define LLVM_IR_ExtractElement      54
#define LLVM_IR_InsertElement       55
#define LLVM_IR_ShuffleVector       56
#define LLVM_IR_ExtractValue        57
#define LLVM_IR_InsertValue         58
#define LLVM_IR_LandingPad          59
#define LLVM_IR_DMAStore            98
#define LLVM_IR_DMALoad             99
#define LLVM_IR_IndexAdd            100
#define LLVM_IR_IndexSub            102
#define LLVM_IR_SilentStore         101

// Pseudo-opcodes for offchip transactions
#define LLVM_IR_DDRReadReq 0xf5
#define LLVM_IR_DDRRead 0xf6
#define LLVM_IR_DDRWriteReq 0xf7
#define LLVM_IR_DDRWrite 0xf8
#define LLVM_IR_DDRWriteResp 0xf9
#define LLVM_IR_DDRSilentReadReq 0xfa
#define LLVM_IR_DDRSilentRead 0xfb
#define LLVM_IR_DDRSilentWriteReq 0xfc
#define LLVM_IR_DDRSilentWrite 0xfd
#define LLVM_IR_DDRSilentWriteResp 0xfe

// Pseudo-opcode for dummy transaction
#define LLVM_IR_Dummy 0xff

static const std::map<unsigned, std::string> reverseOpcodeMap = {
	{0, "move"},
	{1, "ret"},
	{2, "br"},
	{3, "switch"},
	{4, "indirectbr"},
	{5, "invoke"},
	{6, "resume"},
	{7, "unreachable"},
	{8, "add"},
	{9, "fadd"},
	{10, "sub"},
	{11, "fsub"},
	{12, "mul"},
	{13, "fmul"},
	{14, "udiv"},
	{15, "sdiv"},
	{16, "fdiv"},
	{17, "urem"},
	{18, "srem"},
	{19, "frem"},
	{20, "shl"},
	{21, "lshr"},
	{22, "ashr"},
	{23, "and"},
	{24, "or"},
	{25, "xor"},
	{26, "alloca"},
	{27, "load"},
	{28, "store"},
	{29, "getelementptr"},
	{30, "fence"},
	{31, "atomiccmpxchg"},
	{32, "atomicrmw"},
	{33, "trunc"},
	{34, "zext"},
	{35, "sext"},
	{36, "fptoui"},
	{37, "fptosi"},
	{38, "uitofp"},
	{39, "sitofp"},
	{40, "fptrunc"},
	{41, "fpext"},
	{42, "ptrtoint"},
	{43, "inttoptr"},
	{44, "bitcast"},
	{45, "addrspacecast"},
	{46, "icmp"},
	{47, "fcmp"},
	{48, "phi"},
	{49, "call"},
	{50, "select"},
	{53, "vaarg"},
	{54, "extractelement"},
	{55, "insertelement"},
	{56, "shufflevector"},
	{57, "extractvalue"},
	{58, "insertvalue"},
	{59, "landingpad"},
	{98, "dmastore"},
	{99, "dmaload"},
	{100, "indexadd"},
	{102, "indexsub"},
	{101, "silentstore"},

	{0xf5, "ddrreadreq"},
	{0xf6, "ddrread"},
	{0xf7, "ddrwritereq"},
	{0xf8, "ddrwrite"},
	{0xf9, "ddrwriteresp"},
	{0xfa, "ddrsilentreadreq"},
	{0xfb, "ddrsilentread"},
	{0xfc, "ddrsilentwritereq"},
	{0xfd, "ddrsilentwrite"},
	{0xfe, "ddrsilentwriteresp"},

	{0xff, "dummy"}
};

bool isAssociative(unsigned microop);
bool isFAssociative(unsigned microop);
bool isMemoryOp(unsigned microop);
bool isComputeOp(unsigned microop);
bool isStoreOp(unsigned microop);
bool isBitOp(unsigned microop);
bool isAddOp(unsigned microop);
bool isMulOp(unsigned microop);
bool isLoadOp(unsigned microop);
bool isCallOp(unsigned microop);
bool isBranchOp (unsigned microop);
bool isPhiOp(unsigned microop);
bool isControlOp (unsigned microop);
bool isIndexOp (unsigned microop);
bool isDMALoad(unsigned microop);
bool isDMAStore(unsigned microop);
bool isDMAOp(unsigned microop);
bool isFAddOp(unsigned microop);
bool isFSubOp(unsigned microop);
bool isFMulOp(unsigned microop);
bool isFDivOp(unsigned microop);
bool isFCmpOp(unsigned microop);
bool isFloatOp(unsigned microop);

bool isDDRMemoryOp(unsigned microop);
bool isDDRReadOp(unsigned microop);
bool isDDRWriteOp(unsigned microop);
bool isDDRLoad(unsigned microop);
bool isDDRStore(unsigned microop);

#endif
