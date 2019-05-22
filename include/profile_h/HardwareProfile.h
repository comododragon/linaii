#ifndef HARDWAREPROFILE_H
#define HARDWAREPROFILE_H

#include <set>
#include <iostream>

#include "profile_h/auxiliary.h"

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
	unsigned fAddThreshold, fSubThreshold, fMulThreshold, fDivThreshold;
	bool isConstrained;
	bool thresholdSet;
	std::set<int> limitedBy;

public:
	enum {
		LIMITED_BY_FADD,
		LIMITED_BY_FSUB,
		LIMITED_BY_FMUL,
		LIMITED_BY_FDIV
	};

	virtual ~HardwareProfile() { }
	static HardwareProfile *createInstance();
	virtual void clear();

	virtual unsigned getLatency(unsigned opcode) = 0;
	virtual unsigned getSchedulingLatency(unsigned opcode) = 0;
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

	std::set<int> getConstrainedUnits() { return limitedBy; }

	virtual void arrayAddPartition(std::string arrayName) = 0;
	virtual bool fAddAddUnit() = 0;
	virtual bool fSubAddUnit() = 0;
	virtual bool fMulAddUnit() = 0;
	virtual bool fDivAddUnit() = 0;

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

	bool fAddTryAllocate();
	bool fSubTryAllocate();
	bool fMulTryAllocate();
	bool fDivTryAllocate();
	bool fCmpTryAllocate();
	bool loadTryAllocate(std::string arrayPartitionName);
	bool storeTryAllocate(std::string arrayPartitionName);
	bool intOpTryAllocate(unsigned opcode);
	bool callTryAllocate();

	void fAddRelease();
	void fSubRelease();
	void fMulRelease();
	void fDivRelease();
	void fCmpRelease();
	void loadRelease(std::string arrayPartitionName);
	void storeRelease(std::string arrayPartitionName);
	void intOpRelease(unsigned opcode);
	void callRelease();

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
	// TODO: Couldn't we just glue the two enums below?
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

public:
	void clear();

	virtual unsigned getLatency(unsigned opcode);
	virtual unsigned getSchedulingLatency(unsigned opcode);
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

	void arrayAddPartition(std::string arrayName);
	void arrayAddPartitions(std::string arrayName, unsigned amount);
	bool fAddAddUnit();
	bool fSubAddUnit();
	bool fMulAddUnit();
	bool fDivAddUnit();

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

class XilinxZCU102HardwareProfile : public XilinxHardwareProfile {
	enum {
		MAX_DSP = 2520,
		MAX_FF = 548160,
		MAX_LUT = 274080,
		// XXX: This device has BRAM36k units, that can be configured to be used as 2 BRAM18k units.
		// XXX: I'm not sure if this affects the current implementation of Lin-analyzer,
		// XXX: but it's good to know about this fact
		MAX_BRAM18K = 1824
	};


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
		LATENCY_FCMP
	};

	/* We are assuming here that effective frequency will never be above 500 MHz, thus the cases where timing latencies are below 2 ns are excluded */
	/* This map format: {key (the operation being considered), {key (latency for completion), in-cycle latency in ns}} */
	const std::unordered_map<unsigned, std::map<unsigned, double>> timeConstrainedLatencies = {
		{LATENCY_LOAD, {{1, 1.23}}},
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
		{LATENCY_FCMP, {{1, 3.47}, {2, 2.78}, {3, 2.31}}}
	};

	double effectivePeriod;
	std::unordered_map<unsigned, std::pair<unsigned, double>> effectiveLatencies;

public:
	XilinxZCU102HardwareProfile();
	void setResourceLimits();
	unsigned getLatency(unsigned opcode);
};

#endif
