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

public:
	MemoryModel(BaseDatapath *datapath);
	virtual ~MemoryModel() { }
	static MemoryModel *createInstance(BaseDatapath *datapath);

	virtual void analyseAndTransform() = 0;

	virtual bool tryAllocate(unsigned node, int opcode, bool commit = true) = 0;
	virtual void release(unsigned node, int opcode) = 0;
};

class XilinxZCUMemoryModel : public MemoryModel {
	std::unordered_map<unsigned, uint64_t> loadNodes;
	std::unordered_map<unsigned, uint64_t> storeNodes;
	std::unordered_map<unsigned, std::tuple<uint64_t, uint64_t, std::vector<unsigned>>> burstedLoads;
	std::unordered_map<unsigned, std::tuple<uint64_t, uint64_t, std::vector<unsigned>>> burstedStores;
	bool loadOutBurstFound, storeOutBurstFound;
	// This map relates DDR nodes (e.g. ReadReq, WriteReq, WriteResp) to the node ID used in burstedLoad/burstedStores
	std::unordered_map<unsigned, unsigned> ddrNodesToRootLS;
	bool readActive, writeActive;
	std::set<unsigned> activeReads;
	unsigned activeWrite;

	void findInBursts(
		std::unordered_map<unsigned, uint64_t> &foundNodes,
		std::vector<unsigned> &behavedNodes,
		std::unordered_map<unsigned, std::tuple<uint64_t, uint64_t, std::vector<unsigned>>> &burstedNodes,
		std::function<bool(unsigned, unsigned)> comparator
	);
	bool findOutBursts(
		std::unordered_map<unsigned, std::tuple<uint64_t, uint64_t, std::vector<unsigned>>> &burstedNodes,
		std::string &wholeLoopName,
		const std::vector<std::string> &instIDList
	);

	std::string generateInstID(unsigned opcode, std::vector<std::string> instIDList);

public:
	XilinxZCUMemoryModel(BaseDatapath *datapath);

	void analyseAndTransform();

	bool tryAllocate(unsigned node, int opcode, bool commit = true);
	void release(unsigned node, int opcode);

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
