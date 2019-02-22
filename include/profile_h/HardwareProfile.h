#ifndef HARDWAREPROFILE_H
#define HARDWAREPROFILE_H

#include "profile_h/auxiliary.h"

using namespace llvm;

class HardwareProfile {
protected:
	std::map<std::string, unsigned> arrayNameToNumOfPartitions;
	unsigned fAddCount, fSubCount, fMulCount, fDivCount;

public:
	virtual ~HardwareProfile() { }
	static HardwareProfile *createInstance();
	virtual void clear() = 0;

	virtual unsigned getLatency(unsigned opcode) = 0;
	virtual void calculateRequiredResources(
		std::vector<int> &microops,
		const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
		std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress,
		std::map<uint64_t, std::vector<unsigned>> &maxTimesNodesMap
	) = 0;

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

	unsigned usedDSP, usedFF, usedLUT;

public:
	void clear();

	unsigned getLatency(unsigned opcode);
	void calculateRequiredResources(
		std::vector<int> &microops,
		const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap,
		std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress,
		std::map<uint64_t, std::vector<unsigned>> &maxTimesNodesMap
	);

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
};

class XilinxZC702HardwareProfile : public XilinxHardwareProfile {
};

#endif
