#ifndef __MEMORYMODEL_H__
#define __MEMORYMODEL_H__

#include "profile_h/auxiliary.h"
#include "profile_h/boostincls.h"
#include "profile_h/DDDGBuilder.h"

typedef std::tuple<int, std::string, int, std::string, std::string> nodeExportTy;

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

public:
	MemoryModel(BaseDatapath *datapath);
	virtual ~MemoryModel() { }
	static MemoryModel *createInstance(BaseDatapath *datapath);

	virtual void analyseAndTransform() = 0;

	virtual bool tryAllocate(unsigned node, int opcode, bool commit = true) = 0;
	virtual void release(unsigned node, int opcode) = 0;
	virtual bool outBurstsOverlap() = 0;

	virtual void importNode(unsigned nodeID, int opcode) = 0;
	virtual std::vector<nodeExportTy> &getNodesToBeforeDDDG() = 0;
	virtual std::vector<nodeExportTy> &getNodesToAfterDDDG() = 0;
};

class XilinxZCUMemoryModel : public MemoryModel {
	std::unordered_map<unsigned, uint64_t> loadNodes;
	std::unordered_map<unsigned, uint64_t> storeNodes;
	std::unordered_map<unsigned, std::tuple<uint64_t, uint64_t, std::vector<unsigned>>> burstedLoads;
	std::unordered_map<unsigned, std::tuple<uint64_t, uint64_t, std::vector<unsigned>>> burstedStores;
	std::vector<nodeExportTy> nodesToBeforeDDDG;
	std::vector<nodeExportTy> nodesToAfterDDDG;
	bool loadOutBurstFound, storeOutBurstFound;
	// This map relates DDR nodes (e.g. ReadReq, WriteReq, WriteResp) to the node ID used in burstedLoad/burstedStores
	std::unordered_map<unsigned, unsigned> ddrNodesToRootLS;
	bool readActive, writeActive;
	std::set<unsigned> activeReads;
	unsigned activeWrite;
	unsigned completedTransactions;
	bool readReqImported, writeReqImported, writeRespImported;
	unsigned importedReadReq, importedWriteReq, importedWriteResp;
	bool allUnimportedComplete, importedWriteRespComplete;

	void findInBursts(
		std::unordered_map<unsigned, uint64_t> &foundNodes,
		std::vector<unsigned> &behavedNodes,
		std::unordered_map<unsigned, std::tuple<uint64_t, uint64_t, std::vector<unsigned>>> &burstedNodes,
		std::function<bool(unsigned, unsigned)> comparator
	);
	bool findOutBursts(
		std::unordered_map<unsigned, std::tuple<uint64_t, uint64_t, std::vector<unsigned>>> &burstedNodes,
		unsigned &noOfBurstedNodes,
		std::string &wholeLoopName,
		const std::vector<std::string> &instIDList
	);

public:
	XilinxZCUMemoryModel(BaseDatapath *datapath);

	void analyseAndTransform();

	bool tryAllocate(unsigned node, int opcode, bool commit = true);
	void release(unsigned node, int opcode);
	bool outBurstsOverlap();

	void importNode(unsigned nodeID, int opcode);
	std::vector<nodeExportTy> &getNodesToBeforeDDDG();
	std::vector<nodeExportTy> &getNodesToAfterDDDG();

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
