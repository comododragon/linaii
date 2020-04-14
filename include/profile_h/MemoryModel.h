#ifndef __MEMORYMODEL_H__
#define __MEMORYMODEL_H__

#include "profile_h/auxiliary.h"
#include "profile_h/boostincls.h"
#include "profile_h/ContextManager.h"
#include "profile_h/DDDGBuilder.h"

// TODO: Improvements for non-perfect loops:
// - Cached values: When burst packing is active, if the whole burst does not properly fit in the bus
//   width, it will leave some values unused. For example if this happens with a read on BEFORE, if
//   this value is loaded again on BETWEEN or AFTER, it could well be avoided since it was loaded before.
//   Of course there are some conditions: For the read@before case, no writes to the same array should
//   happen in inner loops or in any DDDG of this same level (we are being quite conservative here).
//   The same could be applied to stores@after that would waste bus-width due to underuse. In this case
//   you just have to invert the whole logic for store. My proposed approach:
//   - On gen phase, save all bursts (baseAddress and offset) should suffice of all DDDGs in a form of
//     unordered_map<arrayName, <baseAddress, offset>>. Then at use, for example for read@before, when
//     packing the bursts at BEFORE, first check if the current array that we are packing is written to
//     at BEFORE, BETWEEN, AFTER and also inner loops. If positive, we already cancel. If not, then
//     check if at AFTER or BETWEEN there are values that could be cached since there is bug budget
//     remaining. These values cannot be inside a burst though; they have to be isolated as single
//     burst or all together in a single burst (we cannot destroy other bursts inside those DDDGs
//     because of cached values!). If all conditions are met, increase the burst size (i.e. offset)
//     at BEFORE to make any overlapping logic with other transactions from BEFORE valid and we mark
//     that these nodes are going to be cached. Then when we are on BETWEEN/AFTER, right at the
//     beginning of analyseAndTransform(), when nodes are being converted from simple load to
//     LLVM_IR_DDRRead, convert to LLVM_IR_DDRCachedRead instead (latency 0) and keep it separated
//     from the rest of the logic (to avoid creating ReadReq, etc.). That should suffice. The same
//     logic apply to stores but all thoughts inverted;
// - Continuing transactions: Right now DDR transactions are locally analysed or at most out-bursted.
//   If for example there are loads inside the AFTER that could well be a continuation of a burst at
//   BEFORE, Lina does not detect that for now. My proposed approach:
//   - The map generated at the previous item probably should suffice here. For example at "use", 
//     right before analysing out-bursts in the AFTER, check if any current burst could be a continuation
//     of a burst at BEFORE. Then can overlap also: the overlapping reads should be silenced as they
//     are "cached" (and packBursts must translate its beginning to ignore these silenced nodes).
//     Then mark the trnsaction to out-burst; this will make the ReadReq go silent. It is not properly an
//     out-burst but I think that the logic is the same. We just have to make sure that this silenced
//     node is not recognised anywhere as out-bursted (for example, do not export it!). Things complicate
//     when unroll is used because then we have some instances of BETWEEN then. Also, this whole logic is
//     cancelled if any inner loop makes access to this array. Also to simplify, we should cancel also if
//     for example BETWEEN has two overlapping reads (as we still don't know before ASAP/ALAP/RC which
//     will execute first. We still don't know the scheduling!). A same logic could be applied to
//     write, however everything inverted. Remember to follow similar rules as commented in the
//     findOutBursts() method!
// TODO: The current culprit: for both logics to work properly, each DDDG should be pointing to the
// right iteration so that the load/store addresses are properly aligned in a way that searching for
// cacheable or continuous bursts is with valid results. For example, if we unroll a loop with factor 3,
// for correct analysis the DDDGs should be as (i is the iteration index):
// BEFORE[i] (inner loops) AFTER[i]BEFORE[i+1] (inner loops) AFTER[i+1]BEFORE[i+2] (inner loops) AFTER[i+2]
// However this is not the case now. DDDGs are generated just by reading the trace file but for example
// we generate AFTER before the BETWEEN, so this already messes with the iteration ordering. For the type
// of analysis that we propose here, we must ENFORCE the above order to be followed, or we use memoryTraceMap
// to get the proper addresses from the right iterations, however if the code has conditionals, this could
// break execution. So this is why this is a TODO!
// Audio references @ ralder: search for "LINAII REF OPTS NAO IMPLEMENTADAS", lots of audios set 17/01 and 18/01.

class BaseDatapath;

struct ddrInfoTy {
	unsigned loopLevel;
	unsigned datapathType;
	std::set<std::string> arraysLoaded;
	std::set<std::string> arraysStored;

	ddrInfoTy() { }
	ddrInfoTy(unsigned loopLevel, unsigned datapathType, std::set<std::string> arraysLoaded, std::set<std::string> arraysStored) :
		loopLevel(loopLevel), datapathType(datapathType), arraysLoaded(arraysLoaded), arraysStored(arraysStored) { }
};

struct outBurstInfoTy {
	bool canOutBurst;
	bool isRegistered;

	uint64_t baseAddress;
	uint64_t offset;
#ifdef VAR_WSIZE
	uint64_t wordSize;
#endif

	outBurstInfoTy() : canOutBurst(false), isRegistered(false) { }
	outBurstInfoTy(bool canOutBurst) : canOutBurst(canOutBurst), isRegistered(false) { }
};

struct globalOutBurstsInfoTy {
	unsigned loopLevel;
	unsigned datapathType;
	std::unordered_map<std::string, outBurstInfoTy> loadOutBurstsFound;
	std::unordered_map<std::string, outBurstInfoTy> storeOutBurstsFound;

	globalOutBurstsInfoTy() { }
	globalOutBurstsInfoTy(
		unsigned loopLevel, unsigned datapathType,
		std::unordered_map<std::string, outBurstInfoTy> loadOutBurstsFound, std::unordered_map<std::string, outBurstInfoTy> storeOutBurstsFound
	) : loopLevel(loopLevel), datapathType(datapathType), loadOutBurstsFound(loadOutBurstsFound), storeOutBurstsFound(storeOutBurstsFound) { }
};

struct packInfoTy {
	unsigned loopLevel;
	unsigned datapathType;

	std::unordered_map<unsigned, std::pair<unsigned, unsigned>> loadAlignments;
	std::unordered_map<unsigned, std::pair<unsigned, unsigned>> storeAlignments;

	packInfoTy() { }
	packInfoTy(
		unsigned loopLevel, unsigned datapathType,
		std::unordered_map<unsigned, std::pair<unsigned, unsigned>> loadAlignments,
		std::unordered_map<unsigned, std::pair<unsigned, unsigned>> storeAlignments
	) : loopLevel(loopLevel), datapathType(datapathType), loadAlignments(loadAlignments), storeAlignments(storeAlignments) { }
};

class MemoryModel {
protected:
	BaseDatapath *datapath;
	std::vector<int> &microops;
	Graph &graph;
	std::unordered_map<unsigned, Vertex> &nameToVertex;
	VertexNameMap &vertexToName;
	EdgeWeightMap edgeToWeight;
	std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress;
	ConfigurationManager &CM;
	ParsedTraceContainer &PC;
	const std::unordered_map<int, std::pair<int64_t, unsigned>> &memoryTraceList;
	std::string wholeLoopName;
	std::vector<ddrInfoTy> filteredDDRMap;

	bool importedFromContext;
	bool ddrBanking;

public:
	struct nodeExportTy {
		artificialNodeTy node;
		std::string arrayName;
		uint64_t baseAddress;
		uint64_t offset;
#ifdef VAR_WSIZE
		uint64_t wordSize;
#endif

		nodeExportTy() { }
#ifdef VAR_WSIZE
		nodeExportTy(artificialNodeTy node, std::string arrayName, uint64_t baseAddress, uint64_t offset, uint64_t wordSize) :
			node(node), arrayName(arrayName), baseAddress(baseAddress), offset(offset), wordSize(wordSize) { }
#else
		nodeExportTy(artificialNodeTy node, std::string arrayName, uint64_t baseAddress, uint64_t offset) :
			node(node), arrayName(arrayName), baseAddress(baseAddress), offset(offset) { }
#endif
	};

	struct burstInfoTy {
		uint64_t baseAddress;
		// burst size - 1
		uint64_t offset;
#ifdef VAR_WSIZE
		// in bytes
		uint64_t wordSize;
#endif
		std::vector<unsigned> participants;

		burstInfoTy() { }
#ifdef VAR_WSIZE
		burstInfoTy(uint64_t baseAddress, uint64_t offset, uint64_t wordSize, std::vector<unsigned> participants) :
			baseAddress(baseAddress), offset(offset), wordSize(wordSize), participants(participants) { }
#else
		burstInfoTy(uint64_t baseAddress, uint64_t offset, std::vector<unsigned> participants) :
			baseAddress(baseAddress), offset(offset), participants(participants) { }
#endif
	};

	MemoryModel(BaseDatapath *datapath);
	virtual ~MemoryModel() { }
	static MemoryModel *createInstance(BaseDatapath *datapath);
	static bool canOutBurstsOverlap(std::vector<MemoryModel::nodeExportTy> toBefore, std::vector<MemoryModel::nodeExportTy> toAfter);

	virtual void analyseAndTransform() = 0;

	virtual bool tryAllocate(unsigned node, int opcode, bool commit = true) = 0;
	virtual void release(unsigned node, int opcode) = 0;
	virtual bool canOutBurstsOverlap() = 0;
	virtual unsigned getNumOfReadPorts() = 0;
	virtual unsigned getNumOfWritePorts() = 0;

	virtual void setUp(ContextManager &CtxM) = 0;
	virtual void save(ContextManager &CtxM) = 0;
	virtual void importNode(nodeExportTy &exportedNode) = 0;
	virtual std::vector<nodeExportTy> &getNodesToBeforeDDDG() = 0;
	virtual std::vector<nodeExportTy> &getNodesToAfterDDDG() = 0;

	virtual void inheritLoadDepMap(unsigned targetID, unsigned sourceID) = 0;
	virtual void inheritStoreDepMap(unsigned targetID, unsigned sourceID) = 0;
	virtual void addToLoadDepMap(unsigned targetID, unsigned toAddID) = 0;
	virtual void addToStoreDepMap(unsigned targetID, unsigned toAddID) = 0;
	virtual std::pair<std::string, uint64_t> calculateResIIMemRec(std::vector<uint64_t> rcScheduledTime) = 0;
};

class XilinxZCUMemoryModel : public MemoryModel {
	static std::string preprocessedLoopName;
	static std::vector<ddrInfoTy> filteredDDRMap;
	static std::unordered_map<std::string, std::vector<globalOutBurstsInfoTy>>::iterator filteredOutBurstsInfo;
	static bool ddrBanking;
	static std::unordered_map<std::string, unsigned> packSizes;
	static void preprocess(std::string loopName, ConfigurationManager &CM);
	static void blockInvalidOutBursts(
		unsigned loopLevel, unsigned datapathType,
		std::unordered_map<std::string, outBurstInfoTy> &outBurstsFound,
		bool (*analyseOutBurstFeasabilityGlobal)(std::string, unsigned, unsigned)
	);
	static bool analyseLoadOutBurstFeasabilityGlobal(std::string arrayName, unsigned loopLevel, unsigned datapathType);
	static bool analyseStoreOutBurstFeasabilityGlobal(std::string arrayName, unsigned loopLevel, unsigned datapathType);

	std::unordered_map<unsigned, std::pair<std::string, uint64_t>> loadNodes;
	std::unordered_map<unsigned, std::pair<std::string, uint64_t>> storeNodes;
	std::unordered_map<unsigned, burstInfoTy> burstedLoads;
	std::unordered_map<unsigned, burstInfoTy> burstedStores;
	std::unordered_map<unsigned, std::unordered_map<unsigned, std::pair<unsigned, unsigned>>> packInfo;
	std::vector<nodeExportTy> nodesToBeforeDDDG;
	std::vector<nodeExportTy> nodesToAfterDDDG;
	std::unordered_map<std::string, outBurstInfoTy> loadOutBurstsFoundCached;
	std::unordered_map<std::string, outBurstInfoTy> storeOutBurstsFoundCached;
	// This map relates DDR nodes (e.g. ReadReq, WriteReq, WriteResp) to the node ID used in burstedLoad/burstedStores
	std::unordered_map<unsigned, unsigned> ddrNodesToRootLS;
	std::unordered_map<std::string, bool> readActive, writeActive;
	std::unordered_map<std::string, std::set<unsigned>> activeReads;
	std::unordered_map<std::string, std::set<unsigned>> activeWrites;
	std::set<unsigned> importedReadReqs, importedWriteReqs, importedWriteResps;
	std::unordered_map<unsigned, burstInfoTy> importedLoads;
	std::unordered_map<unsigned, burstInfoTy> importedStores;
	std::unordered_map<unsigned, std::pair<std::string, uint64_t>> genFromImpLoadNodes;
	std::unordered_map<unsigned, std::pair<std::string, uint64_t>> genFromImpStoreNodes;
	// Set containing all control edges that do not configure a data dependency
	std::set<std::pair<unsigned, unsigned>> falseDeps;
	// Dependency maps, used for approximating ResMIIMem
	std::unordered_map<unsigned, std::set<unsigned>> loadDepMap;
	std::unordered_map<unsigned, std::set<unsigned>> storeDepMap;

	void findInBursts(
		std::unordered_map<unsigned, std::pair<std::string, uint64_t>> &foundNodes,
		std::vector<unsigned> &behavedNodes,
		std::unordered_map<unsigned, burstInfoTy> &burstedNodes,
		std::function<bool(unsigned, unsigned)> comparator
	);
	bool analyseLoadOutBurstFeasability(unsigned burstID, std::string arrayName);
	bool analyseStoreOutBurstFeasability(unsigned burstID, std::string arrayName);
	std::unordered_map<std::string, outBurstInfoTy> findOutBursts(
		std::unordered_map<unsigned, burstInfoTy> &burstedNodes,
		std::unordered_map<unsigned, std::pair<std::string, uint64_t>> &foundNodes,
		const std::vector<std::string> &instIDList,
		bool (XilinxZCUMemoryModel::*analyseOutBurstFeasability)(unsigned, std::string)
	);
	void packBursts(
		std::unordered_map<unsigned, burstInfoTy> &burstedNodes,
		std::unordered_map<unsigned, std::pair<std::string, uint64_t>> &nodes,
		int silentOpcode = -1, bool nonSilentLast = false
	);

public:
	enum {
		DDR_DATA_BUS_WIDTH = 16
	};

	XilinxZCUMemoryModel(BaseDatapath *datapath);

	void analyseAndTransform();

	bool tryAllocate(unsigned node, int opcode, bool commit = true);
	void release(unsigned node, int opcode);
	bool canOutBurstsOverlap();
	unsigned getNumOfReadPorts() { return 1; }
	unsigned getNumOfWritePorts() { return 1; }

	void setUp(ContextManager &CtxM);
	void save(ContextManager &CtxM);
	void importNode(nodeExportTy &exportedNode);
	std::vector<MemoryModel::nodeExportTy> &getNodesToBeforeDDDG();
	std::vector<MemoryModel::nodeExportTy> &getNodesToAfterDDDG();

	void inheritLoadDepMap(unsigned targetID, unsigned sourceID);
	void inheritStoreDepMap(unsigned targetID, unsigned sourceID);
	void addToLoadDepMap(unsigned targetID, unsigned toAddID);
	void addToStoreDepMap(unsigned targetID, unsigned toAddID);
	std::pair<std::string, uint64_t> calculateResIIMemRec(std::vector<uint64_t> rcScheduledTime);

// TODO Cleanup
//#ifdef DBG_PRINT_ALL
#if 1
	void printDatabase();
#endif
};

#endif
