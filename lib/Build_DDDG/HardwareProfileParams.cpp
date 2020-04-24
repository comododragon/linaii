#ifndef HARDWAREPROFILEPARAMS_CPP
#define HARDWAREPROFILEPARAMS_CPP

#include "profile_h/HardwareProfile.h"

/*
LLVM_IR_FAdd/LLVM_IR_FSub:
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |     DSP48E |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
	|          1 |       15.8 |          2 |        177 |        194 |
	|          2 |       12.6 |          2 |        177 |        194 |
	|          3 |       10.5 |          2 |        177 |        194 |
	|          4 |       6.43 |          2 |        227 |        214 |
	|          5 |       5.02 |          2 |        205 |        208 |
	|          6 |       4.82 |          2 |        237 |        216 |
	|          7 |       4.08 |          2 |        306 |        246 |
	|          8 |       3.45 |          2 |        296 |        239 |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	|         10 |       2.46 |          2 |        365 |        238 |
	|         11 |       2.26 |          2 |        369 |        236 |
	|         12 |       1.78 |          0 |        572 |        411 |
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
LLVM_IR_FMul:
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |     DSP48E |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
	|          1 |       10.5 |          3 |        128 |        135 |
	|          2 |       8.41 |          3 |        128 |        135 |
	|          3 |       7.01 |          3 |        128 |        135 |
	|          4 |       3.79 |          3 |        143 |        139 |
	|          5 |       3.17 |          3 |        151 |        145 |
	|          6 |       2.56 |          3 |        165 |        146 |
	|          7 |       2.41 |          3 |        197 |        123 |
	|          8 |       1.72 |          3 |        199 |        146 |
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
LLVM_IR_FDiv:
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |     DSP48E |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
	|          1 |       54.6 |          0 |        128 |        755 |
	|          2 |       43.7 |          0 |        128 |        755 |
	|          3 |       36.4 |          0 |        128 |        755 |
	|          4 |       33.9 |          0 |        166 |        755 |
	|          5 |       17.6 |          0 |        218 |        779 |
	|          6 |       12.0 |          0 |        268 |        792 |
	|          7 |       9.66 |          0 |        315 |        792 |
	|          8 |       8.27 |          0 |        363 |        802 |
	|          9 |       7.05 |          0 |        411 |        802 |
	|         10 |       5.69 |          0 |        459 |        799 |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	|         12 |       4.36 |          0 |        563 |        810 |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	|         15 |       4.34 |          0 |        563 |        807 |
	|         16 |       3.03 |          0 |        761 |        800 |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	|         28 |       3.01 |          0 |        785 |        822 |
	|         29 |       2.91 |          0 |        814 |        812 |
	|         30 |       2.23 |          0 |       1436 |        867 |
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
LLVM_IR_Add/LLVM_IR_Sub:
	+ ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- +
	|          1 |       1.01 |          0 |         39 |
	+ ---------- + ---------- + ---------- + ---------- +
LLVM_IR_Mul:
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |     DSP48E |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
	|          1 |       3.42 |          3 |          0 |         20 |
	|          2 |       2.36 |          4 |        165 |         49 |
	|          3 |       2.11 |          4 |        166 |         49 |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	|          5 |       1.59 |          4 |        215 |          1 |
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
LLVM_IR_SDiv/LLVM_IR_UDiv:
	+ ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- +
	|         36 |       1.47 |        394 |        238 |
	+ ---------- + ---------- + ---------- + ---------- +
LLVM_IR_And/LLVM_IR_Or/LLVM_IR_Xor:
	+ ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- +
	|          1 |       0.35 |          0 |         32 |
	+ ---------- + ---------- + ---------- + ---------- +
LLVM_IR_Shl/LLVM_IR_AShr/LLVM_IR_LShr:
	+ ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- +
	|          1 |       1.38 |          0 |         97 |
	+ ---------- + ---------- + ---------- + ---------- +
LLVM_IR_Add8/LLVM_IR_Sub8:
	+ ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- +
	|          1 |       0.76 |          0 |         15 |
	+ ---------- + ---------- + ---------- + ---------- +
LLVM_IR_Mul8:
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |     DSP48E |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
	|          1 |       1.65 |          0 |          0 |         40 |
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
LLVM_IR_SDiv8/LLVM_IR_UDiv8:
	+ ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- +
	|         12 |       1.87 |        106 |         41 |
	+ ---------- + ---------- + ---------- + ---------- +
LLVM_IR_And8/LLVM_IR_Or8/LLVM_IR_Xor8:
	+ ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- +
	|          1 |       0.33 |          0 |          8 |
	+ ---------- + ---------- + ---------- + ---------- +
LLVM_IR_Shl8/LLVM_IR_AShr8/LLVM_IR_LShr8:
	+ ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- +
	|          1 |       0.74 |          0 |         16 |
	+ ---------- + ---------- + ---------- + ---------- +
LLVM_IR_APAdd/LLVM_IR_APSub:
	+ ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- +
	|          1 |       2.26 |          0 |        384 |
	|          2 |       1.12 |        580 |        130 |
	+ ---------- + ---------- + ---------- + ---------- +
LLVM_IR_APMul:
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |     DSP48E |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
	|          1 |       55.3 |        497 |          0 |       9296 |
	|          2 |       3.68 |         16 |        361 |        178 |
	|          3 |       3.41 |         16 |        362 |        178 |
	| xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx | xxxxxxxxxx |
	|          5 |       2.61 |         16 |        441 |        249 |
	|          6 |       1.81 |         16 |        527 |        179 |
	+ ---------- + ---------- + ---------- + ---------- + ---------- +
LLVM_IR_APDiv:
	+ ---------- + ---------- + ---------- + ---------- +
	|    latency |      delay |         FF |        LUT |
	+ ---------- + ---------- + ---------- + ---------- +
	|        388 |       1.56 |        779 |        469 |
	+ ---------- + ---------- + ---------- + ---------- +
*/

#ifdef CONSTRAIN_INT_OP
const std::set<unsigned> HardwareProfile::constrainedIntOps = {
	LLVM_IR_Add,
	LLVM_IR_Sub,
	LLVM_IR_Mul,
	LLVM_IR_SDiv,
	LLVM_IR_UDiv,
	LLVM_IR_And,
	LLVM_IR_Or,
	LLVM_IR_Xor,
	LLVM_IR_Shl,
	LLVM_IR_AShr,
	LLVM_IR_LShr,
#ifdef BYTE_OPS
	LLVM_IR_Add8,
	LLVM_IR_Sub8,
	LLVM_IR_Mul8,
	LLVM_IR_SDiv8,
	LLVM_IR_UDiv8,
	LLVM_IR_And8,
	LLVM_IR_Or8,
	LLVM_IR_Xor8,
	LLVM_IR_Shl8,
	LLVM_IR_AShr8,
	LLVM_IR_LShr8,
#endif
#ifdef CUSTOM_OPS
	LLVM_IR_APAdd,
	LLVM_IR_APSub,
	LLVM_IR_APMul,
	LLVM_IR_APDiv
#endif
};

const std::unordered_map<unsigned, XilinxHardwareProfile::fuResourcesTy> XilinxHardwareProfile::intOpStandardResources {
	{LLVM_IR_Add, fuResourcesTy(0, 0, 39, 0)},
	{LLVM_IR_Sub, fuResourcesTy(0, 0, 39, 0)},
	{LLVM_IR_Mul, fuResourcesTy(3, 0, 20, 0)},
	{LLVM_IR_SDiv, fuResourcesTy(0, 394, 238, 0)},
	{LLVM_IR_UDiv, fuResourcesTy(0, 394, 238, 0)},
	{LLVM_IR_And, fuResourcesTy(0, 0, 32, 0)},
	{LLVM_IR_Or, fuResourcesTy(0, 0, 32, 0)},
	{LLVM_IR_Xor, fuResourcesTy(0, 0, 32, 0)},
	{LLVM_IR_Shl, fuResourcesTy(0, 0, 97, 0)},
	{LLVM_IR_AShr, fuResourcesTy(0, 0, 97, 0)},
	{LLVM_IR_LShr, fuResourcesTy(0, 0, 97, 0)},
#ifdef BYTE_OPS
	{LLVM_IR_Add8, fuResourcesTy(0, 0, 15, 0)},
	{LLVM_IR_Sub8, fuResourcesTy(0, 0, 15, 0)},
	{LLVM_IR_Mul8, fuResourcesTy(0, 0, 40, 0)},
	{LLVM_IR_SDiv8, fuResourcesTy(0, 106, 41, 0)},
	{LLVM_IR_UDiv8, fuResourcesTy(0, 106, 41, 0)},
	{LLVM_IR_And8, fuResourcesTy(0, 0, 8, 0)},
	{LLVM_IR_Or8, fuResourcesTy(0, 0, 8, 0)},
	{LLVM_IR_Xor8, fuResourcesTy(0, 0, 8, 0)},
	{LLVM_IR_Shl8, fuResourcesTy(0, 0, 16, 0)},
	{LLVM_IR_AShr8, fuResourcesTy(0, 0, 16, 0)},
	{LLVM_IR_LShr8, fuResourcesTy(0, 0, 16, 0)},
#endif
#ifdef CUSTOM_OPS
	{LLVM_IR_APAdd, fuResourcesTy(0, 0, 384, 0)},
	{LLVM_IR_APSub, fuResourcesTy(0, 0, 384, 0)},
	{LLVM_IR_APMul, fuResourcesTy(16, 361, 178, 0)},
	{LLVM_IR_APDiv, fuResourcesTy(0, 779, 469, 0)},
#endif
};
#endif

/* We are assuming here that effective frequency will never be above 500 MHz, thus the cases where timing latencies are below 2 ns are excluded */
/* This map format: {key (the operation being considered), {key (latency for completion), in-cycle latency in ns}} */
const std::unordered_map<unsigned, std::map<unsigned, double>> XilinxZCUHardwareProfile::timeConstrainedLatencies = {
	{LLVM_IR_Load, {{2, 1.23}}},
	{LLVM_IR_Store, {{1, 1.23}}},
	{LLVM_IR_Add, {{1, 1.01}}},
	{LLVM_IR_Sub, {{1, 1.01}}},
	{LLVM_IR_Mul, {{1, 3.42}, {2, 2.36}, {3, 2.11}}},
	{LLVM_IR_UDiv, {{36, 1.47}}},
	{LLVM_IR_FAdd, {{1, 15.80}, {2, 12.60}, {3, 10.50}, {4, 6.43}, {5, 5.02}, {6, 4.82}, {7, 4.08}, {8, 3.45}, {10, 2.46}, {11, 2.26}}},
	{LLVM_IR_FSub, {{1, 15.80}, {2, 12.60}, {3, 10.50}, {4, 6.43}, {5, 5.02}, {6, 4.82}, {7, 4.08}, {8, 3.45}, {10, 2.46}, {11, 2.26}}},
	{LLVM_IR_FMul, {{1, 10.50}, {2, 8.41}, {3, 7.01}, {4, 3.79}, {5, 3.17}, {6, 2.56}, {7, 2.41}}},
	{LLVM_IR_FDiv, {
		{1, 54.60}, {2, 43.70}, {3, 36.40}, {4, 33.90}, {5, 17.60}, {6, 12.00}, {7, 9.66}, {8, 8.27}, {9, 7.05}, {10, 5.69}, {12, 4.36},
		{15, 4.34}, {16, 3.03}, {28, 3.01}, {29, 2.91}, {30, 2.23}
	}},
	{LLVM_IR_FCmp, {{1, 3.47}, {2, 2.78}, {3, 2.31}}},
#ifdef CONSTRAIN_INT_OP
	{LLVM_IR_And, {{1, 0.35}}},
	{LLVM_IR_Or, {{1, 0.35}}},
	{LLVM_IR_Xor, {{1, 0.35}}},
	{LLVM_IR_Shl, {{1, 1.38}}},
	{LLVM_IR_AShr, {{1, 1.38}}},
	{LLVM_IR_LShr, {{1, 1.38}}},
#ifdef BYTE_OPS
	{LLVM_IR_Add8, {{1, 0.76}}},
	{LLVM_IR_Sub8, {{1, 0.76}}},
	{LLVM_IR_Mul8, {{1, 1.65}}},
	{LLVM_IR_UDiv8, {{12, 1.87}}},
	{LLVM_IR_And8, {{1, 0.33}}},
	{LLVM_IR_Or8, {{1, 0.33}}},
	{LLVM_IR_Xor8, {{1, 0.33}}},
	{LLVM_IR_Shl8, {{1, 0.74}}},
	{LLVM_IR_AShr8, {{1, 0.74}}},
	{LLVM_IR_LShr8, {{1, 0.74}}},
#endif
#ifdef CUSTOM_OPS
	{LLVM_IR_APAdd, {{1, 2.26}, {2, 1.12}}},
	{LLVM_IR_APSub, {{1, 2.26}, {2, 1.12}}},
	{LLVM_IR_APMul, {{1, 55.3}, {2, 3.68}, {3, 3.41}, {5, 2.61}, {6, 1.81}}},
	{LLVM_IR_APDiv, {{388, 1.56}}},
#endif
#endif
	// XXX: DDR transactions are time-constrained to the effective target period. We set double::max for this
	// XXX: Check the hardware profile constructor for more information about how this is processed (e.g. XilinxZCUHardwareProfile)
	{LLVM_IR_DDRReadReq, {{134, std::numeric_limits<double>::max()}}},
	{LLVM_IR_DDRRead, {{1, std::numeric_limits<double>::max()}}},
	{LLVM_IR_DDRWriteReq, {{1, std::numeric_limits<double>::max()}}},
	{LLVM_IR_DDRWrite, {{1, std::numeric_limits<double>::max()}}},
	{LLVM_IR_DDRWriteResp, {{132, std::numeric_limits<double>::max()}}}
};
const std::unordered_map<unsigned, std::map<unsigned, unsigned>> XilinxZCUHardwareProfile::timeConstrainedDSPs = {
	{LLVM_IR_FAdd, {{1, 2}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}, {7, 2}, {8, 2}, {10, 2}, {11, 2}}},
	{LLVM_IR_FSub, {{1, 2}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}, {7, 2}, {8, 2}, {10, 2}, {11, 2}}},
	{LLVM_IR_FMul, {{1, 3}, {2, 3}, {3, 3}, {4, 3}, {5, 3}, {6, 3}, {7, 3}}},
	{LLVM_IR_FDiv, {
		{1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {10, 0}, {12, 0},
		{15, 0}, {16, 0}, {28, 0}, {29, 0}, {30, 0}
	}},
#ifdef CONSTRAIN_INT_OP
	{LLVM_IR_Add, {{1, 0}}},
	{LLVM_IR_Sub, {{1, 0}}},
	{LLVM_IR_Mul, {{1, 3}, {2, 4}, {3, 4}}},
	{LLVM_IR_UDiv, {{36, 0}}},
	{LLVM_IR_SDiv, {{36, 0}}},
	{LLVM_IR_And, {{1, 0}}},
	{LLVM_IR_Or, {{1, 0}}},
	{LLVM_IR_Xor, {{1, 0}}},
	{LLVM_IR_Shl, {{1, 0}}},
	{LLVM_IR_AShr, {{1, 0}}},
	{LLVM_IR_LShr, {{1, 0}}},
#ifdef BYTE_OPS
	{LLVM_IR_Add8, {{1, 0}}},
	{LLVM_IR_Sub8, {{1, 0}}},
	{LLVM_IR_Mul8, {{1, 0}}},
	{LLVM_IR_UDiv8, {{12, 0}}},
	{LLVM_IR_And8, {{1, 0}}},
	{LLVM_IR_Or8, {{1, 0}}},
	{LLVM_IR_Xor8, {{1, 0}}},
	{LLVM_IR_Shl8, {{1, 0}}},
	{LLVM_IR_AShr8, {{1, 0}}},
	{LLVM_IR_LShr8, {{1, 0}}},
#endif
#ifdef CUSTOM_OPS
	{LLVM_IR_APAdd, {{1, 0}, {2, 0}}},
	{LLVM_IR_APSub, {{1, 0}, {2, 0}}},
	{LLVM_IR_APMul, {{1, 497}, {2, 16}, {3, 16}, {5, 16}, {6, 16}}},
	{LLVM_IR_APDiv, {{388, 0}}},
#endif
#endif
};
const std::unordered_map<unsigned, std::map<unsigned, unsigned>> XilinxZCUHardwareProfile::timeConstrainedFFs = {
	{LLVM_IR_FAdd, {{1, 177}, {2, 177}, {3, 177}, {4, 227}, {5, 205}, {6, 237}, {7, 306}, {8, 296}, {10, 365}, {11, 369}}},
	{LLVM_IR_FSub, {{1, 177}, {2, 177}, {3, 177}, {4, 227}, {5, 205}, {6, 237}, {7, 306}, {8, 296}, {10, 365}, {11, 369}}},
	{LLVM_IR_FMul, {{1, 128}, {2, 128}, {3, 128}, {4, 143}, {5, 151}, {6, 165}, {7, 197}}},
	{LLVM_IR_FDiv, {
		{1, 128}, {2, 128}, {3, 128}, {4, 166}, {5, 218}, {6, 268}, {7, 315}, {8, 363}, {9, 411}, {10, 459}, {12, 563},
		{15, 563}, {16, 761}, {28, 785}, {29, 814}, {30, 1436}
	}},
#ifdef CONSTRAIN_INT_OP
	{LLVM_IR_Add, {{1, 0}}},
	{LLVM_IR_Sub, {{1, 0}}},
	{LLVM_IR_Mul, {{1, 0}, {2, 165}, {3, 166}}},
	{LLVM_IR_UDiv, {{36, 394}}},
	{LLVM_IR_SDiv, {{36, 394}}},
	{LLVM_IR_And, {{1, 0}}},
	{LLVM_IR_Or, {{1, 0}}},
	{LLVM_IR_Xor, {{1, 0}}},
	{LLVM_IR_Shl, {{1, 0}}},
	{LLVM_IR_AShr, {{1, 0}}},
	{LLVM_IR_LShr, {{1, 0}}},
#ifdef BYTE_OPS
	{LLVM_IR_Add8, {{1, 0}}},
	{LLVM_IR_Sub8, {{1, 0}}},
	{LLVM_IR_Mul8, {{1, 0}}},
	{LLVM_IR_UDiv8, {{12, 106}}},
	{LLVM_IR_And8, {{1, 0}}},
	{LLVM_IR_Or8, {{1, 0}}},
	{LLVM_IR_Xor8, {{1, 0}}},
	{LLVM_IR_Shl8, {{1, 0}}},
	{LLVM_IR_AShr8, {{1, 0}}},
	{LLVM_IR_LShr8, {{1, 0}}},
#endif
#ifdef CUSTOM_OPS
	{LLVM_IR_APAdd, {{1, 0}, {2, 580}}},
	{LLVM_IR_APSub, {{1, 0}, {2, 580}}},
	{LLVM_IR_APMul, {{1, 0}, {2, 361}, {3, 362}, {5, 441}, {6, 527}}},
	{LLVM_IR_APDiv, {{388, 779}}},
#endif
#endif
};
const std::unordered_map<unsigned, std::map<unsigned, unsigned>> XilinxZCUHardwareProfile::timeConstrainedLUTs = {
	{LLVM_IR_FAdd, {{1, 194}, {2, 194}, {3, 194}, {4, 214}, {5, 208}, {6, 216}, {7, 246}, {8, 239}, {10, 238}, {11, 236}}},
	{LLVM_IR_FSub, {{1, 194}, {2, 194}, {3, 194}, {4, 214}, {5, 208}, {6, 216}, {7, 246}, {8, 239}, {10, 238}, {11, 236}}},
	{LLVM_IR_FMul, {{1, 135}, {2, 135}, {3, 135}, {4, 139}, {5, 145}, {6, 146}, {7, 123}}},
	{LLVM_IR_FDiv, {
		{1, 755}, {2, 755}, {3, 755}, {4, 755}, {5, 779}, {6, 792}, {7, 792}, {8, 802}, {9, 802}, {10, 799}, {12, 810},
		{15, 807}, {16, 800}, {28, 822}, {29, 812}, {30, 867}
	}},
#ifdef CONSTRAIN_INT_OP
	{LLVM_IR_Add, {{1, 39}}},
	{LLVM_IR_Sub, {{1, 39}}},
	{LLVM_IR_Mul, {{1, 20}, {2, 49}, {3, 49}}},
	{LLVM_IR_UDiv, {{36, 238}}},
	{LLVM_IR_SDiv, {{36, 238}}},
	{LLVM_IR_And, {{1, 32}}},
	{LLVM_IR_Or, {{1, 32}}},
	{LLVM_IR_Xor, {{1, 32}}},
	{LLVM_IR_Shl, {{1, 97}}},
	{LLVM_IR_AShr, {{1, 97}}},
	{LLVM_IR_LShr, {{1, 97}}},
#ifdef BYTE_OPS
	{LLVM_IR_Add8, {{1, 15}}},
	{LLVM_IR_Sub8, {{1, 15}}},
	{LLVM_IR_Mul8, {{1, 40}}},
	{LLVM_IR_UDiv8, {{12, 41}}},
	{LLVM_IR_And8, {{1, 8}}},
	{LLVM_IR_Or8, {{1, 8}}},
	{LLVM_IR_Xor8, {{1, 8}}},
	{LLVM_IR_Shl8, {{1, 16}}},
	{LLVM_IR_AShr8, {{1, 16}}},
	{LLVM_IR_LShr8, {{1, 16}}},
#endif
#ifdef CUSTOM_OPS
	{LLVM_IR_APAdd, {{1, 384}, {2, 130}}},
	{LLVM_IR_APSub, {{1, 384}, {2, 130}}},
	{LLVM_IR_APMul, {{1, 9296}, {2, 178}, {3, 178}, {5, 249}, {6, 179}}},
	{LLVM_IR_APDiv, {{388, 469}}},
#endif
#endif
};

#endif
