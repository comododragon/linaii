#ifndef __BASE_DATAPATH__
#define __BASE_DATAPATH__

#include <algorithm>
#include <assert.h>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <stdint.h>

#include "profile_h/auxiliary.h"
#include "profile_h/DDDGBuilder.h"
#include "profile_h/HardwareProfile.h"
#include "profile_h/MemoryModel.h"

#include "profile_h/boostincls.h"

typedef std::unordered_map<std::string, unsigned> staticInstID2OpcodeMapTy;
extern staticInstID2OpcodeMapTy staticInstID2OpcodeMap;

typedef struct {
	unsigned from;
	unsigned to;
	uint8_t paramID;
} edgeTy;

class BaseDatapath {
public:
	enum {
		NORMAL_LOOP,
		PERFECT_LOOP,
		NON_PERFECT_BEFORE,
		NON_PERFECT_BETWEEN,
		NON_PERFECT_AFTER
	};
	// Additional costs for latency calculation
	enum {
		EXTRA_ENTER_EXIT_LOOP_LATENCY = 2
	};

	class TCScheduler {
		typedef std::pair<double, std::vector<unsigned>> pathTy;

		const std::vector<int> &microops;
		const Graph &graph;
		unsigned numOfTotalNodes;
		const std::unordered_map<unsigned, Vertex> &nameToVertex;
		const VertexNameMap &vertexToName;
		HardwareProfile &profile;

		double effectivePeriod;
		std::vector<pathTy> paths;

	public:
		TCScheduler(
			const std::vector<int> &microops,
			const Graph &graph, unsigned numOfTotalNodes,
			const std::unordered_map<unsigned, Vertex> &nameToVertex, const VertexNameMap &vertexToName,
			HardwareProfile &profile
		);

		void clear();
		std::pair<std::vector<pathTy *>, std::vector<pathTy>> findDependencies(unsigned nodeID);
		bool tryAllocate(unsigned nodeID, bool checkTiming = true);
		double getCriticalPath();
	};

	class RCScheduler {
		typedef std::list<std::pair<unsigned, uint64_t>> nodeTickTy;
		typedef std::list<unsigned> selectedListTy;
		typedef std::map<unsigned, unsigned> executingMapTy;
		typedef std::vector<unsigned> executedListTy;

		const std::vector<int> &microops;
		const Graph &graph;
		unsigned numOfTotalNodes;
		const std::unordered_map<unsigned, Vertex> &nameToVertex;
		const VertexNameMap &vertexToName;
		HardwareProfile &profile;
		const std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress;
		const std::vector<uint64_t> &asap;
		const std::vector<uint64_t> &alap;
		std::vector<uint64_t> &rc;

		std::vector<unsigned> numParents;
		std::vector<bool> finalIsolated;
		unsigned totalConnectedNodes;
		unsigned scheduledNodeCount;
		uint64_t cycleTick;
		bool isNullCycle;
		double achievedPeriod;
		bool readyChanged;
		uint64_t alapShift;
		bool criticalPathAllocated;

		TCScheduler tcSched;

		nodeTickTy startingNodes;

		nodeTickTy fAddReady;
		nodeTickTy fSubReady;
		nodeTickTy fMulReady;
		nodeTickTy fDivReady;
		nodeTickTy fCmpReady;
		nodeTickTy loadReady;
		nodeTickTy storeReady;
		nodeTickTy intOpReady;
		nodeTickTy callReady;
		nodeTickTy othersReady;
		nodeTickTy ddrOpReady;

		selectedListTy fAddSelected;
		selectedListTy fSubSelected;
		selectedListTy fMulSelected;
		selectedListTy fDivSelected;
		selectedListTy fCmpSelected;
		selectedListTy loadSelected;
		selectedListTy storeSelected;
		selectedListTy intOpSelected;
		selectedListTy callSelected;
		selectedListTy ddrOpSelected;

		executingMapTy fAddExecuting;
		executingMapTy fSubExecuting;
		executingMapTy fMulExecuting;
		executingMapTy fDivExecuting;
		executingMapTy fCmpExecuting;
		executingMapTy loadExecuting;
		executingMapTy storeExecuting;
		executingMapTy intOpExecuting;
		executingMapTy callExecuting;
		executingMapTy ddrOpExecuting;

		executedListTy fAddExecuted;
		executedListTy fSubExecuted;
		executedListTy fMulExecuted;
		executedListTy fDivExecuted;
		executedListTy fCmpExecuted;
		executedListTy loadExecuted;
		executedListTy storeExecuted;
		executedListTy intOpExecuted;
		executedListTy callExecuted;
		executedListTy ddrOpExecuted;

		std::ofstream dumpFile;

		bool dummyAllocate() { return true; }
		static bool prioritiseSmallerALAP(const std::pair<unsigned, uint64_t> &first, const std::pair<unsigned, uint64_t> &second) { return first.second < second.second; }

		void assignReadyStartingNodes();
		void select();
		void execute();
		void release();

		void pushReady(unsigned nodeID, uint64_t tick);
		void trySelect(nodeTickTy &ready, selectedListTy &selected, bool (HardwareProfile::*tryAllocate)(bool));
		void trySelect(nodeTickTy &ready, selectedListTy &selected, bool (HardwareProfile::*tryAllocateOp)(unsigned, bool));
		void trySelect(nodeTickTy &ready, selectedListTy &selected, bool (HardwareProfile::*tryAllocateMem)(std::string, bool));
		void enqueueExecute(unsigned opcode, selectedListTy &selected, executingMapTy &executing, void (HardwareProfile::*release)());
		void enqueueExecute(selectedListTy &selected, executingMapTy &executing, void (HardwareProfile::*releaseOp)(unsigned));
		void enqueueExecute(unsigned opcde, selectedListTy &selected, executingMapTy &executing, void (HardwareProfile::*releaseMem)(std::string));
		void tryRelease(unsigned opcode, executingMapTy &executing, executedListTy &executed, void (HardwareProfile::*release)());
		void tryRelease(executingMapTy &executing, executedListTy &executed, void (HardwareProfile::*releaseOp)(unsigned));
		void tryRelease(unsigned opcode, executingMapTy &executing, executedListTy &executed, void (HardwareProfile::*releaseMem)(std::string));
		void setScheduledAndAssignReadyChildren(unsigned nodeID);

	public:
		RCScheduler(
			const std::string loopName, const unsigned loopLevel, const unsigned datapathType,
			const std::vector<int> &microops,
			const Graph &graph, unsigned numOfTotalNodes,
			const std::unordered_map<unsigned, Vertex> &nameToVertex, const VertexNameMap &vertexToName,
			HardwareProfile &profile, const std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress,
			const std::vector<uint64_t> &asap, const std::vector<uint64_t> &alap, std::vector<uint64_t> &rc
		);

		~RCScheduler();

		std::pair<uint64_t, double> schedule();
	};
	
	class ColorWriter {
		Graph &graph;
		VertexNameMap &vertexNameMap;
		const std::vector<std::string> &bbNames;
		const std::vector<std::string> &funcNames;
		std::vector<int> &opcodes;
		llvm::bbFuncNamePair2lpNameLevelPairMapTy &bbFuncNamePair2lpNameLevelPairMap;

	public:
		ColorWriter(
			Graph &graph,
			VertexNameMap &vertexNameMap,
			const std::vector<std::string> &bbNames,
			const std::vector<std::string> &funcNames,
			std::vector<int> &opcodes,
			llvm::bbFuncNamePair2lpNameLevelPairMapTy &bbFuncNamePair2lpNameLevelPairMap
		);

		template<class VE> void operator()(std::ostream &out, const VE &v) const;
	};

	class EdgeColorWriter {
		Graph &graph;
		EdgeWeightMap &edgeWeightMap;

	public:
		EdgeColorWriter(Graph &graph, EdgeWeightMap &edgeWeightMap);

		template<class VE> void operator()(std::ostream &out, const VE &e) const;
	};

private:
	std::string kernelName;
	ConfigurationManager &CM;
	std::ostream *summaryFile;
	std::string loopName;
	unsigned loopLevel;
	uint64_t loopUnrollFactor;

	HardwareProfile *profile;
	MemoryModel *memmodel;

	void findMinimumRankPair(std::pair<unsigned, unsigned> &pair, std::map<unsigned, unsigned> rankMap);
	static bool prioritiseSmallerResIIMem(const std::pair<std::string, double> &first, const std::pair<std::string, double> &second) { return first.second < second.second; }

public:
	BaseDatapath(
		std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
		std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor,
		bool enablePipelining, uint64_t asapII
	);

	BaseDatapath(
		std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
		std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor, unsigned datapathType
	);

	~BaseDatapath();

	std::string getTargetLoopName() const;
	unsigned getTargetLoopLevel() const;
	uint64_t getTargetLoopUnrollFactor() const;
	unsigned getNumNodes() const;
	unsigned getNumEdges() const;
	uint64_t getMaxII() const;
	uint64_t getRCIL() const;
	Pack &getPack();

	void postDDDGBuild();
	void refreshDDDG();
	void insertMicroop(int microop);
	void insertDDDGEdge(unsigned from, unsigned to, uint8_t paramID);
	bool edgeExists(unsigned from, unsigned to);
	void updateRemoveDDDGEdges(std::set<Edge> &edgesToRemove);
	void updateAddDDDGEdges(std::vector<edgeTy> &edgesToAdd);
	void updateRemoveDDDGNodes(std::vector<unsigned> &nodesToRemove);

	std::string constructUniqueID(std::string funcID, std::string instID, std::string bbID);

	// Interface for non-subclasses (e.g. MemoryModel)
	ConfigurationManager &getConfigurationManager();
	ParsedTraceContainer &getParsedTraceContainer();
	Graph &getDDDG();
	VertexNameMap &getVertexToName();
	EdgeWeightMap &getEdgeToWeight();
	std::unordered_map<unsigned, Vertex> &getNameToVertex();
	std::vector<int> &getMicroops();
	std::unordered_map<int, std::pair<std::string, int64_t>> &getBaseAddress();

protected:
	// Special edge types
	enum {
		EDGE_CONTROL = 200,
		EDGE_PIPE = 201
	};

	unsigned datapathType;
	bool enablePipelining;
	uint64_t asapII;
	uint64_t numCycles;

	DDDGBuilder *builder;
	ParsedTraceContainer PC;
	Pack P;

	// A map from node ID to its microop
	std::vector<int> microops;
	// The DDDG
	Graph graph;
	// Number of nodes in the graph
	unsigned numOfTotalNodes;
	// A map from node ID to boost internal ID
	std::unordered_map<unsigned, Vertex> nameToVertex;
	// A map from boost internal ID to node ID
	VertexNameMap vertexToName;
	// A map from edge internal ID to its weight (parameter ID before estimation, node latency after estimation)
	EdgeWeightMap edgeToWeight;
	// Set containing all called functions
	std::unordered_set<std::string> functionNames;
	// The name says it all
	unsigned numOfPortsPerPartition;
	// A map from node ID to a getelementptr pair (variable name and its address)
	std::unordered_map<int, std::pair<std::string, int64_t>> baseAddress;
	// A set containing the name of all arrays that are not marked for partitioning
	std::set<std::string> noPartitionArrayName;
	// Memory disambiguation context variable
  	std::unordered_set<std::string> dynamicMemoryOps;
	// Vector with scheduled times for each node
	std::vector<uint64_t> asapScheduledTime;
	std::vector<uint64_t> alapScheduledTime;
	std::vector<uint64_t> rcScheduledTime;
	// Vector with nodes on the critical path
	std::vector<unsigned> cPathNodes;
	// Number of reads inside an array partition
	std::map<std::string, uint64_t> arrayPartitionToNumOfReads;
	// Number of writes inside an array partition
	std::map<std::string, uint64_t> arrayPartitionToNumOfWrites;
	// Maximum Initiation Interval for pipelined loops
	uint64_t maxII;
	// Final latency of the loop, without performing the nest calculations
	uint64_t rcIL;

	// Optimisation counters
	uint64_t sharedLoadsRemoved;
	uint64_t repeatedStoresRemoved;

	void initBaseAddress();

	uint64_t fpgaEstimationOneMoreSubtraceForRecIICalculation();
	uint64_t fpgaEstimation();

	void removeInductionDependencies();
	void removePhiNodes();
	void enableStoreBufferOptimisation();
	void initScratchpadPartitions();
	void optimiseDDDG();
	void performMemoryDisambiguation();
	void removeSharedLoads();
	void removeRepeatedStores();
	void reduceTreeHeight(bool (&isAssociativeFunc)(unsigned));

	std::tuple<uint64_t, uint64_t> asapScheduling();
	void alapScheduling(std::tuple<uint64_t, uint64_t> asapResult);
	void identifyCriticalPaths();
	std::pair<uint64_t, double> rcScheduling();
	std::tuple<std::string, uint64_t> calculateResIIMem();
	uint64_t calculateRecII(uint64_t currAsapII);
	uint64_t getLoopTotalLatency(uint64_t maxII);

	void dumpSummary(
		uint64_t numCycles, uint64_t asapII, double achievedPeriod,
		uint64_t maxII, std::tuple<std::string, uint64_t> resIIMem, std::tuple<std::string, uint64_t> resIIOp, uint64_t recII
	);
	void dumpGraph(bool isOptimised = false);
};

#endif // End of __BASE_DATAPATH__
