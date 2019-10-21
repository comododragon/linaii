#ifndef __MEMORYMODEL_H__
#define __MEMORYMODEL_H__

#include "profile_h/auxiliary.h"
#include "profile_h/boostincls.h"
#include "profile_h/DDDGBuilder.h"

class BaseDatapath;

class MemoryModel {
protected:
	std::vector<int> &microops;
	Graph &graph;
	unsigned &numOfTotalNodes;
	std::unordered_map<unsigned, Vertex> &nameToVertex;
	VertexNameMap &vertexToName;
	std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress;
	ParsedTraceContainer &PC;

	bool changedDDDG;

public:
	MemoryModel(
		std::vector<int> &microops, Graph &graph, unsigned &numOfTotalNodes,
		std::unordered_map<unsigned, Vertex> &nameToVertex, VertexNameMap &vertexToName,
		std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress,
		ParsedTraceContainer &PC
	);
	virtual ~MemoryModel() { }
	static MemoryModel *createInstance(
		std::vector<int> &microops, Graph &graph, unsigned &numOfTotalNodes,
		std::unordered_map<unsigned, Vertex> &nameToVertex, VertexNameMap &vertexToName,
		std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress,
		ParsedTraceContainer &PC
	);

	virtual void analyseAndTransform() = 0;

	bool mustRefreshDDDG();
};

class XilinxZCUMemoryModel : public MemoryModel {
	std::unordered_map<unsigned, uint64_t> loadNodes;
	std::unordered_map<unsigned, uint64_t> storeNodes;
	std::unordered_map<unsigned, std::tuple<uint64_t, uint64_t, std::vector<unsigned>>> burstedLoads;
	std::unordered_map<unsigned, std::tuple<uint64_t, uint64_t, std::vector<unsigned>>> burstedStores;

	void findInBursts(
		std::unordered_map<unsigned, uint64_t> &foundNodes,
		std::vector<unsigned> &behavedNodes,
		std::unordered_map<unsigned, std::tuple<uint64_t, uint64_t, std::vector<unsigned>>> &burstedNodes,
		std::function<bool(unsigned, unsigned)> comparator
	);

	std::string generateInstID(unsigned opcode, std::vector<std::string> instIDList);

public:
	XilinxZCUMemoryModel(
		std::vector<int> &microops, Graph &graph, unsigned &numOfTotalNodes,
		std::unordered_map<unsigned, Vertex> &nameToVertex, VertexNameMap &vertexToName,
		std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress,
		ParsedTraceContainer &PC
	);

	void analyseAndTransform();

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
