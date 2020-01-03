#ifndef __MEMORYMODEL_H__
#define __MEMORYMODEL_H__

#include "profile_h/auxiliary.h"
#include "profile_h/boostincls.h"
#include "profile_h/DDDGBuilder.h"

class BaseDatapath;

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

	virtual void analyseAndTransform() = 0;

	virtual bool tryAllocate(unsigned node, int opcode, bool commit = true) = 0;
	virtual void release(unsigned node, int opcode) = 0;
	virtual bool outBurstsOverlap() = 0;

	virtual void importNode(nodeExportTy &exportedNode) = 0;
	virtual std::vector<nodeExportTy> &getNodesToBeforeDDDG() = 0;
	virtual std::vector<nodeExportTy> &getNodesToAfterDDDG() = 0;
};

class XilinxZCUMemoryModel : public MemoryModel {
	std::unordered_map<unsigned, std::pair<std::string, uint64_t>> loadNodes;
	std::unordered_map<unsigned, std::pair<std::string, uint64_t>> storeNodes;
	std::unordered_map<unsigned, burstInfoTy> burstedLoads;
	std::unordered_map<unsigned, burstInfoTy> burstedStores;
	std::vector<nodeExportTy> nodesToBeforeDDDG;
	std::vector<nodeExportTy> nodesToAfterDDDG;
	bool loadOutBurstFound, storeOutBurstFound;
	// This map relates DDR nodes (e.g. ReadReq, WriteReq, WriteResp) to the node ID used in burstedLoad/burstedStores
	std::unordered_map<unsigned, unsigned> ddrNodesToRootLS;
	std::unordered_map<std::string, bool> readActive, writeActive;
	std::unordered_map<std::string, std::set<unsigned>> activeReads;
	std::unordered_map<std::string, std::set<unsigned>> activeWrites;
	//unsigned activeWrite;
	//unsigned completedTransactions;
	bool readReqImported, writeReqImported, writeRespImported;
	unsigned importedWriteReq, importedWriteResp;
	std::unordered_map<unsigned, burstInfoTy> importedLoads;
	std::unordered_map<unsigned, burstInfoTy> importedStores;
	std::unordered_map<unsigned, std::pair<std::string, uint64_t>> genFromImpLoadNodes;
	std::unordered_map<unsigned, std::pair<std::string, uint64_t>> genFromImpStoreNodes;
	//bool allUnimportedComplete, importedWriteRespComplete;

	void findInBursts(
		std::unordered_map<unsigned, std::pair<std::string, uint64_t>> &foundNodes,
		std::vector<unsigned> &behavedNodes,
		std::unordered_map<unsigned, burstInfoTy> &burstedNodes,
		std::function<bool(unsigned, unsigned)> comparator
	);
	bool findOutBursts(
		std::unordered_map<unsigned, burstInfoTy> &burstedNodes,
		std::string &wholeLoopName,
		const std::vector<std::string> &instIDList
	);
	void packBursts(
		std::unordered_map<unsigned, burstInfoTy> &burstedNodes,
		std::unordered_map<unsigned, std::pair<std::string, uint64_t>> &nodes,
		int silentOpcode, bool nonSilentLast = false
	);

public:
	enum {
		DDR_DATA_BUS_WIDTH = 16
	};

	XilinxZCUMemoryModel(BaseDatapath *datapath);

	void analyseAndTransform();

	bool tryAllocate(unsigned node, int opcode, bool commit = true);
	void release(unsigned node, int opcode);
	bool outBurstsOverlap();

	void importNode(nodeExportTy &exportedNode);
	std::vector<MemoryModel::nodeExportTy> &getNodesToBeforeDDDG();
	std::vector<MemoryModel::nodeExportTy> &getNodesToAfterDDDG();

// TODO TODO TODO TODO
// TODO TODO TODO TODO
// TODO TODO TODO TODO
// TODO TODO TODO TODO
//#ifdef DBG_PRINT_ALL
#if 1
	void printDatabase();
#endif
};

#endif
