#ifndef HARDWAREPROFILEPARAMS_CPP
#define HARDWAREPROFILEPARAMS_CPP

#include "profile_h/HardwareProfile.h"

/* We are assuming here that effective frequency will never be above 500 MHz, thus the cases where timing latencies are below 2 ns are excluded */
/* This map format: {key (the operation being considered), {key (latency for completion), in-cycle latency in ns}} */
const std::unordered_map<unsigned, std::map<unsigned, double>> XilinxZCUHardwareProfile::timeConstrainedLatencies = {
	{LATENCY_LOAD, {{2, 1.23}}},
	{LATENCY_STORE, {{1, 1.23}}},
	{LATENCY_ADD, {{1, 1.01}}},
	{LATENCY_SUB, {{1, 1.01}}},
	{LATENCY_MUL32, {{1, 3.42}, {2, 2.36}, {3, 2.11}}},
	{LATENCY_DIV32, {{36, 1.47}}},
	{LATENCY_FADD32, {{1, 15.80}, {2, 12.60}, {3, 10.50}, {4, 6.43}, {5, 5.02}, {6, 4.82}, {7, 4.08}, {8, 3.45}, {10, 2.46}, {11, 2.26}}},
	{LATENCY_FSUB32, {{1, 15.80}, {2, 12.60}, {3, 10.50}, {4, 6.43}, {5, 5.02}, {6, 4.82}, {7, 4.08}, {8, 3.45}, {10, 2.46}, {11, 2.26}}},
	{LATENCY_FMUL32, {{1, 10.50}, {2, 8.41}, {3, 7.01}, {4, 3.79}, {5, 3.17}, {6, 2.56}, {7, 2.41}}},
	{LATENCY_FDIV32, {
		{1, 54.60}, {2, 43.70}, {3, 36.40}, {4, 33.90}, {5, 17.60}, {6, 12.00}, {7, 9.66}, {8, 8.27}, {9, 7.05}, {10, 5.69}, {12, 4.36},
		{15, 4.34}, {16, 3.03}, {28, 3.01}, {29, 2.91}, {30, 2.23}
	}},
	{LATENCY_FCMP, {{1, 3.47}, {2, 2.78}, {3, 2.31}}},
	// XXX: DDR transactions are time-constrained to the effective target period. We set double::max for this
	// XXX: Check the hardware profile constructor for more information about how this is processed (e.g. XilinxZCUHardwareProfile)
	{LATENCY_DDRREADREQ, {{134, std::numeric_limits<double>::max()}}},
	{LATENCY_DDRREAD, {{1, std::numeric_limits<double>::max()}}},
	{LATENCY_DDRWRITEREQ, {{1, std::numeric_limits<double>::max()}}},
	{LATENCY_DDRWRITE, {{1, std::numeric_limits<double>::max()}}},
	{LATENCY_DDRWRITERESP, {{132, std::numeric_limits<double>::max()}}}
};
const std::unordered_map<unsigned, std::map<unsigned, unsigned>> XilinxZCUHardwareProfile::timeConstrainedDSPs = {
	{LATENCY_FADD32, {{1, 2}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}, {7, 2}, {8, 2}, {10, 2}, {11, 2}}},
	{LATENCY_FSUB32, {{1, 2}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}, {7, 2}, {8, 2}, {10, 2}, {11, 2}}},
	{LATENCY_FMUL32, {{1, 3}, {2, 3}, {3, 3}, {4, 3}, {5, 3}, {6, 3}, {7, 3}}},
	{LATENCY_FDIV32, {
		{1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {10, 0}, {12, 0},
		{15, 0}, {16, 0}, {28, 0}, {29, 0}, {30, 0}
	}}
};
const std::unordered_map<unsigned, std::map<unsigned, unsigned>> XilinxZCUHardwareProfile::timeConstrainedFFs = {
	{LATENCY_FADD32, {{1, 177}, {2, 177}, {3, 177}, {4, 227}, {5, 205}, {6, 237}, {7, 306}, {8, 296}, {10, 365}, {11, 369}}},
	{LATENCY_FSUB32, {{1, 177}, {2, 177}, {3, 177}, {4, 227}, {5, 205}, {6, 237}, {7, 306}, {8, 296}, {10, 365}, {11, 369}}},
	{LATENCY_FMUL32, {{1, 128}, {2, 128}, {3, 128}, {4, 143}, {5, 151}, {6, 165}, {7, 197}}},
	{LATENCY_FDIV32, {
		{1, 128}, {2, 128}, {3, 128}, {4, 166}, {5, 218}, {6, 268}, {7, 315}, {8, 363}, {9, 411}, {10, 459}, {12, 563},
		{15, 563}, {16, 761}, {28, 785}, {29, 814}, {30, 1436}
	}}
};
const std::unordered_map<unsigned, std::map<unsigned, unsigned>> XilinxZCUHardwareProfile::timeConstrainedLUTs = {
	{LATENCY_FADD32, {{1, 194}, {2, 194}, {3, 194}, {4, 214}, {5, 208}, {6, 216}, {7, 246}, {8, 239}, {10, 238}, {11, 236}}},
	{LATENCY_FSUB32, {{1, 194}, {2, 194}, {3, 194}, {4, 214}, {5, 208}, {6, 216}, {7, 246}, {8, 239}, {10, 238}, {11, 236}}},
	{LATENCY_FMUL32, {{1, 135}, {2, 135}, {3, 135}, {4, 139}, {5, 145}, {6, 146}, {7, 123}}},
	{LATENCY_FDIV32, {
		{1, 755}, {2, 755}, {3, 755}, {4, 755}, {5, 779}, {6, 792}, {7, 792}, {8, 802}, {9, 802}, {10, 799}, {12, 810},
		{15, 807}, {16, 800}, {28, 822}, {29, 812}, {30, 867}
	}}
};

#endif
