#ifndef HARDWAREPROFILE_H
#define HARDWAREPROFILE_H

#include <set>

#include "profile_h/auxiliary.h"

using namespace llvm;

class HardwareProfile {
protected:
	const unsigned INFINITE_RESOURCES = 999999999;

	std::map<std::string, unsigned> arrayNameToNumOfPartitions;
	unsigned fAddCount, fSubCount, fMulCount, fDivCount;
	unsigned fAddThreshold, fSubThreshold, fMulThreshold, fDivThreshold;
	const ConfigurationManager::arrayInfoCfgMapTy *arrayInfoCfgMap;
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
	virtual void calculateRequiredResources(
		std::vector<int> &microops,
		const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
		std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress,
		std::map<uint64_t, std::vector<unsigned>> &maxTimesNodesMap
	) = 0;
	virtual void setResourceLimits() = 0;
	virtual void setThresholdWithCurrentUsage() = 0;
	virtual void constrainHardware(const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap);

	std::set<int> getConstrainedUnits() { return limitedBy; }

	virtual void arrayAddPartition(std::string arrayName) = 0;
	virtual unsigned arrayGetNumOfPartitions(std::string arrayName) = 0;
	virtual void fAddAddUnit() = 0;
	unsigned fAddGetAmount() { return fAddCount; }
	virtual void fSubAddUnit() = 0;
	unsigned fSubGetAmount() { return fSubCount; }
	virtual void fMulAddUnit() = 0;
	unsigned fMulGetAmount() { return fMulCount; }
	virtual void fDivAddUnit() = 0;
	unsigned fDivGetAmount() { return fDivCount; }
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
	unsigned maxDSP, maxFF, maxLUT, maxBRAM18k;
	unsigned usedDSP, usedFF, usedLUT, usedBRAM18k;

public:
	void clear();

	unsigned getLatency(unsigned opcode);
	void calculateRequiredResources(
		std::vector<int> &microops,
		const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
		std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress,
		std::map<uint64_t, std::vector<unsigned>> &maxTimesNodesMap
	);
	void setThresholdWithCurrentUsage();
	void setBRAM18kUsage();
	void constrainHardware(const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap);

	void arrayAddPartition(std::string arrayName);
	unsigned arrayGetNumOfPartitions(std::string arrayName);
	void fAddAddUnit();
	void fSubAddUnit();
	void fMulAddUnit();
	void fDivAddUnit();

	unsigned resourcesGetDSPs() { return usedDSP; }
	unsigned resourcesGetFFs() { return usedFF; }
	unsigned resourcesGetLUTs() { return usedLUT; }
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

#endif
