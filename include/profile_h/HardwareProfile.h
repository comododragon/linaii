#ifndef HARDWAREPROFILE_H
#define HARDWAREPROFILE_H

#include <set>
#include <iostream>

#include "profile_h/auxiliary.h"
#include "profile_h/MemoryModel.h"

using namespace llvm;

class HardwareProfile {
protected:
	const unsigned INFINITE_RESOURCES = 999999999;

	std::map<std::string, std::tuple<uint64_t, uint64_t, size_t>> arrayNameToConfig;
	std::map<std::string, unsigned> arrayNameToNumOfPartitions;
	std::map<std::string, unsigned> arrayNameToWritePortsPerPartition;
	std::map<std::string, float> arrayNameToEfficiency;
	std::map<std::string, unsigned> arrayPartitionToReadPorts;
	std::map<std::string, unsigned> arrayPartitionToReadPortsInUse;
	std::map<std::string, unsigned> arrayPartitionToWritePorts;
	std::map<std::string, unsigned> arrayPartitionToWritePortsInUse;
	unsigned fAddCount, fSubCount, fMulCount, fDivCount;
	unsigned unrFAddCount, unrFSubCount, unrFMulCount, unrFDivCount;
	unsigned fAddInUse, fSubInUse, fMulInUse, fDivInUse;
	bool ddrReadPortInUse, ddrWritePortInUse;
	unsigned fAddThreshold, fSubThreshold, fMulThreshold, fDivThreshold;
	bool isConstrained;
	bool thresholdSet;
	std::set<int> limitedBy;

	MemoryModel *memmodel;

public:
	enum {
		LIMITED_BY_FADD,
		LIMITED_BY_FSUB,
		LIMITED_BY_FMUL,
		LIMITED_BY_FDIV
	};

	HardwareProfile();
	virtual ~HardwareProfile() { }
	static HardwareProfile *createInstance();
	void setMemoryModel(MemoryModel *memmodel);
	virtual void clear();

	void performMemoryModelAnalysis();
	virtual unsigned getLatency(unsigned opcode) = 0;
	virtual double getInCycleLatency(unsigned opcode) = 0;
	virtual bool isPipelined(unsigned opcode) = 0;
	virtual void calculateRequiredResources(
		std::vector<int> &microops,
		const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
		std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress,
		std::map<uint64_t, std::vector<unsigned>> &maxTimesNodesMap
	) = 0;
	virtual void setResourceLimits() = 0;
	virtual void setThresholdWithCurrentUsage() = 0;
	virtual void setMemoryCurrentUsage(
		const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
		const ConfigurationManager::partitionCfgMapTy &partitionCfgMap,
		const ConfigurationManager::partitionCfgMapTy &completePartitionCfgMap
	) = 0;
	virtual void constrainHardware(
		const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
		const ConfigurationManager::partitionCfgMapTy &partitionCfgMap,
		const ConfigurationManager::partitionCfgMapTy &completePartitionCfgMap
	);
	std::tuple<std::string, uint64_t> calculateResIIOp();

	virtual void fillPack(Pack &P);
	std::set<int> getConstrainedUnits() { return limitedBy; }

	virtual void arrayAddPartition(std::string arrayName) = 0;
	virtual bool fAddAddUnit(bool commit = true) = 0;
	virtual bool fSubAddUnit(bool commit = true) = 0;
	virtual bool fMulAddUnit(bool commit = true) = 0;
	virtual bool fDivAddUnit(bool commit = true) = 0;

	unsigned arrayGetNumOfPartitions(std::string arrayName);
	unsigned arrayGetPartitionReadPorts(std::string partitionName);
	unsigned arrayGetPartitionWritePorts(std::string partitionName);
	const std::map<std::string, std::tuple<uint64_t, uint64_t, size_t>> &arrayGetConfig() { return arrayNameToConfig; }
	const std::map<std::string, unsigned> &arrayGetNumOfPartitions() { return arrayNameToNumOfPartitions; }
	const std::map<std::string, float> &arrayGetEfficiency() { return arrayNameToEfficiency; }
	virtual unsigned arrayGetMaximumWritePortsPerPartition() = 0;
	unsigned fAddGetAmount() { return fAddCount; }
	unsigned fSubGetAmount() { return fSubCount; }
	unsigned fMulGetAmount() { return fMulCount; }
	unsigned fDivGetAmount() { return fDivCount; }

	bool fAddTryAllocate(bool commit = true);
	bool fSubTryAllocate(bool commit = true);
	bool fMulTryAllocate(bool commit = true);
	bool fDivTryAllocate(bool commit = true);
	bool fCmpTryAllocate(bool commit = true);
	bool loadTryAllocate(std::string arrayPartitionName, bool commit = true);
	bool storeTryAllocate(std::string arrayPartitionName, bool commit = true);
	bool intOpTryAllocate(int opcode, bool commit = true);
	bool callTryAllocate(bool commit = true);
	bool ddrOpTryAllocate(unsigned node, int opcode, bool commit = true);

	void pipelinedRelease();
	void fAddRelease();
	void fSubRelease();
	void fMulRelease();
	void fDivRelease();
	void fCmpRelease();
	void loadRelease(std::string arrayPartitionName);
	void storeRelease(std::string arrayPartitionName);
	void intOpRelease(int opcode);
	void callRelease();
	void ddrOpRelease(unsigned node, int opcode);
};

class XilinxHardwareProfile : public HardwareProfile {
	enum {
		LATENCY_LOAD = 2,
		LATENCY_STORE = 1,
		LATENCY_ADD = 1,
		LATENCY_SUB = 1,
		LATENCY_MUL32 = 6,
		LATENCY_DIV32 = 36,
		LATENCY_FADD32 = 5,
		LATENCY_FSUB32 = 5,
		LATENCY_FMUL32 = 4,
		LATENCY_FDIV32 = 16,
		LATENCY_FCMP = 1
	};
	enum {
		SCHEDULING_LATENCY_LOAD = 1
	};
	enum {
		BRAM_PORTS_R = 2,
		BRAM_PORTS_W = 1
	};
	enum {
		PER_PARTITION_PORTS_R = 2,
		PER_PARTITION_PORTS_W = 1,
		PER_PARTITION_MAX_PORTS_W = 2
	};
	enum {
		DSP_FADD = 2,
		DSP_FSUB = 2,
		DSP_FMUL = 3,
		DSP_FDIV = 0
	};
	enum {
		FF_FADD = 205,
		FF_FSUB = 205,
		FF_FMUL = 143,
		FF_FDIV = 761
	};
	enum {
		LUT_FADD = 390,
		LUT_FSUB = 390,
		LUT_FMUL = 321,
		LUT_FDIV = 994
	};

protected:
	std::map<std::string, unsigned> arrayNameToUsedBRAM18k;
	unsigned maxDSP, maxFF, maxLUT, maxBRAM18k;
	unsigned usedDSP, usedFF, usedLUT, usedBRAM18k;
	unsigned fAddDSP, fAddFF, fAddLUT;
	unsigned fSubDSP, fSubFF, fSubLUT;
	unsigned fMulDSP, fMulFF, fMulLUT;
	unsigned fDivDSP, fDivFF, fDivLUT;

public:
	XilinxHardwareProfile();

	void clear();

	virtual unsigned getLatency(unsigned opcode);
	virtual double getInCycleLatency(unsigned opcode);
	bool isPipelined(unsigned opcode);
	void calculateRequiredResources(
		std::vector<int> &microops,
		const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
		std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress,
		std::map<uint64_t, std::vector<unsigned>> &maxTimesNodesMap
	);
	void setThresholdWithCurrentUsage();
	void setMemoryCurrentUsage(
		const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
		const ConfigurationManager::partitionCfgMapTy &partitionCfgMap,
		const ConfigurationManager::partitionCfgMapTy &completePartitionCfgMap
	);
	void constrainHardware(
		const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
		const ConfigurationManager::partitionCfgMapTy &partitionCfgMap,
		const ConfigurationManager::partitionCfgMapTy &completePartitionCfgMap
	);

	void fillPack(Pack &P);

	void arrayAddPartition(std::string arrayName);
	void arrayAddPartitions(std::string arrayName, unsigned amount);
	bool fAddAddUnit(bool commit = true);
	bool fSubAddUnit(bool commit = true);
	bool fMulAddUnit(bool commit = true);
	bool fDivAddUnit(bool commit = true);

	unsigned arrayGetMaximumWritePortsPerPartition();
	std::map<std::string, unsigned> arrayGetUsedBRAM18k() { return arrayNameToUsedBRAM18k; }

	unsigned resourcesGetDSPs() { return usedDSP; }
	unsigned resourcesGetFFs() { return usedFF; }
	unsigned resourcesGetLUTs() { return usedLUT; }
	unsigned resourcesGetBRAM18k() { return usedBRAM18k; }
};

class XilinxVC707HardwareProfile : public XilinxHardwareProfile {
	enum {
		MAX_DSP = 2800,
		MAX_FF = 607200,
		MAX_LUT = 303600,
		MAX_BRAM18K = 2060
	};

public:
	void setResourceLimits();
};

class XilinxZC702HardwareProfile : public XilinxHardwareProfile {
	enum {
		MAX_DSP = 220,
		MAX_FF = 106400,
		MAX_LUT = 53200,
		MAX_BRAM18K = 280
	};

public:
	void setResourceLimits();
};

class XilinxZCUHardwareProfile : public XilinxHardwareProfile {
	enum {
		LATENCY_LOAD,
		LATENCY_STORE,
		LATENCY_ADD,
		LATENCY_SUB,
		LATENCY_MUL32,
		LATENCY_DIV32,
		LATENCY_FADD32,
		LATENCY_FSUB32,
		LATENCY_FMUL32,
		LATENCY_FDIV32,
		LATENCY_FCMP,
		LATENCY_DDRREADREQ,
		LATENCY_DDRREAD,
		LATENCY_DDRWRITEREQ,
		LATENCY_DDRWRITE,
		LATENCY_DDRWRITERESP
	};

	/* We are assuming here that effective frequency will never be above 500 MHz, thus the cases where timing latencies are below 2 ns are excluded */
	/* This map format: {key (the operation being considered), {key (latency for completion), in-cycle latency in ns}} */
	const std::unordered_map<unsigned, std::map<unsigned, double>> timeConstrainedLatencies = {
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
	const std::unordered_map<unsigned, std::map<unsigned, unsigned>> timeConstrainedDSPs = {
		{LATENCY_FADD32, {{1, 2}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}, {7, 2}, {8, 2}, {10, 2}, {11, 2}}},
		{LATENCY_FSUB32, {{1, 2}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}, {7, 2}, {8, 2}, {10, 2}, {11, 2}}},
		{LATENCY_FMUL32, {{1, 3}, {2, 3}, {3, 3}, {4, 3}, {5, 3}, {6, 3}, {7, 3}}},
		{LATENCY_FDIV32, {
			{1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {10, 0}, {12, 0},
			{15, 0}, {16, 0}, {28, 0}, {29, 0}, {30, 0}
		}}
	};
	const std::unordered_map<unsigned, std::map<unsigned, unsigned>> timeConstrainedFFs = {
		{LATENCY_FADD32, {{1, 177}, {2, 177}, {3, 177}, {4, 227}, {5, 205}, {6, 237}, {7, 306}, {8, 296}, {10, 365}, {11, 369}}},
		{LATENCY_FSUB32, {{1, 177}, {2, 177}, {3, 177}, {4, 227}, {5, 205}, {6, 237}, {7, 306}, {8, 296}, {10, 365}, {11, 369}}},
		{LATENCY_FMUL32, {{1, 128}, {2, 128}, {3, 128}, {4, 143}, {5, 151}, {6, 165}, {7, 197}}},
		{LATENCY_FDIV32, {
			{1, 128}, {2, 128}, {3, 128}, {4, 166}, {5, 218}, {6, 268}, {7, 315}, {8, 363}, {9, 411}, {10, 459}, {12, 563},
			{15, 563}, {16, 761}, {28, 785}, {29, 814}, {30, 1436}
		}}
	};
	const std::unordered_map<unsigned, std::map<unsigned, unsigned>> timeConstrainedLUTs = {
		{LATENCY_FADD32, {{1, 194}, {2, 194}, {3, 194}, {4, 214}, {5, 208}, {6, 216}, {7, 246}, {8, 239}, {10, 238}, {11, 236}}},
		{LATENCY_FSUB32, {{1, 194}, {2, 194}, {3, 194}, {4, 214}, {5, 208}, {6, 216}, {7, 246}, {8, 239}, {10, 238}, {11, 236}}},
		{LATENCY_FMUL32, {{1, 135}, {2, 135}, {3, 135}, {4, 139}, {5, 145}, {6, 146}, {7, 123}}},
		{LATENCY_FDIV32, {
			{1, 755}, {2, 755}, {3, 755}, {4, 755}, {5, 779}, {6, 792}, {7, 792}, {8, 802}, {9, 802}, {10, 799}, {12, 810},
			{15, 807}, {16, 800}, {28, 822}, {29, 812}, {30, 867}
		}}
	};

	double effectivePeriod;
	std::unordered_map<unsigned, std::pair<unsigned, double>> effectiveLatencies;

public:
	XilinxZCUHardwareProfile();
	unsigned getLatency(unsigned opcode);
	double getInCycleLatency(unsigned opcode);
};

class XilinxZCU102HardwareProfile : public XilinxZCUHardwareProfile {
	enum {
		MAX_DSP = 2520,
		MAX_FF = 548160,
		MAX_LUT = 274080,
		// XXX: This device has BRAM36k units, that can be configured to be used as 2 BRAM18k units.
		MAX_BRAM18K = 1824
	};

public:
	XilinxZCU102HardwareProfile() { }
	void setResourceLimits();
};

class XilinxZCU104HardwareProfile : public XilinxZCUHardwareProfile {
	enum {
		MAX_DSP = 1728,
		MAX_FF = 460800,
		MAX_LUT = 230400,
		// XXX: This device has BRAM36k units, that can be configured to be used as 2 BRAM18k units.
		MAX_BRAM18K = 624
	};

public:
	XilinxZCU104HardwareProfile() { }
	void setResourceLimits();
};

#endif
