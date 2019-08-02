#include "profile_h/BaseDatapath.h"

#include <fstream>
#include <sstream>

#include "llvm/Support/GraphWriter.h"
#include "profile_h/colors.h"
#include "profile_h/opcodes.h"

void BaseDatapath::findMinimumRankPair(std::pair<unsigned, unsigned> &pair, std::map<unsigned, unsigned> rankMap) {
	unsigned minRank = numOfTotalNodes;

	for(auto &it : rankMap) {
		unsigned nodeRank = it.second;
		if(nodeRank < minRank) {
			pair.first = it.first;
			minRank = nodeRank;
		}
	}

	minRank = numOfTotalNodes;

	for(auto &it : rankMap) {
		unsigned nodeRank = it.second;
		if(it.first != pair.first && nodeRank < minRank) {
			pair.second = it.first;
			minRank = nodeRank;
		}
	}
}

BaseDatapath::BaseDatapath(
	std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
	std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor,
	bool enablePipelining, uint64_t asapII
) :
	kernelName(kernelName), CM(CM), summaryFile(summaryFile),
	loopName(loopName), loopLevel(loopLevel), loopUnrollFactor(loopUnrollFactor), datapathType(NORMAL_LOOP),
	enablePipelining(enablePipelining), asapII(asapII), PC(kernelName)
{
	builder = nullptr;
	profile = nullptr;
	microops.clear();

	// Create hardware profile based on selected platform
	profile = HardwareProfile::createInstance();

	VERBOSE_PRINT(errs() << "\tBuild initial DDDG\n");

	builder = new DDDGBuilder(this, PC);
	builder->buildInitialDDDG();
	delete builder;
	builder = nullptr;

	postDDDGBuild();

	numCycles = 0;
	maxII = 0;
	rcIL = 0;

	///FIXME: We set numOfPortsPerPartition to 1000, so that we do not have memory port limitations. 
	/// 1000 ports are sufficient. 
	/// Later, we need to add memory port limitation below (struct will be better) to take read/write
	/// ports into consideration.
	numOfPortsPerPartition = 1000;

	// Reset resource counting in profile
	profile->clear();

	sharedLoadsRemoved = 0;
	repeatedStoresRemoved = 0;
}

// This constructor does not perform DDDG generation. It should be generated externally via
// child classes (e.g. DynamicDatapath)
BaseDatapath::BaseDatapath(
	std::string kernelName, ConfigurationManager &CM, std::ofstream *summaryFile,
	std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor, unsigned datapathType
) :
	kernelName(kernelName), CM(CM), summaryFile(summaryFile),
	loopName(loopName), loopLevel(loopLevel), loopUnrollFactor(loopUnrollFactor), datapathType(datapathType),
	enablePipelining(false), asapII(0), PC(kernelName)
{
	builder = nullptr;
	profile = nullptr;
	microops.clear();

	// Create hardware profile based on selected platform
	profile = HardwareProfile::createInstance();

	numCycles = 0;
	rcIL = 0;

	///FIXME: We set numOfPortsPerPartition to 1000, so that we do not have memory port limitations. 
	/// 1000 ports are sufficient. 
	/// Later, we need to add memory port limitation below (struct will be better) to take read/write
	/// ports into consideration.
	numOfPortsPerPartition = 1000;

	// Reset resource counting in profile
	profile->clear();

	sharedLoadsRemoved = 0;
	repeatedStoresRemoved = 0;
}

BaseDatapath::~BaseDatapath() {
	if(builder)
		delete builder;
	if(profile)
		delete profile;
}

std::string BaseDatapath::getTargetLoopName() const {
	return loopName;
}

unsigned BaseDatapath::getTargetLoopLevel() const {
	return loopLevel;
}

uint64_t BaseDatapath::getTargetLoopUnrollFactor() const {
	return loopUnrollFactor;
}

unsigned BaseDatapath::getNumNodes() const {
	return boost::num_vertices(graph);
}

unsigned BaseDatapath::getNumEdges() const {
	return boost::num_edges(graph);
}

uint64_t BaseDatapath::getMaxII() const {
	return maxII;
}

uint64_t BaseDatapath::getRCIL() const {
	return rcIL;
}

Pack &BaseDatapath::getPack() {
	return P;
}

void BaseDatapath::postDDDGBuild() {
	// XXX: Changed from old logic that seemed buggish (i.e. numOfTotalNodes had always one more isolated node)
	// that was read from getTraceLineFromTo()
	numOfTotalNodes = getNumNodes();

	BGL_FORALL_VERTICES(v, graph, Graph) nameToVertex[boost::get(boost::vertex_index, graph, v)] = v;
	vertexToName = boost::get(boost::vertex_index, graph);

	for(auto &it : PC.getFuncList()) {
#ifdef LEGACY_SEPARATOR
		size_t tagPos = it.find("-");
#else
		size_t tagPos = it.find(GLOBAL_SEPARATOR);
#endif
		std::string functionName = it.substr(0, tagPos);

		functionNames.insert(functionName);
	}
}

void BaseDatapath::insertMicroop(int microop) {
	microops.push_back(microop);
}

void BaseDatapath::insertDDDGEdge(unsigned from, unsigned to, uint8_t paramID) {
	if(from != to)
		boost::add_edge(from, to, EdgeProperty(paramID), graph);
}

bool BaseDatapath::edgeExists(unsigned from, unsigned to) {
	return boost::edge(nameToVertex[from], nameToVertex[to], graph).second;
}

void BaseDatapath::updateRemoveDDDGEdges(std::set<Edge> &edgesToRemove) {
	for(auto &it : edgesToRemove)
		boost::remove_edge(it, graph);
}

void BaseDatapath::updateAddDDDGEdges(std::vector<edgeTy> &edgesToAdd) {
	for(auto &it : edgesToAdd) {
		if(it.from != it.to && !edgeExists(it.from, it.to))
			boost::get(boost::edge_weight, graph)[boost::add_edge(it.from, it.to, graph).first] = it.paramID;
	}
}

void BaseDatapath::updateRemoveDDDGNodes(std::vector<unsigned> &nodesToRemove) {
	for(auto &it : nodesToRemove)
		boost::clear_vertex(nameToVertex[it], graph);
}

void BaseDatapath::initBaseAddress() {
	const ConfigurationManager::partitionCfgMapTy &partitionMap = CM.getPartitionCfgMap();
	const ConfigurationManager::partitionCfgMapTy &completePartitionMap = CM.getCompletePartitionCfgMap();
	const std::unordered_map<int, std::pair<std::string, int64_t>> &getElementPtrMap = PC.getGetElementPtrList();

	edgeToWeight = boost::get(boost::edge_weight, graph);

	VertexIterator vi, viEnd;
	for(std::tie(vi, viEnd) = vertices(graph); vi != viEnd; vi++) {
		if(!boost::degree(*vi, graph))
			continue;

		Vertex currNode = *vi;
		unsigned nodeID = vertexToName[currNode];
		int nodeMicroop = microops.at(nodeID);

		if(!isMemoryOp(nodeMicroop))
			continue;

		bool modified = false;

		while(true) {
			bool foundParent = false;

			InEdgeIterator inEdgei, inEdgeEnd;
			for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(currNode, graph); inEdgei != inEdgeEnd; inEdgei++) {
				int paramID = edgeToWeight[*inEdgei];
				if((isLoadOp(nodeMicroop) && paramID != 1) || (LLVM_IR_GetElementPtr == nodeMicroop && paramID != 1) || (isStoreOp(nodeMicroop) && paramID != 2))
					continue;

				unsigned parentID = vertexToName[boost::source(*inEdgei, graph)];
				int parentMicroop = microops.at(parentID);
				if(LLVM_IR_GetElementPtr == parentMicroop || isLoadOp(parentMicroop)) {
					baseAddress[nodeID] = getElementPtrMap.at(parentID);
					currNode = boost::source(*inEdgei, graph);
					nodeMicroop = parentMicroop;
					foundParent = true;
					modified = true;
					break;
				}
				else if(LLVM_IR_Alloca == parentMicroop) {
					baseAddress[nodeID] = getElementPtrMap.at(parentID);
					modified = true;
					break;
				}
			}

			if(!foundParent)
				break;
		}

		if(!modified)
			baseAddress[nodeID] = getElementPtrMap.at(nodeID);

		// Check if base address is inside a partition request. If not, add to a no-partition vector
		// XXX: A partition sanity check was implemented in the original version.
		// I've removed it because since my map and partition configuration are constructed together ("atomically"),
		// I don't think that are possibilities of a partition not existing in the database
		std::string baseAddr = baseAddress[nodeID].first;
		if(partitionMap.end() == partitionMap.find(baseAddr) && completePartitionMap.end() == completePartitionMap.find(baseAddr))
			noPartitionArrayName.insert(baseAddr);
	}

	// XXX: In the original implementation, base address is written to a configuration file "_baseAddr.gz"
	// but apparently is never used. Therefore it was removed for now.
}

uint64_t BaseDatapath::fpgaEstimationOneMoreSubtraceForRecIICalculation() {
	VERBOSE_PRINT(errs() << "\tStarting RecII calculation\n");

	VERBOSE_PRINT(errs() << "\tRemoving induction dependencies\n");
	removeInductionDependencies();
	VERBOSE_PRINT(errs() << "\tRemoving PHI nodes\n");
	removePhiNodes();

	if(args.fSBOpt) {
		VERBOSE_PRINT(errs() << "\tOptimising store buffers\n");
		enableStoreBufferOptimisation();
	}

	// Put the node latency using selected architecture as edge weights in the graph
	VERBOSE_PRINT(errs() << "\tUpdating DDDG edges with operation latencies according to selected hardware\n");
	bool nonNullFound = false;
	EdgeIterator edgei, edgeEnd;
	for(std::tie(edgei, edgeEnd) = boost::edges(graph); edgei != edgeEnd; edgei++) {
		uint8_t weight = edgeToWeight[*edgei];

		// XXX: Up to this point no control edges were added so far, I think
		if(EDGE_CONTROL == weight) {
			boost::put(boost::edge_weight, graph, *edgei, 0);
		}
		else {
			unsigned nodeID = vertexToName[boost::source(*edgei, graph)];
			unsigned opcode = microops.at(nodeID);
			unsigned latency = profile->getLatency(opcode);
			boost::put(boost::edge_weight, graph, *edgei, latency);

			if(latency)
				nonNullFound = true;
		}
	}

	if(!nonNullFound) {
		VERBOSE_PRINT(errs() << "\tThis DDDG has no latency\n");
		return 0;
	}

	VERBOSE_PRINT(errs() << "\tStarting ASAP scheduling\n");
	std::tuple<uint64_t, uint64_t> asapResult = asapScheduling();

	return std::get<0>(asapResult);
}

uint64_t BaseDatapath::fpgaEstimation() {
	VERBOSE_PRINT(errs() << "\tStarting IL and II calculation\n");

	VERBOSE_PRINT(errs() << "\tRemoving induction dependencies\n");
	removeInductionDependencies();
	VERBOSE_PRINT(errs() << "\tRemoving PHI nodes\n");
	removePhiNodes();

	if(args.fSBOpt) {
		VERBOSE_PRINT(errs() << "\tOptimising store buffers\n");
		enableStoreBufferOptimisation();
	}

	// Put the node latency using selected architecture as edge weights in the graph
	VERBOSE_PRINT(errs() << "\tUpdating DDDG edges with operation latencies according to selected hardware\n");
	bool nonNullFound = false;
	EdgeIterator edgei, edgeEnd;
	for(std::tie(edgei, edgeEnd) = boost::edges(graph); edgei != edgeEnd; edgei++) {
		uint8_t weight = edgeToWeight[*edgei];

		// XXX: Up to this point no control edges were added so far, I think
		if(EDGE_CONTROL == weight) {
			boost::put(boost::edge_weight, graph, *edgei, 0);
		}
		else {
			unsigned nodeID = vertexToName[boost::source(*edgei, graph)];
			unsigned opcode = microops.at(nodeID);
			unsigned latency = profile->getLatency(opcode);
			boost::put(boost::edge_weight, graph, *edgei, latency);

			if(latency)
				nonNullFound = true;
		}
	}

	if(!nonNullFound) {
		VERBOSE_PRINT(errs() << "\tThis DDDG has no latency\n");
		return 0;
	}

	VERBOSE_PRINT(errs() << "\tStarting ASAP scheduling\n");
	std::tuple<uint64_t, uint64_t> asapResult = asapScheduling();

	VERBOSE_PRINT(errs() << "\tStarting ALAP scheduling\n");
	alapScheduling(asapResult);

	VERBOSE_PRINT(errs() << "\tIdentifying critical paths\n");
	identifyCriticalPaths();

	VERBOSE_PRINT(errs() << "\tStarting resource-constrained scheduling\n");
	std::pair<uint64_t, double> rcPair = rcScheduling();
	rcIL = rcPair.first;
	double achievedPeriod = rcPair.second;

	VERBOSE_PRINT(errs() << "\tGetting memory-constrained II\n");
	std::tuple<std::string, uint64_t> resIIMem = calculateResIIMem();

	VERBOSE_PRINT(errs() << "\tGetting hardware-constrained II\n");
	std::tuple<std::string, uint64_t> resIIOp = profile->calculateResIIOp();


	VERBOSE_PRINT(errs() << "\tGetting recurrence-constrained II\n");
	uint64_t recII = calculateRecII(std::get<0>(asapResult));

	uint64_t resII = (std::get<1>(resIIMem) > std::get<1>(resIIOp))? std::get<1>(resIIMem) : std::get<1>(resIIOp);
	maxII = (resII > recII)? resII : recII;

	P.clear();
	profile->fillPack(P);
	for(auto &it : P.getStructure()) {
		VERBOSE_PRINT(errs() << "\t" << std::get<0>(it) << ": ");

		switch(std::get<2>(it)) {
			case Pack::TYPE_UNSIGNED:
				VERBOSE_PRINT(errs() << std::to_string(P.getElements<uint64_t>(std::get<0>(it))[0]) << "\n");
				break;
			case Pack::TYPE_SIGNED:
				VERBOSE_PRINT(errs() << std::to_string(P.getElements<int64_t>(std::get<0>(it))[0]) << "\n");
				break;
			case Pack::TYPE_FLOAT:
				VERBOSE_PRINT(errs() << std::to_string(P.getElements<float>(std::get<0>(it))[0]) << "\n");
				break;
			case Pack::TYPE_STRING:
				VERBOSE_PRINT(errs() << P.getElements<std::string>(std::get<0>(it))[0] << "\n");
				break;
		}
	}
	VERBOSE_PRINT(errs() << "\n\tAchieved period: " << std::to_string(achievedPeriod) << "\n");
	VERBOSE_PRINT(errs() << "\tIL: " << std::to_string(rcIL) << "\n");
	VERBOSE_PRINT(errs() << "\tII: " << std::to_string(maxII) << "\n");
	VERBOSE_PRINT(errs() << "\tRecII: " << std::to_string(recII) << "\n");
	VERBOSE_PRINT(errs() << "\tResII: " << std::to_string(resII) << "\n");
	VERBOSE_PRINT(errs() << "\tResIIMem: " << std::to_string(std::get<1>(resIIMem)) << " constrained by: " << std::get<0>(resIIMem) << "\n");
	VERBOSE_PRINT(errs() << "\tResIIOp: " << std::to_string(std::get<1>(resIIOp)) << " constrained by: " << std::get<0>(resIIOp) << "\n");
	if(sharedLoadsRemoved)
		VERBOSE_PRINT(errs() << "\tNumber of shared loads detected: " << std::to_string(sharedLoadsRemoved) << "\n");
	if(repeatedStoresRemoved)
		VERBOSE_PRINT(errs() << "\tNumber of repeated stores detected: " << std::to_string(repeatedStoresRemoved) << "\n");

	uint64_t numCycles = getLoopTotalLatency(maxII);
	dumpSummary(numCycles, std::get<0>(asapResult), achievedPeriod, maxII, resIIMem, resIIOp, recII);

	P.addDescriptor("Achieved period", Pack::MERGE_MAX, Pack::TYPE_FLOAT);
	P.addElement<float>("Achieved period", achievedPeriod);
	P.addDescriptor("Number of shared loads detected", Pack::MERGE_SUM, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("Number of shared loads detected", sharedLoadsRemoved);
	P.addDescriptor("Number of repeated stores detected", Pack::MERGE_SUM, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("Number of repeated stores detected", repeatedStoresRemoved);
	if(!(args.fNoFPUThresOpt)) {
		P.addDescriptor("Units limited by DSP usage", Pack::MERGE_SET, Pack::TYPE_STRING);
		for(auto &i : profile->getConstrainedUnits()) {
			P.addElement<uint64_t>("Units limited by DSP usage", i);
			switch(i) {
				case HardwareProfile::LIMITED_BY_FADD:
					P.addElement<std::string>("Units limited by DSP usage", "fadd");
					break;
				case HardwareProfile::LIMITED_BY_FSUB:
					P.addElement<std::string>("Units limited by DSP usage", "fsub");
					break;
				case HardwareProfile::LIMITED_BY_FMUL:
					P.addElement<std::string>("Units limited by DSP usage", "fmul");
					break;
				case HardwareProfile::LIMITED_BY_FDIV:
					P.addElement<std::string>("Units limited by DSP usage", "fdiv");
					break;
			}
		}
	}
	// XXX: There is still missing reports for resII, resIIMem and resIIOp. I have to think on a better way of
	// merging these values when multiple DDDGs are generated

	return numCycles;
}

void BaseDatapath::removeInductionDependencies() {
	const std::vector<std::string> &instID = PC.getInstIDList();

	std::vector<Vertex> topologicalSortedNodes;
	boost::topological_sort(graph, std::back_inserter(topologicalSortedNodes));

	// Nodes with no incoming edges first
	for(auto vi = topologicalSortedNodes.rbegin(); vi != topologicalSortedNodes.rend(); vi++) {
		unsigned nodeID = vertexToName[*vi];
		std::string nodeInstID = instID.at(nodeID);

		if(nodeInstID.find("indvars") != std::string::npos) {
			if(LLVM_IR_Add == microops.at(nodeID))
				microops.at(nodeID) = LLVM_IR_IndexAdd;
			else if(LLVM_IR_Sub == microops.at(nodeID))
				microops.at(nodeID) = LLVM_IR_IndexSub;
		}
		else {
			InEdgeIterator inEdgei, inEdgeEnd;
			for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(*vi, graph); inEdgei != inEdgeEnd; inEdgei++) {
				unsigned parentID = vertexToName[boost::source(*inEdgei, graph)];
				std::string parentInstID = instID.at(parentID);

				if(std::string::npos == parentInstID.find("indvars") && !isIndexOp(microops.at(parentID)))
					continue;

				if(LLVM_IR_Add == microops.at(nodeID))
					microops.at(nodeID) = LLVM_IR_IndexAdd;
				else if(LLVM_IR_Sub == microops.at(nodeID))
					microops.at(nodeID) = LLVM_IR_IndexSub;
			}
		}
	}
}

void BaseDatapath::removePhiNodes() {
	std::set<Edge> edgesToRemove;
	std::vector<edgeTy> edgesToAdd;

	VertexIterator vi, viEnd;
	for(std::tie(vi, viEnd) = boost::vertices(graph); vi != viEnd; vi++) {
		unsigned nodeID = vertexToName[*vi];
		int nodeMicroop = microops.at(nodeID);

		if(nodeMicroop != LLVM_IR_PHI && nodeMicroop != LLVM_IR_BitCast)
			continue;

		// If code reaches this point, this node is a PHI node

		std::vector<std::pair<unsigned, uint8_t>> phiChild;

		// Mark its children
		OutEdgeIterator outEdgei, outEdgeEnd;
		for(std::tie(outEdgei, outEdgeEnd) = boost::out_edges(*vi, graph); outEdgei != outEdgeEnd; outEdgei++) {
			edgesToRemove.insert(*outEdgei);
			phiChild.push_back(std::make_pair(vertexToName[target(*outEdgei, graph)], edgeToWeight[*outEdgei]));
		}

		if(!phiChild.size())
			continue;

		// Mark its parents
		InEdgeIterator inEdgei, inEdgeEnd;
		for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(*vi, graph); inEdgei != inEdgeEnd; inEdgei++) {
			unsigned parentID = vertexToName[boost::source(*inEdgei, graph)];
			edgesToRemove.insert(*inEdgei);

			for(auto &child : phiChild)
				edgesToAdd.push_back({parentID, child.first, child.second});
		}

		std::vector<std::pair<unsigned, uint8_t>>().swap(phiChild);
	}

	// Edges from-to PHI nodes are substituted by direct connections (i.e. PHI nodes are removed)
	updateRemoveDDDGEdges(edgesToRemove);
	updateAddDDDGEdges(edgesToAdd);
}

void BaseDatapath::enableStoreBufferOptimisation() {
	const std::vector<std::string> &instID = PC.getInstIDList();
	const std::vector<std::string> &dynamicMethodID = PC.getFuncList();
	const std::vector<std::string> &prevBB = PC.getPrevBBList();

	std::vector<edgeTy> edgesToAdd;
	std::vector<unsigned> nodesToRemove;

	for(unsigned nodeID = 0; nodeID < numOfTotalNodes; nodeID++) {
		// Node not found or with no connections
		if(nameToVertex.end() == nameToVertex.find(nodeID) || !boost::degree(nameToVertex[nodeID], graph)) {
			// XXX: We will check the child and also the parent of this node, therefore this might be the case
			// why the counter is incremented by 2 (i.e. check in pairs)
			nodeID++;
			continue;
		}

		if(isStoreOp(microops.at(nodeID))) {
			std::string key = constructUniqueID(dynamicMethodID.at(nodeID), instID.at(nodeID), prevBB.at(nodeID));
			// XXX: Please note that dynamicMemoryOps is still not generated in pipeline analysis
			// Dynamic store, cannot disambiguate in static time, cannot remove
			if(dynamicMemoryOps.find(key) != dynamicMemoryOps.end()) {
				nodeID++;
				continue;
			}

			Vertex node = nameToVertex[nodeID];
			std::vector<Vertex> storeChild;

			// Check for child nodes that are loads
			OutEdgeIterator outEdgei, outEdgeEnd;
			for(tie(outEdgei, outEdgeEnd) = boost::out_edges(node, graph); outEdgei != outEdgeEnd; outEdgei++) {
				Vertex child = boost::target(*outEdgei, graph);
				unsigned childID = vertexToName[child];

				if(isLoadOp(microops.at(childID))) {
					std::string key = constructUniqueID(dynamicMethodID.at(childID), instID.at(childID), prevBB.at(childID));
					// TODO: Same possible problem as above!
					if(dynamicMemoryOps.find(key) != dynamicMemoryOps.end())
						continue;
					else
						storeChild.push_back(child);
				}
			}

			if(storeChild.size()) {
				// Find the parent of the store node that generates the stored value
				InEdgeIterator inEdgei, inEdgeEnd;
				for(tie(inEdgei, inEdgeEnd) = boost::in_edges(node, graph); inEdgei != inEdgeEnd; inEdgei++) {
					if(1 == edgeToWeight[*inEdgei]) {
						// Create a direct connection between the node that generates the value and the node that loads it
						for(auto &it : storeChild) {
							nodesToRemove.push_back(vertexToName[it]);

							OutEdgeIterator outEdgei, outEdgeEnd;
							for(tie(outEdgei, outEdgeEnd) = boost::out_edges(it, graph); outEdgei != outEdgeEnd; outEdgei++) {
								edgesToAdd.push_back({
									(unsigned) vertexToName[boost::source(*inEdgei, graph)],
									(unsigned) vertexToName[boost::target(*outEdgei, graph)],
									edgeToWeight[*outEdgei]
								});
							}
						}

						break;
					}
				}
			}
		}
	}

	// Sequences of static [value generation]->store->load->[value use] are substituted by [value generation]->[value use]
	updateAddDDDGEdges(edgesToAdd);
	updateRemoveDDDGNodes(nodesToRemove);
}

void BaseDatapath::initScratchpadPartitions() {
	const ConfigurationManager::partitionCfgMapTy &partitionMap = CM.getPartitionCfgMap();
	const std::unordered_map<int, std::pair<int64_t, unsigned>> &memoryTraceList = PC.getMemoryTraceList();

	for(unsigned nodeID = 0; nodeID < numOfTotalNodes; nodeID++) {
		if(!isMemoryOp(microops.at(nodeID)) || baseAddress.end() == baseAddress.find(nodeID))
			continue;

		std::string label = baseAddress[nodeID].first;
		int64_t address = baseAddress[nodeID].second;

		ConfigurationManager::partitionCfgMapTy::const_iterator found = partitionMap.find(label);
		if(found != partitionMap.end()) {
			unsigned type = found->second.type;
			uint64_t size = found->second.size;
			uint64_t pFactor = found->second.pFactor;

			if(1 == pFactor)
				continue;

			int64_t absAddress = memoryTraceList.at(nodeID).first;
			unsigned dataSize = memoryTraceList.at(nodeID).second >> 3;
			int64_t relAddress = (absAddress - address) / dataSize;

			int64_t finalAddress;
			if(ConfigurationManager::partitionCfgTy::PARTITION_TYPE_BLOCK == type)
				finalAddress = std::ceil(nextPowerOf2(size) / pFactor);
			else if(ConfigurationManager::partitionCfgTy::PARTITION_TYPE_CYCLIC == type)
				finalAddress = relAddress % pFactor;
			else
				assert(false && "Invalid partition type found");

#ifdef LEGACY_SEPARATOR
			baseAddress[nodeID] = std::make_pair(label + "-" + std::to_string(finalAddress), address);
#else
			baseAddress[nodeID] = std::make_pair(label + GLOBAL_SEPARATOR + std::to_string(finalAddress), address);
#endif
		}
	}
}

void BaseDatapath::optimiseDDDG() {
	// NOTE: Test memory disambiguation
	if(args.fMemDisambuigOpt)
		performMemoryDisambiguation();

	if(!args.fNoSLROpt) {
		bool activate = args.fSLROpt;

		if(!activate) {
			// If both --fno-slr and --f-slr are omitted, lin-analyzer will activate it if the inntermost loop is fully unrolled
			loopName2levelUnrollVecMapTy::iterator found = loopName2levelUnrollVecMap.find(loopName);
			assert(found != loopName2levelUnrollVecMap.end() && "Loop not found in loopName2levelUnrollVecMap");
			std::vector<unsigned> unrollFactors = found->second;
			wholeloopName2loopBoundMapTy::iterator found2 = wholeloopName2loopBoundMap.find(appendDepthToLoopName(loopName, unrollFactors.size()));
			assert(found2 != wholeloopName2loopBoundMap.end() && "Loop not found in wholeloopName2loopBoundMap");
			uint64_t innermostBound = found2->second;

			activate = unrollFactors.back() == innermostBound;
		}

		if(activate)
			removeSharedLoads();
	}

	if(args.fRSROpt)
		removeRepeatedStores();

	if(args.fTHRIntOpt)
		reduceTreeHeight(isAssociative);

	if(args.fTHRFloatOpt)
		reduceTreeHeight(isFAssociative);
}

void BaseDatapath::performMemoryDisambiguation() {
	assert(false && "Memory disambiguation is untested for now and was deactivated");

	std::unordered_multimap<std::string, std::string> loadStorePairs;
	std::unordered_set<std::string> pairedStore;
	const std::vector<std::string> &dynamicMethodID = PC.getFuncList();
	const std::vector<std::string> &instID = PC.getInstIDList();
	const std::vector<std::string> &prevBB = PC.getPrevBBList();

	std::vector<Vertex> topologicalSortedNodes;
	boost::topological_sort(graph, std::back_inserter(topologicalSortedNodes));

	// Nodes with no incoming edges first
	for(auto vi = topologicalSortedNodes.rbegin(); vi != topologicalSortedNodes.rend(); vi++) {
		unsigned nodeID = vertexToName[*vi];
		int microop = microops.at(nodeID);

		// Only look for store nodes
		if(!isStoreOp(microop))
			continue;

		// Look for subsequent loads
		OutEdgeIterator outEdgei, outEdgeEnd;
		for(std::tie(outEdgei, outEdgeEnd) = boost::out_edges(*vi, graph); outEdgei != outEdgeEnd; outEdgei++) {
			unsigned childID = vertexToName[boost::target(*outEdgei, graph)];
			int childMicroop = microops.at(childID);

			if(!isLoadOp(childMicroop))
				continue;

			std::string nodeDynamicMethodID = dynamicMethodID.at(nodeID);
			std::string childDynamicMethodID = dynamicMethodID.at(childID);

			// Ignore if dynamic function names are different (either functions are different or different executions)
			if(nodeDynamicMethodID.compare(childDynamicMethodID))
				continue;

			std::string storeUniqueID = constructUniqueID(nodeDynamicMethodID, instID.at(nodeID), prevBB.at(nodeID));
			std::string loadUniqueID = constructUniqueID(childDynamicMethodID, instID.at(childID), prevBB.at(childID));

			// Mark this store as paired
			pairedStore.insert(storeUniqueID);

			// Find in all loads with this ID if any is paired with this store. If not, add it
			bool storeFound = false;
			auto loadRange = loadStorePairs.equal_range(loadUniqueID);
			for(auto it = loadRange.first; it != loadRange.second; it++) {
				if(!storeUniqueID.compare(it->second)) {
					storeFound = true;
					break;
				}
			}
			if(!storeFound)
				loadStorePairs.insert(std::make_pair(loadUniqueID, storeUniqueID));
		}
	}

	// Return if no pairing happened
	if(!loadStorePairs.size())
		return;

	std::vector<edgeTy> edgesToAdd;
	std::unordered_map<std::string, unsigned> lastStore;

	for(unsigned nodeID = 0; nodeID < numOfTotalNodes; nodeID++) {
		int microop = microops.at(nodeID);

		// Only consider loads and stores
		if(!isMemoryOp(microop))
			continue;

		std::string uniqueID = constructUniqueID(dynamicMethodID.at(nodeID), instID.at(nodeID), prevBB.at(nodeID));

		// Store node
		if(isStoreOp(microop)) {
			// Ignore non-paired stores
			if(pairedStore.end() == pairedStore.find(uniqueID))
				continue;

			// Mark this as the last store so far
			lastStore[uniqueID] = nodeID;
		}
		// Load node
		else {
			// If there is only one load-store pair, there is no ambiguity
			auto loadRange = loadStorePairs.equal_range(uniqueID);
			if(1 == std::distance(loadRange.first, loadRange.second))
				continue;

			for(auto it = loadRange.first; it != loadRange.second; it++) {
				assert(pairedStore.find(it->second) != pairedStore.end() && "Store that was paired not found in pairedStore");

				std::unordered_map<std::string, unsigned>::iterator found = lastStore.find(it->second);
				if(lastStore.end() == found)
					continue;

				// Create a dependency for this load-store pair
				unsigned prevStoreID = found->second;
				if(!edgeExists(prevStoreID, nodeID)) {
					// XXX: Perhaps a meaningful name should be given to this type of edge
					edgesToAdd.push_back({prevStoreID, nodeID, 255});
					// XXX: This seems quite odd and I have not tested
					// it->[first|second] is already a unique ID and we are appending one more element to it
					dynamicMemoryOps.insert(it->second + "-" + prevBB.at(prevStoreID));
					dynamicMemoryOps.insert(it->first + "-" + prevBB.at(nodeID));
				}
			}
		}
	}
	updateAddDDDGEdges(edgesToAdd);
}

void BaseDatapath::removeSharedLoads() {
	const std::unordered_map<int, std::pair<int64_t, unsigned>> &memoryTraceList = PC.getMemoryTraceList();
	std::set<Edge> edgesToRemove;
	std::vector<edgeTy> edgesToAdd;
	std::unordered_map<int64_t, unsigned> loadedAddresses;
	sharedLoadsRemoved = 0;

	for(unsigned nodeID = 0; nodeID < numOfTotalNodes; nodeID++) {
		if(nameToVertex.end() == nameToVertex.find(nodeID))
			continue;

		if(!boost::degree(nameToVertex[nodeID], graph))
			continue;

		int microop = microops.at(nodeID);
		if(!isMemoryOp(microop))
			continue;

		// From this point only active store and loads are considered

		std::unordered_map<int, std::pair<int64_t, unsigned>>::const_iterator found = memoryTraceList.find(nodeID);
		assert(found != memoryTraceList.end() && "Storage operation found with no memory trace element");
		std::unordered_map<int64_t, unsigned>::iterator found2 = loadedAddresses.find(found->second.first);

		// Address is loaded
		if(found2 != loadedAddresses.end()) {
			// If this is store, unload address
			if(isStoreOp(microop)) {
				loadedAddresses.erase(found2);
			}
			// This is a load. Since address is already loaded, this is a shared load
			else if(isLoadOp(microop)) {
				sharedLoadsRemoved++;
				microops.at(nodeID) = LLVM_IR_Move;
				unsigned prevLoadID = found2->second;

				// Disconnect this load, and connect its childs to the previous load
				OutEdgeIterator outEdgei, outEdgeEnd;
				for(std::tie(outEdgei, outEdgeEnd) = boost::out_edges(nameToVertex[nodeID], graph); outEdgei != outEdgeEnd; outEdgei++) {
					unsigned childID = vertexToName[boost::target(*outEdgei, graph)];
					if(!edgeExists(prevLoadID, childID))
						edgesToAdd.push_back({prevLoadID, childID, edgeToWeight[*outEdgei]});
					edgesToRemove.insert(*outEdgei);
				}
				InEdgeIterator inEdgei, inEdgeEnd;
				for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(nameToVertex[nodeID], graph); inEdgei != inEdgeEnd; inEdgei++)
					edgesToRemove.insert(*inEdgei);
			}
		}
		// Address is not loaded and this is a load op, mark address as loaded
		else if(isLoadOp(microop)) {
			loadedAddresses.insert(std::make_pair(found->second.first, nodeID));
		}
	}

	updateRemoveDDDGEdges(edgesToRemove);
	updateAddDDDGEdges(edgesToAdd);
}

void BaseDatapath::removeRepeatedStores() {
	const std::unordered_map<int, std::pair<int64_t, unsigned>> &memoryTraceList = PC.getMemoryTraceList();
	const std::vector<std::string> &dynamicMethodID = PC.getFuncList();
	const std::vector<std::string> &instID = PC.getInstIDList();
	const std::vector<std::string> &prevBB = PC.getPrevBBList();
	std::unordered_map<int64_t, unsigned> addressStoreMap;
	repeatedStoresRemoved = 0;

	for(unsigned nodeID = numOfTotalNodes - 1; nodeID + 1; nodeID--) {
		if(nameToVertex.end() == nameToVertex.find(nodeID))
			continue;

		if(!boost::degree(nameToVertex[nodeID], graph))
			continue;

		if(!isStoreOp(microops.at(nodeID)))
			continue;

		// From this point only active stores are considered

		int64_t nodeAddress = memoryTraceList.at(nodeID).first;
		std::unordered_map<int64_t, unsigned>::iterator found = addressStoreMap.find(nodeAddress);
		// This is the first time a store to this address is found, so we save it
		if(addressStoreMap.end() == found) {
			addressStoreMap[nodeAddress] = nodeID;
		}
		// This is not the first time a store is found to this address
		else {
			std::string storeUniqueID = constructUniqueID(dynamicMethodID.at(nodeID), instID.at(nodeID), prevBB.at(nodeID));

			// If there is no ambiguity related to this store, we convert it to a silent store
			if(dynamicMemoryOps.end() == dynamicMemoryOps.find(storeUniqueID) && !boost::out_degree(nameToVertex[nodeID], graph)) {
				microops.at(nodeID) = LLVM_IR_SilentStore;
				repeatedStoresRemoved++;
			}
		}
	}
}

void BaseDatapath::reduceTreeHeight(bool (&isAssociativeFunc)(unsigned)) {
	std::vector<bool> visited(numOfTotalNodes, false);
	std::set<Edge> edgesToRemove;
	std::vector<edgeTy> edgesToAdd;

	for(unsigned int nodeID = numOfTotalNodes - 1; nodeID + 1; nodeID--) {
		if(nameToVertex.end() == nameToVertex.find(nodeID) || !boost::degree(nameToVertex[nodeID], graph))
			continue;

		if(visited.at(nodeID) || !isAssociativeFunc(microops.at(nodeID)))
			continue;

		visited.at(nodeID) = true;

		std::list<unsigned> nodes;
		std::vector<Edge> edgesToRemoveTmp;
		std::vector<std::pair<unsigned, bool>> leaves;
		std::vector<unsigned> associativeChain;

		associativeChain.push_back(nodeID);
		for(unsigned i = 0; i < associativeChain.size(); i++) {
			unsigned chainNodeID = associativeChain.at(i);
			int chainNodeMicroop = microops.at(chainNodeID);

			if(isAssociativeFunc(chainNodeMicroop)) {
				visited.at(chainNodeID) = true;
				unsigned numOfChainParents = 0;

				InEdgeIterator inEdgei, inEdgeEnd;
				for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(nameToVertex[chainNodeID], graph); inEdgei != inEdgeEnd; inEdgei++) {
					unsigned parentID = vertexToName[boost::source(*inEdgei, graph)];

					if(isBranchOp(microops.at(parentID)))
						continue;

					numOfChainParents++;
				}

				if(2 == numOfChainParents) {
					nodes.push_front(chainNodeID);

					for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(nameToVertex[chainNodeID], graph); inEdgei != inEdgeEnd; inEdgei++) {
						Vertex parentNode = boost::source(*inEdgei, graph);
						unsigned parentID = vertexToName[parentNode];
						assert(parentID < chainNodeID && "Parent node has larger ID than its child");

						int parentMicroop = microops.at(parentID);

						if(isBranchOp(parentMicroop))
							continue;

						edgesToRemoveTmp.push_back(*inEdgei);

						visited.at(parentID) = true;

						if(!isAssociativeFunc(parentMicroop)) {
							leaves.push_back(std::make_pair(parentID, false));
						}
						else {
							int numOfChildren = 0;
							OutEdgeIterator outEdgei, outEdgeEnd;
							for(std::tie(outEdgei, outEdgeEnd) = boost::out_edges(parentNode, graph); outEdgei != outEdgeEnd; outEdgei++) {
								if(edgeToWeight[*outEdgei] != BaseDatapath::EDGE_CONTROL)
									numOfChildren++;
							}

							if(1 == numOfChildren)
								associativeChain.push_back(parentID);
							else
								leaves.push_back(std::make_pair(parentID, false));
						}
					}
				}
				else {
					leaves.push_back(std::make_pair(chainNodeID, false));
				}
			}
			else {
				leaves.push_back(std::make_pair(chainNodeID, false));
			}
		}

		if(nodes.size() < 3)
			continue;

		for(auto &it : edgesToRemoveTmp)
			edgesToRemove.insert(it);

		std::map<unsigned, unsigned> rankMap;

		for(auto &it : leaves)
			rankMap[it.first] = (it.second)? numOfTotalNodes : 0;

		for(auto &it : nodes) {
			std::pair<unsigned, unsigned> nodePair;

			if(2 == rankMap.size()) {
				nodePair.first = rankMap.begin()->first;
				nodePair.second = (++(rankMap.begin()))->first;
			}
			else {
				findMinimumRankPair(nodePair, rankMap);
			}

			assert(nodePair.first != numOfTotalNodes && nodePair.second != numOfTotalNodes);

			// TODO: maybe a meaningful weight here?
			edgesToAdd.push_back({nodePair.first, it, 1});
			edgesToAdd.push_back({nodePair.second, it, 1});

			rankMap[it] = std::max(rankMap[nodePair.first], rankMap[nodePair.second]) + 1;
			rankMap.erase(nodePair.first);
			rankMap.erase(nodePair.second);
		}
	}

	updateRemoveDDDGEdges(edgesToRemove);
	updateAddDDDGEdges(edgesToAdd);
}

std::string BaseDatapath::constructUniqueID(std::string funcID, std::string instID, std::string bbID) {
#ifdef LEGACY_SEPARATOR
	return funcID + "-" + instID + "-" + bbID;
#else
	return funcID + GLOBAL_SEPARATOR + instID + GLOBAL_SEPARATOR + bbID;
#endif
}

std::tuple<uint64_t, uint64_t> BaseDatapath::asapScheduling() {
	VERBOSE_PRINT(errs() << "\t\tASAP scheduling started\n");

	uint64_t maxCycles = 0, maxScheduledTime = 0;

	asapScheduledTime.assign(numOfTotalNodes, 0);

	std::map<uint64_t, std::vector<unsigned>> maxTimesNodesMap;
#ifdef CHECK_VISITED_NODES
	std::set<unsigned> visitedNodes;
#endif

	for(unsigned nodeID = 0; nodeID < numOfTotalNodes; nodeID++) {
		Vertex currNode = nameToVertex[nodeID];

		// Set scheduled time to 0 to root nodes
		if(!boost::in_degree(currNode, graph)) {
			asapScheduledTime[currNode] = 0;
#ifdef CHECK_VISITED_NODES
			visitedNodes.insert(nodeID);
#endif
			continue;
		}

		unsigned maxCurrStartTime = 0;
		InEdgeIterator inEdgei, inEdgeEnd;
		// Evaluate all incoming edges. Save the largest incoming time considering scheduled time of parents + the edge weight
		for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(currNode, graph); inEdgei != inEdgeEnd; inEdgei++) {
			unsigned parentNodeID = vertexToName[boost::source(*inEdgei, graph)];
#ifdef CHECK_VISITED_NODES
			assert(visitedNodes.find(parentNodeID) != visitedNodes.end() && "Node was not yet visited!");
#endif
			unsigned currNodeStartTime = asapScheduledTime[parentNodeID] + edgeToWeight[*inEdgei];
			if(currNodeStartTime > maxCurrStartTime)
				maxCurrStartTime = currNodeStartTime;
		}
		asapScheduledTime[nodeID] = maxCurrStartTime;
#ifdef CHECK_VISITED_NODES
		visitedNodes.insert(nodeID);
#endif

		maxTimesNodesMap[maxCurrStartTime].push_back(nodeID);
	}

	// Find the path with the maximum scheduled time
	std::vector<uint64_t>::iterator found = std::max_element(asapScheduledTime.begin(), asapScheduledTime.end());
	maxScheduledTime = *found;

	// The maximum scheduled time does not consider the latency of the last node. If there is more
	// than one path with the same maximum scheduled time, check which generates the largest
	// latency
	uint64_t maxLatency = 0;
	for(auto &it : maxTimesNodesMap[maxScheduledTime]) {
		unsigned opcode = microops.at(it);
		unsigned latency = profile->getLatency(opcode);
		if(latency > maxLatency)
			maxLatency = latency;
	}

	maxCycles = (args.fExtraScalar)? maxScheduledTime + maxLatency : maxScheduledTime + maxLatency - 1;

	std::map<uint64_t, std::vector<unsigned>>().swap(maxTimesNodesMap);

	VERBOSE_PRINT(errs() << "\t\tLatency: " << std::to_string(maxCycles) << "\n");
	VERBOSE_PRINT(errs() << "\t\tfASAP scheduling finished\n");

	return std::make_tuple(maxCycles, maxScheduledTime);
}

void BaseDatapath::alapScheduling(std::tuple<uint64_t, uint64_t> asapResult) {
	VERBOSE_PRINT(errs() << "\t\tALAP scheduling started\n");

	alapScheduledTime.assign(numOfTotalNodes, 0);

	std::map<uint64_t, std::vector<unsigned>> minTimesNodesMap;
#ifdef CHECK_VISITED_NODES
	std::set<unsigned> visitedNodes;
#endif

	// nodeID is incremented by 1 here, so that we can use unsigned (otherwise exit condition would be i < 0)
	for(unsigned nodeID = numOfTotalNodes - 1; nodeID + 1; nodeID--) {
		Vertex currNode = nameToVertex[nodeID];

		// Set scheduled time to maximum time from ASAP to leaf nodes
		if(!boost::out_degree(currNode, graph)) {
			alapScheduledTime[nodeID] = std::get<1>(asapResult);
#ifdef CHECK_VISITED_NODES
			visitedNodes.insert(nodeID);
#endif
			continue;
		}

		// Initialise minimum time with the result of ASAP
		unsigned minCurrStartTime = std::get<1>(asapResult);
		OutEdgeIterator outEdgei, outEdgeEnd;
		// Evaluate all outcoming edges. Save the smallest outcoming time considering scheduled time of childs - the edge weight
		for(std::tie(outEdgei, outEdgeEnd) = boost::out_edges(currNode, graph); outEdgei != outEdgeEnd; outEdgei++) {
			unsigned childNodeID = vertexToName[boost::target(*outEdgei, graph)];
#ifdef CHECK_VISITED_NODES
			assert(visitedNodes.find(childNodeID) != visitedNodes.end() && "Node was not yet visited!");
#endif
			unsigned currNodeStartTime = alapScheduledTime[childNodeID] - edgeToWeight[*outEdgei];
			if(currNodeStartTime < minCurrStartTime)
				minCurrStartTime = currNodeStartTime;
		}
		alapScheduledTime[nodeID] = minCurrStartTime;
#ifdef CHECK_VISITED_NODES
		visitedNodes.insert(nodeID);
#endif

		minTimesNodesMap[minCurrStartTime].push_back(nodeID);
	}

	// Calculate required resources for current scheduling, without imposing any restrictions
	const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap = CM.getArrayInfoCfgMap();
	profile->calculateRequiredResources(microops, arrayInfoCfgMap, baseAddress, minTimesNodesMap);

	std::map<uint64_t, std::vector<unsigned>>().swap(minTimesNodesMap);

	P.clear();
	profile->fillPack(P);
	for(auto &it : P.getStructure()) {
		VERBOSE_PRINT(errs() << "\t\t" << std::get<0>(it) << ": ");

		switch(std::get<2>(it)) {
			case Pack::TYPE_UNSIGNED:
				VERBOSE_PRINT(errs() << std::to_string(P.getElements<uint64_t>(std::get<0>(it))[0]) << "\n");
				break;
			case Pack::TYPE_SIGNED:
				VERBOSE_PRINT(errs() << std::to_string(P.getElements<int64_t>(std::get<0>(it))[0]) << "\n");
				break;
			case Pack::TYPE_FLOAT:
				VERBOSE_PRINT(errs() << std::to_string(P.getElements<float>(std::get<0>(it))[0]) << "\n");
				break;
			case Pack::TYPE_STRING:
				VERBOSE_PRINT(errs() << P.getElements<std::string>(std::get<0>(it))[0] << "\n");
				break;
		}
	}

	VERBOSE_PRINT(errs() << "\t\tALAP scheduling finished\n");
}

void BaseDatapath::identifyCriticalPaths() {
	cPathNodes.clear();
	assert(
		asapScheduledTime.size() && alapScheduledTime.size() &&
		(asapScheduledTime.size() == alapScheduledTime.size()) &&
		"ASAP and/or ALAP scheduled times for nodes not generated or generated incorrectly (forgot to call asapScheduling() and/or alapScheduling()?)"
	);

	// After calculating ASAP and ALAP, the critical path is defined by the nodes that have the same scheduled time on both
	// (i.e. no operation mobility / slack)
	for(unsigned nodeID = 0; nodeID < numOfTotalNodes; nodeID++) {
		if(!boost::degree(nameToVertex[nodeID], graph))
			continue;

		if(asapScheduledTime[nodeID] == alapScheduledTime[nodeID])
			cPathNodes.push_back(nodeID);
	}
}

std::pair<uint64_t, double> BaseDatapath::rcScheduling() {
	VERBOSE_PRINT(errs() << "\t\tResource-constrained scheduling started\n");

	assert(asapScheduledTime.size() && alapScheduledTime.size() && cPathNodes.size() && "ASAP, ALAP and/or critical path list not generated");

	// initScratchpadPartitions() generated more stuff that we do not use in Lina. Thus it was reduced to process only what we need
	VERBOSE_PRINT(errs() << "\t\tUpdating base address database\n");
	initScratchpadPartitions();
	VERBOSE_PRINT(errs() << "\t\tOptimising DDDG\n");
	optimiseDDDG();

	if(args.showPostOptDDDG)
		dumpGraph(true);

	rcScheduledTime.assign(numOfTotalNodes, 0);

	profile->constrainHardware(CM.getArrayInfoCfgMap(), CM.getPartitionCfgMap(), CM.getCompletePartitionCfgMap());

	RCScheduler rcSched(
		loopName, loopLevel, datapathType,
		microops, graph, numOfTotalNodes, nameToVertex, vertexToName,
		*profile, baseAddress, asapScheduledTime, alapScheduledTime, rcScheduledTime
	);
	std::pair<uint64_t, double> rcPair = rcSched.schedule();

	VERBOSE_PRINT(errs() << "\t\tResource-constrained scheduling finished\n");
	return rcPair;
}

std::tuple<std::string, uint64_t> BaseDatapath::calculateResIIMem() {
	const std::map<std::string, std::tuple<uint64_t, uint64_t, uint64_t>> &arrayConfig = profile->arrayGetConfig();
	std::map<std::string, bool> arrayPartitionToPreviousSchedReadAssigned;
	std::map<std::string, bool> arrayPartitionToPreviousSchedWriteAssigned;
	std::map<std::string, uint64_t> arrayPartitionToPreviousSchedRead;
	std::map<std::string, uint64_t> arrayPartitionToPreviousSchedWrite;
	std::map<std::string, uint64_t> arrayPartitionToResII;
	std::map<std::string, std::vector<uint64_t>> arrayPartitionToSchedReadDiffs;
	std::map<std::string, std::vector<uint64_t>> arrayPartitionToSchedWriteDiffs;

	arrayPartitionToNumOfReads.clear();
	arrayPartitionToNumOfWrites.clear();

	for(auto &it : arrayConfig) {
		std::string arrayName = it.first;
		uint64_t numOfPartitions = std::get<0>(it.second);

		// Only partial partitioning
		if(numOfPartitions > 1) {
			for(unsigned i = 0; i < numOfPartitions; i++) {
#ifdef LEGACY_SEPARATOR
				std::string arrayPartitionName = arrayName + "-" + std::to_string(i);
#else
				std::string arrayPartitionName = arrayName + GLOBAL_SEPARATOR + std::to_string(i);
#endif
				arrayPartitionToPreviousSchedReadAssigned.insert(std::make_pair(arrayPartitionName, false));
				arrayPartitionToPreviousSchedWriteAssigned.insert(std::make_pair(arrayPartitionName, false));
				arrayPartitionToResII.insert(std::make_pair(arrayPartitionName, 0));
			}
		}
		// Complete or no partitioning
		else {
			arrayPartitionToPreviousSchedReadAssigned.insert(std::make_pair(arrayName, false));
			arrayPartitionToPreviousSchedWriteAssigned.insert(std::make_pair(arrayName, false));
			arrayPartitionToResII.insert(std::make_pair(arrayName, 0));
		}
	}

	std::map<uint64_t, std::vector<unsigned>> rcToNodes;
	for(unsigned nodeID = 0; nodeID < numOfTotalNodes; nodeID++) {
		if(!boost::degree(nameToVertex[nodeID], graph))
			continue;

		rcToNodes[rcScheduledTime[nodeID]].push_back(nodeID);
	}

	for(auto &it : rcToNodes) {
		uint64_t currentSched = it.first;

		for(auto &it2 : it.second) {
			unsigned opcode = microops.at(it2);

			if(!isMemoryOp(opcode))
				continue;

			// From this point only loads and stores are considered

			std::string partitionName = baseAddress.at(it2).first;
#ifdef LEGACY_SEPARATOR
			std::string arrayName = partitionName.substr(0, partitionName.find("-"));
#else
			std::string arrayName = partitionName.substr(0, partitionName.find(GLOBAL_SEPARATOR));
#endif

			if(isLoadOp(opcode)) {
				// Complete partitioning, no need to analyse
				if(!std::get<0>(arrayConfig.at(arrayName)))
					continue;

				arrayPartitionToNumOfReads[partitionName]++;

				if(!arrayPartitionToPreviousSchedReadAssigned[partitionName]) {
					arrayPartitionToPreviousSchedRead[partitionName] = currentSched;
					arrayPartitionToPreviousSchedReadAssigned[partitionName] = true;
				}

				uint64_t prevSchedRead = arrayPartitionToPreviousSchedRead[partitionName];
				if(prevSchedRead != currentSched) {
					arrayPartitionToSchedReadDiffs[partitionName].push_back(currentSched - prevSchedRead);
					arrayPartitionToPreviousSchedRead[partitionName] = currentSched;
				}
			}

			if(isStoreOp(opcode)) {
				// Complete partitioning, no need to analyse
				if(!std::get<0>(arrayConfig.at(arrayName)))
					continue;

				arrayPartitionToNumOfWrites[partitionName]++;

				if(!arrayPartitionToPreviousSchedWriteAssigned[partitionName]) {
					arrayPartitionToPreviousSchedWrite[partitionName] = currentSched;
					arrayPartitionToPreviousSchedWriteAssigned[partitionName] = true;
				}

				uint64_t prevSchedWrite = arrayPartitionToPreviousSchedWrite[partitionName];
				if(prevSchedWrite != currentSched) {
					arrayPartitionToSchedWriteDiffs[partitionName].push_back(currentSched - prevSchedWrite);
					arrayPartitionToPreviousSchedWrite[partitionName] = currentSched;
				}
			}
		}
	}

	for(auto &it : arrayPartitionToResII) {
		std::string partitionName = it.first;

		// Analyse for read
		uint64_t readII = 0;
		std::map<std::string, uint64_t>::iterator found = arrayPartitionToNumOfReads.find(partitionName);
		if(found != arrayPartitionToNumOfReads.end()) {
			uint64_t numReads = found->second;
			uint64_t numReadPorts = profile->arrayGetPartitionReadPorts(partitionName);

			std::map<std::string, std::vector<uint64_t>>::iterator found2 = arrayPartitionToSchedReadDiffs.find(partitionName);
			if(found2 != arrayPartitionToSchedReadDiffs.end())
				readII = std::ceil(numReads / (double) numReadPorts);
		}

		// Analyse for write
		uint64_t writeII = 0;
		std::map<std::string, uint64_t>::iterator found3 = arrayPartitionToNumOfWrites.find(partitionName);
		if(found3 != arrayPartitionToNumOfWrites.end()) {
			uint64_t numWrites = found3->second;
			uint64_t numWritePorts = profile->arrayGetPartitionWritePorts(partitionName);

			std::map<std::string, std::vector<uint64_t>>::iterator found4 = arrayPartitionToSchedWriteDiffs.find(partitionName);
			if(found4 != arrayPartitionToSchedWriteDiffs.end()) {
				uint64_t minDiff = *(std::min_element(arrayPartitionToSchedWriteDiffs[partitionName].begin(), arrayPartitionToSchedWriteDiffs[partitionName].end()));
				writeII = std::ceil((numWrites * minDiff) / (double) numWritePorts);
			}
			else {
				writeII = std::ceil(numWrites / (double) numWritePorts);
			}
		}

		it.second = (readII > writeII)? readII : writeII;
	}

	std::map<std::string, uint64_t>::iterator maxIt = std::max_element(arrayPartitionToResII.begin(), arrayPartitionToResII.end(), prioritiseSmallerResIIMem);

	if(maxIt->second > 1)
		return std::make_tuple(maxIt->first, maxIt->second);
	else
		return std::make_tuple("none", 1);
}

uint64_t BaseDatapath::calculateRecII(uint64_t currAsapII) {
	if(enablePipelining) {
		int64_t sub = (int64_t) (asapII - currAsapII);

		assert((sub >= 0) && "Negative value found when calculating recII");

		// When loop pipelining is enabled, the registers between floating point units
		// are removed. To improve estimation accuracy, we must subtract from recII
		// the amount of floating point units used during the last "recII" cycles from
		// the critical path
		if(sub > 1) {
			std::map<uint64_t, std::vector<unsigned>> asapToNodes;
			for(auto &it : cPathNodes)
				asapToNodes[asapScheduledTime.at(it)].push_back(it);

			unsigned maxLatency = 1;
			uint64_t fOpsFound = 0;
			for(uint64_t timeStamp = currAsapII - sub; timeStamp <= currAsapII; timeStamp += maxLatency) {
				std::map<uint64_t, std::vector<unsigned>>::iterator found = asapToNodes.find(timeStamp);
				while(asapToNodes.end() == found) {
					found = asapToNodes.find(++timeStamp);
					assert(timeStamp < currAsapII && "Did not find any critical path nodes in the timestamp window search");
				}

				maxLatency = 1;
				bool fOpFound = false;

				for(auto &it : found->second) {
					unsigned opcode = microops.at(it);

					if(!isFloatOp(opcode))
						continue;

					unsigned latency = profile->getLatency(opcode);
					if(latency > maxLatency)
						maxLatency = latency;

					fOpFound = true;
				}

				if(fOpFound)
					fOpsFound++;
			}

			sub -= fOpsFound;
			assert(sub >= 0 && "recII is now negative after adjusting with fOps");

			return sub + 1;
		}
		else {
			return 1;
		}
	}
	else {
		return 1;
	}
}

uint64_t BaseDatapath::getLoopTotalLatency(uint64_t maxII) {
	uint64_t noPipelineLatency = 0, pipelinedLatency = 0;

	loopName2levelUnrollVecMapTy::iterator found = loopName2levelUnrollVecMap.find(loopName);
	assert(found != loopName2levelUnrollVecMap.end() && "Could not find loop in loopName2levelUnrollVecMap");
	std::string wholeLoopName = appendDepthToLoopName(loopName, loopLevel);
	wholeloopName2loopBoundMapTy::iterator found2 = wholeloopName2loopBoundMap.find(wholeLoopName);
	assert(found2 != wholeloopName2loopBoundMap.end() && "Could not find loop in wholeloopName2loopBoundMap");

	std::vector<unsigned> targetUnroll = found->second;
	uint64_t loopBound = found2->second;

	for(unsigned i = loopLevel - 1; i + 1; i--) {
		unsigned unrollFactor = targetUnroll.at(i);
		std::string currentWholeLoopName = appendDepthToLoopName(loopName, i + 1);

		uint64_t currentLoopBound = wholeloopName2loopBoundMap.at(currentWholeLoopName);
		assert(currentLoopBound && "Loop bound is equal to zero");

		if(loopLevel - 1 == i)
			noPipelineLatency = rcIL * (currentLoopBound / unrollFactor) + EXTRA_ENTER_EXIT_LOOP_LATENCY;
		else if(i)
			noPipelineLatency = noPipelineLatency * (currentLoopBound / unrollFactor) + EXTRA_ENTER_EXIT_LOOP_LATENCY;
		else
			noPipelineLatency = noPipelineLatency * (currentLoopBound / unrollFactor);

		if(i) {
			unsigned upperLoopUnrollFactor = targetUnroll.at(i - 1);

			noPipelineLatency *= upperLoopUnrollFactor;

			// We consider EXTRA_ENTER_EXIT_LOOP_LATENCY as the overhead latency for a loop. When two consecutive loops
			// are present, a cycle for each loop overhead can be merged (i.e. the exit condition of a loop can be evaluated
			// at the same time as the enter condition of the following loop). Since right now consecutive inner loops are only
			// possible with unroll, we compensate this cycle difference with the loop unroll factor
			noPipelineLatency -= (upperLoopUnrollFactor - 1);
		}
	}

	if(enablePipelining) {
		unsigned unrollFactor = targetUnroll.at(loopLevel - 1);

		int64_t i;
		uint64_t currentIterations = loopBound / unrollFactor;
		for(i = ((int) loopLevel) - 2; i >= 0; i--) {
			std::string currentWholeLoopName = appendDepthToLoopName(loopName, i + 1);
			uint64_t currentLoopBound = wholeloopName2loopBoundMap.at(currentWholeLoopName);

			if(wholeloopName2perfectOrNotMap.at(currentWholeLoopName))
				currentIterations *= currentLoopBound;
			else
				break;
		}

		uint64_t totalIterations = 1;
		for(; i >= 0; i--) {
			std::string currentWholeLoopName = appendDepthToLoopName(loopName, i + 1);
			uint64_t currentLoopBound = wholeloopName2loopBoundMap.at(currentWholeLoopName);
			totalIterations *= currentLoopBound;
		}

		pipelinedLatency = (maxII * (currentIterations - 1) + rcIL + 2) * totalIterations;
	}

	return enablePipelining? pipelinedLatency : noPipelineLatency;
}

void BaseDatapath::dumpSummary(
	uint64_t numCycles, uint64_t asapII, double achievedPeriod,
	uint64_t maxII, std::tuple<std::string, uint64_t> resIIMem, std::tuple<std::string, uint64_t> resIIOp, uint64_t recII
) {
	*summaryFile << "================================================\n";
	if(args.fNoTCS)
		*summaryFile << "Time-constrained scheduling disabled\n";
	*summaryFile << "Target clock: " << std::to_string(args.frequency) << " MHz\n";
	*summaryFile << "Clock uncertainty: " << std::to_string(args.uncertainty) << " %\n";
	*summaryFile << "Target clock period: " << std::to_string(1000 / args.frequency) << " ns\n";
	*summaryFile << "Effective clock period: " << std::to_string((1000 / args.frequency) - (10 * args.uncertainty / args.frequency)) << " ns\n";
	*summaryFile << "Achieved clock period: " << std::to_string(achievedPeriod) << " ns\n";
	*summaryFile << "Loop name: " << loopName << "\n";
	*summaryFile << "Loop level: " << std::to_string(loopLevel) << "\n";

	*summaryFile << "DDDG type: ";
	switch(datapathType) {
		case NON_PERFECT_BEFORE:
			*summaryFile << "anterior part of loop body (before any nested loops)\n";
			break;
		case NON_PERFECT_AFTER:
			*summaryFile << "posterior part of loop body (after any nested loops)\n";
			break;
		case NON_PERFECT_BETWEEN:
			*summaryFile << "posterior + anterior (between unrolled iterations)\n";
			break;
		default:
			*summaryFile << "full loop body\n";
	}

	*summaryFile << "Loop unrolling factor: " << std::to_string(loopUnrollFactor) << "\n";
	*summaryFile << "Loop pipelining enabled? " << (enablePipelining? "yes" : "no") << "\n";
	*summaryFile << "Total cycles: " << std::to_string(numCycles) << "\n";
	*summaryFile << "------------------------------------------------\n";

	if(sharedLoadsRemoved)
		*summaryFile << "Number of shared loads detected: " << std::to_string(sharedLoadsRemoved) << "\n";
	if(repeatedStoresRemoved)
		*summaryFile << "Number of repeated stores detected: " << std::to_string(repeatedStoresRemoved) << "\n";
	if(sharedLoadsRemoved || repeatedStoresRemoved)
		*summaryFile << "------------------------------------------------\n";

	*summaryFile << "Ideal iteration latency (ASAP): " << std::to_string(asapII) << "\n";
	*summaryFile << "Constrained iteration latency: " << std::to_string(rcIL) << "\n";
	*summaryFile << "Initiation interval (if applicable): " << std::to_string(maxII) << "\n";
	*summaryFile << "resII (mem): " << std::to_string(std::get<1>(resIIMem)) << "\n";
	*summaryFile << "resII (op): " << std::to_string(std::get<1>(resIIOp)) << "\n";
	*summaryFile << "recII: " << std::to_string(recII) << "\n";

	*summaryFile << "Limited by ";
	if(std::get<1>(resIIMem) > std::get<1>(resIIOp) && std::get<1>(resIIMem) > recII && std::get<1>(resIIMem) > 1)
		*summaryFile << "memory, array name: " << std::get<0>(resIIMem) << "\n";
	else if(std::get<1>(resIIOp) > std::get<1>(resIIMem) && std::get<1>(resIIOp) > recII && std::get<1>(resIIOp) > 1)
		*summaryFile << "floating point operation: " << std::get<0>(resIIOp) << "\n";
	else if(recII > std::get<1>(resIIMem) && recII > std::get<1>(resIIOp) && recII > 1)
		*summaryFile << "loop-carried dependency\n";
	else
		*summaryFile << "none\n";
	*summaryFile << "------------------------------------------------\n";

	if(!(args.fNoFPUThresOpt)) {
		*summaryFile << "Units limited by DSP usage: ";
		bool anyFound = false;
		for(auto &i : profile->getConstrainedUnits()) {
			std::string unitName;
			switch(i) {
				case HardwareProfile::LIMITED_BY_FADD:
					unitName = "fadd";
					break;
				case HardwareProfile::LIMITED_BY_FSUB:
					unitName = "fsub";
					break;
				case HardwareProfile::LIMITED_BY_FMUL:
					unitName = "fmul";
					break;
				case HardwareProfile::LIMITED_BY_FDIV:
					unitName = "fdiv";
					break;
			}

			if(!anyFound) {
				*summaryFile << unitName;
				anyFound = true;
			}
			else {
				*summaryFile << ", " << unitName;
			}
		}

		if(!anyFound)
			*summaryFile << "none";

		*summaryFile << "\n";
		*summaryFile << "------------------------------------------------\n";
	}

	for(auto &it : P.getStructure()) {
		*summaryFile << std::get<0>(it) << ": ";

		switch(std::get<2>(it)) {
			case Pack::TYPE_UNSIGNED:
				*summaryFile << std::to_string(P.getElements<uint64_t>(std::get<0>(it))[0]) << "\n";
				break;
			case Pack::TYPE_SIGNED:
				*summaryFile << std::to_string(P.getElements<int64_t>(std::get<0>(it))[0]) << "\n";
				break;
			case Pack::TYPE_FLOAT:
				*summaryFile << std::to_string(P.getElements<float>(std::get<0>(it))[0]) << "\n";
				break;
			case Pack::TYPE_STRING:
				*summaryFile << P.getElements<std::string>(std::get<0>(it))[0] << "\n";
				break;
		}
	}
}	

void BaseDatapath::dumpGraph(bool isOptimised) {
	std::string datapathTypeStr(
		(NON_PERFECT_BEFORE == datapathType)? "_before" : ((NON_PERFECT_AFTER == datapathType)? "_after" : ((NON_PERFECT_BETWEEN == datapathType)? "_inter" : "" ))
	);
	std::string graphFileName(
		args.outWorkDir
			+ appendDepthToLoopName(loopName, loopLevel)
			+ datapathTypeStr
			+ (isOptimised? "_graph_opt.dot" : "_graph.dot")
	);
	std::ofstream out(graphFileName);

	std::vector<std::string> functionNames;
	for(auto &it : PC.getFuncList()) {
#ifdef LEGACY_SEPARATOR
		size_t tagPos = it.find("-");
#else
		size_t tagPos = it.find(GLOBAL_SEPARATOR);
#endif
		std::string functionName = it.substr(0, tagPos);

		functionNames.push_back(functionName);
	}

	ColorWriter colorWriter(graph, vertexToName, PC.getCurrBBList(), functionNames, microops, bbFuncNamePair2lpNameLevelPairMap);
	EdgeColorWriter edgeColorWriter(graph, edgeToWeight);
	write_graphviz(out, graph, colorWriter, edgeColorWriter);

	out.close();
}

BaseDatapath::RCScheduler::RCScheduler(
	const std::string loopName, const unsigned loopLevel, const unsigned datapathType,
	const std::vector<int> &microops,
	const Graph &graph, unsigned numOfTotalNodes,
	const std::unordered_map<unsigned, Vertex> &nameToVertex, const VertexNameMap &vertexToName,
	HardwareProfile &profile, const std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress,
	const std::vector<uint64_t> &asap, const std::vector<uint64_t> &alap, std::vector<uint64_t> &rc
) :
	microops(microops),
	graph(graph), numOfTotalNodes(numOfTotalNodes),
	nameToVertex(nameToVertex), vertexToName(vertexToName),
	profile(profile), baseAddress(baseAddress),
	asap(asap), alap(alap), rc(rc),
	tcSched(microops, graph, numOfTotalNodes, nameToVertex, vertexToName, profile)
{
	numParents.assign(numOfTotalNodes, 0);
	finalIsolated.assign(numOfTotalNodes, true);
	totalConnectedNodes = 0;
	scheduledNodeCount = 0;
	achievedPeriod = 0;
	alapShift = 0;

	startingNodes.clear();

	fAddReady.clear();
	fSubReady.clear();
	fMulReady.clear();
	fDivReady.clear();
	fCmpReady.clear();
	loadReady.clear();
	storeReady.clear();
	intOpReady.clear();
	callReady.clear();
	othersReady.clear();

	fAddSelected.clear();
	fSubSelected.clear();
	fMulSelected.clear();
	fDivSelected.clear();
	fCmpSelected.clear();
	loadSelected.clear();
	storeSelected.clear();
	intOpSelected.clear();
	callSelected.clear();

	fAddExecuting.clear();
	fSubExecuting.clear();
	fMulExecuting.clear();
	fDivExecuting.clear();
	fCmpExecuting.clear();
	loadExecuting.clear();
	storeExecuting.clear();
	intOpExecuting.clear();
	callExecuting.clear();

	// Select root connected nodes to start scheduling
	VertexIterator vi, vEnd;
	for(std::tie(vi, vEnd) = boost::vertices(graph); vi != vEnd; vi++) {
		unsigned currNodeID = vertexToName[*vi];

		if(!boost::degree(*vi, graph))
			continue;

		unsigned inDegree = boost::in_degree(*vi, graph);
		numParents[currNodeID] = inDegree;
		totalConnectedNodes++;
		finalIsolated[currNodeID] = false;

		if(inDegree)
			continue;

		// From this point only connected root nodes are considered

		startingNodes.push_back(std::make_pair(currNodeID, alap[currNodeID]));
	}

	if(args.showScheduling) {
		std::string datapathTypeStr(
			(NON_PERFECT_BEFORE == datapathType)? "_before" : ((NON_PERFECT_AFTER == datapathType)? "_after" : ((NON_PERFECT_BETWEEN == datapathType)? "_inter" : "" ))
		);
		dumpFile.open(args.outWorkDir + appendDepthToLoopName(loopName, loopLevel) + datapathTypeStr + ".sched.rpt");

		dumpFile << "================================================\n";
		dumpFile << "Lin-analyzer scheduling report file\n";
		dumpFile << "Loop name: " << loopName << "\n";
		if(args.fNoTCS)
			dumpFile << "Time-constrained scheduling disabled\n";
		dumpFile << "Target clock: " << std::to_string(args.frequency) << " MHz\n";
		dumpFile << "Clock uncertainty: " << std::to_string(args.uncertainty) << " %\n";
		dumpFile << "Target clock period: " << std::to_string(1000 / args.frequency) << " ns\n";
		dumpFile << "Effective clock period: " << std::to_string((1000 / args.frequency) - (10 * args.uncertainty / args.frequency)) << " ns\n";
		dumpFile << "------------------------------------------------\n";
	}
}

BaseDatapath::RCScheduler::~RCScheduler() {
	if(dumpFile.is_open())
		dumpFile.close();
}

std::pair<uint64_t, double> BaseDatapath::RCScheduler::schedule() {
	unsigned nullCycles = 0;

	for(cycleTick = 0; scheduledNodeCount != totalConnectedNodes; cycleTick++) {
		if(args.showScheduling)
			dumpFile << "[TICK] " << std::to_string(cycleTick) << "\n";

		isNullCycle = true;

		// Assign ready state to starting nodes (if any)
		if(startingNodes.size())
			assignReadyStartingNodes();

		if(!(args.fNoTCS)) {
			// Reset timing-constraint scheduling
			tcSched.clear();

			// Before selecting, we must deduce the in-cycle latency that is being held by running instructions
			for(auto &it : fAddExecuting)
				tcSched.tryAllocate(it.first, false);
			for(auto &it : fSubExecuting)
				tcSched.tryAllocate(it.first, false);
			for(auto &it : fMulExecuting)
				tcSched.tryAllocate(it.first, false);
			for(auto &it : fDivExecuting)
				tcSched.tryAllocate(it.first, false);
			for(auto &it : fCmpExecuting)
				tcSched.tryAllocate(it.first, false);
			for(auto &it : loadExecuting)
				tcSched.tryAllocate(it.first, false);
			for(auto &it : storeExecuting)
				tcSched.tryAllocate(it.first, false);
			for(auto &it : intOpExecuting)
				tcSched.tryAllocate(it.first, false);
			for(auto &it : callExecuting)
				tcSched.tryAllocate(it.first, false);
		}

		// Nodes must be executed only once per clock tick. These lists hold which nodes were already executed
		fAddExecuted.clear();
		fSubExecuted.clear();
		fMulExecuted.clear();
		fDivExecuted.clear();
		fCmpExecuted.clear();
		loadExecuted.clear();
		storeExecuted.clear();
		intOpExecuted.clear();
		callExecuted.clear();

		// Normal cycle allocation
		readyChanged = false;
		select();
		execute();
		release();

		// Cycle allocation using remaining timing budget
		if(!(args.fNoTCS)) {
			while(readyChanged) {
				criticalPathAllocated = false;
				readyChanged = false;

				select();
				execute();
				release();

				// If any critical path node was allocated, since we are allocating nodes before their actual ALAP time
				// (cycle merging, due to timing budget), we must compensate the ALAP values, as the critical path is
				// now a bit shorter. alapShift does the trick
				if(criticalPathAllocated) {
					alapShift++;

					// Since we shifted the critical path, we check again if there are ready nodes to be solved in this shifted world
					if(startingNodes.size())
						assignReadyStartingNodes();
				}
			}
		}

		// Release pipelined functional units for next clock tick
		profile.pipelinedRelease();

		if(args.fNoTCS) {
			if(args.showScheduling)
				dumpFile << "[TICK]";
		}
		else {
			double currCriticalPath = tcSched.getCriticalPath();
			if(currCriticalPath > achievedPeriod)
				achievedPeriod = currCriticalPath;

			if(args.showScheduling)
				dumpFile << "[TICK] Critical path for this tick: " << std::to_string(currCriticalPath) << " ns";
		}

		// Null cycle detected: no instructions of interest were selected
		if(isNullCycle) {
			nullCycles++;
			dumpFile << " (null cycle)\n\n";
		}
		else {
			dumpFile << "\n\n";
		}
	}

	if(args.showScheduling) {
		dumpFile << "================================================\n";
		dumpFile.close();
	}

	// Deduce null cycles (deactivated)
	//cycleTick -= nullCycles;

	return std::make_pair((args.fExtraScalar)? cycleTick + 1 : cycleTick, achievedPeriod);
}

void BaseDatapath::RCScheduler::assignReadyStartingNodes() {
	// Sort nodes by their ALAP, smallest first (urgent nodes first)
	startingNodes.sort(prioritiseSmallerALAP);

	while(startingNodes.size()) {
		unsigned currNodeID = startingNodes.front().first;
		uint64_t alapTime = startingNodes.front().second;

		// If the cycle tick equals to the node's ALAP time, this node has to be solved now!
		// (the alapShift compensates for critical path reduction if nodes were merged before their intended cycle due to timing budget)
		if(alapTime - cycleTick <= alapShift) {
			pushReady(currNodeID, alapTime);
			startingNodes.pop_front();
		}
		// Since the list is sorted, if the if above fails, cycleTick < alapTime for
		// all other cases, we don't need to analyse
		else {
			break;
		}
	}
}

void BaseDatapath::RCScheduler::select() {
	// Schedule/finish all instructions that we are not considering (i.e. latency 0)
	unsigned failedAttempts = 0;
	while(failedAttempts < othersReady.size()) {
		unsigned currNodeID = othersReady.front().first;

		// If selecting the current node does not violate timing in any way, proceed
		if(args.fNoTCS || tcSched.tryAllocate(currNodeID)) {
			if(args.showScheduling) {
				unsigned opcode = microops.at(currNodeID);
				dumpFile << "\t[RELEASED] [0/" <<  std::to_string(profile.getLatency(opcode)) << "] Node " << currNodeID << " (" << reverseOpcodeMap.at(opcode) << ")\n";
			}

			readyChanged = true;
			othersReady.pop_front();
			rc[currNodeID] = cycleTick;
			setScheduledAndAssignReadyChildren(currNodeID);

			failedAttempts = 0;
		}
		else {
			failedAttempts++;
		}
	}

	// Attempt to allocate resources to the most urgent nodes
	trySelect(fAddReady, fAddSelected, &HardwareProfile::fAddTryAllocate);
	trySelect(fSubReady, fSubSelected, &HardwareProfile::fSubTryAllocate);
	trySelect(fMulReady, fMulSelected, &HardwareProfile::fMulTryAllocate);
	trySelect(fDivReady, fDivSelected, &HardwareProfile::fDivTryAllocate);
	trySelect(fCmpReady, fCmpSelected, &HardwareProfile::fCmpTryAllocate);
	trySelect(loadReady, loadSelected, &HardwareProfile::loadTryAllocate);
	trySelect(storeReady, storeSelected, &HardwareProfile::storeTryAllocate);
	trySelect(intOpReady, intOpSelected, &HardwareProfile::intOpTryAllocate);
	trySelect(callReady, callSelected, &HardwareProfile::callTryAllocate);
}

void BaseDatapath::RCScheduler::execute() {
	// Enqueue selected nodes for execution
	enqueueExecute(LLVM_IR_FAdd, fAddSelected, fAddExecuting, &HardwareProfile::fAddRelease);
	enqueueExecute(LLVM_IR_FSub, fSubSelected, fSubExecuting, &HardwareProfile::fSubRelease);
	enqueueExecute(LLVM_IR_FMul, fMulSelected, fMulExecuting, &HardwareProfile::fMulRelease);
	enqueueExecute(LLVM_IR_FDiv, fDivSelected, fDivExecuting, &HardwareProfile::fDivRelease);
	enqueueExecute(LLVM_IR_FCmp, fCmpSelected, fCmpExecuting, &HardwareProfile::fCmpRelease);
	enqueueExecute(LLVM_IR_Load, loadSelected, loadExecuting, &HardwareProfile::loadRelease);
	enqueueExecute(LLVM_IR_Store, storeSelected, storeExecuting, &HardwareProfile::storeRelease);
	enqueueExecute(intOpSelected, intOpExecuting, &HardwareProfile::intOpRelease);
	enqueueExecute(LLVM_IR_Call, callSelected, callExecuting, &HardwareProfile::callRelease);
}

void BaseDatapath::RCScheduler::release() {
	// Try to release resources that are being held by executing nodes
	tryRelease(LLVM_IR_FAdd, fAddExecuting, fAddExecuted, &HardwareProfile::fAddRelease);
	tryRelease(LLVM_IR_FSub, fSubExecuting, fSubExecuted, &HardwareProfile::fSubRelease);
	tryRelease(LLVM_IR_FMul, fMulExecuting, fMulExecuted, &HardwareProfile::fMulRelease);
	tryRelease(LLVM_IR_FDiv, fDivExecuting, fDivExecuted, &HardwareProfile::fDivRelease);
	tryRelease(LLVM_IR_FCmp, fCmpExecuting, fCmpExecuted, &HardwareProfile::fCmpRelease);
	tryRelease(LLVM_IR_Load, loadExecuting, loadExecuted, &HardwareProfile::loadRelease);
	tryRelease(LLVM_IR_Store, storeExecuting, storeExecuted, &HardwareProfile::storeRelease);
	tryRelease(intOpExecuting, intOpExecuted, &HardwareProfile::intOpRelease);
	tryRelease(LLVM_IR_Call, callExecuting, callExecuted, &HardwareProfile::callRelease);
}

void BaseDatapath::RCScheduler::pushReady(unsigned nodeID, uint64_t tick) {
	readyChanged = true;

	switch(microops.at(nodeID)) {
		case LLVM_IR_FAdd:
			fAddReady.push_back(std::make_pair(nodeID, tick));
			break;
		case LLVM_IR_FSub:
			fSubReady.push_back(std::make_pair(nodeID, tick));
			break;
		case LLVM_IR_FMul:
			fMulReady.push_back(std::make_pair(nodeID, tick));
			break;
		case LLVM_IR_FDiv:
			fDivReady.push_back(std::make_pair(nodeID, tick));
			break;
		case LLVM_IR_FCmp:
			fCmpReady.push_back(std::make_pair(nodeID, tick));
			break;
		case LLVM_IR_Load:
			loadReady.push_back(std::make_pair(nodeID, tick));
			break;
		case LLVM_IR_Store:
			storeReady.push_back(std::make_pair(nodeID, tick));
			break;
		case LLVM_IR_Add:
		case LLVM_IR_Sub:
		case LLVM_IR_Mul:
		case LLVM_IR_UDiv:
		case LLVM_IR_SDiv:
			intOpReady.push_back(std::make_pair(nodeID, tick));
			break;
		case LLVM_IR_Call:
			callReady.push_back(std::make_pair(nodeID, tick));
			break;
		default:
			othersReady.push_back(std::make_pair(nodeID, tick));
			break;
	}

	if(args.showScheduling)
		dumpFile << "\t[READY] Node " << std::to_string(nodeID) << " (" << reverseOpcodeMap.at(microops.at(nodeID)) << ")\n";
}

void BaseDatapath::RCScheduler::trySelect(nodeTickTy &ready, selectedListTy &selected, bool (HardwareProfile::*tryAllocate)(bool)) {
	if(ready.size()) {
		selected.clear();

		// Sort nodes by their ALAP, smallest first (urgent nodes first)
		ready.sort(prioritiseSmallerALAP);
		size_t initialReadySize = ready.size();
		for(unsigned i = 0; i < initialReadySize; i++) {
			unsigned nodeID = ready.front().first;

			// If allocation is successful (i.e. there is one operation unit available), select this operation
			// If timing-constrained scheduling is enabled, allocation is not yet performed, only attempted
			if((profile.*tryAllocate)(args.fNoTCS)) {
				bool timingConstrained = false;

				// Timing-constrained scheduling (taa-daa)
				if(!(args.fNoTCS)) {
					// If selecting the current node does not violate timing in any way, proceed
					if(tcSched.tryAllocate(nodeID))
						(profile.*tryAllocate)(true);
					// Else, fail
					else
						timingConstrained = true;
				}

				// No timing-constraint, allocate
				if(!timingConstrained) {
					// If this node is in critical path, notify. This is used when allocation is being performed over the
					// intended cycle tick to consume the timing budget (if TCS is enabled)
					if(alap[nodeID] == asap[nodeID])
						criticalPathAllocated = true;

					selected.push_back(nodeID);
					ready.pop_front();
					readyChanged = true;
					rc[nodeID] = cycleTick;
				}
				// Timing contention, not able to allocate now
				else {
					break;
				}
			}
			// Resource contention, not able to allocate now
			else {
				break;
			}
		}
	}
}

void BaseDatapath::RCScheduler::trySelect(nodeTickTy &ready, selectedListTy &selected, bool (HardwareProfile::*tryAllocateInt)(unsigned, bool)) {
	if(ready.size()) {
		selected.clear();

		// Sort nodes by their ALAP, smallest first (urgent nodes first)
		ready.sort(prioritiseSmallerALAP);
		size_t initialReadySize = ready.size();
		for(unsigned i = 0; i < initialReadySize; i++) {
			unsigned nodeID = ready.front().first;

			// If allocation is successful (i.e. there is one operation unit available), select this operation
			// If timing-constrained scheduling is enabled, allocation is not yet performed, only attempted
			if((profile.*tryAllocateInt)(microops.at(nodeID), args.fNoTCS)) {
				bool timingConstrained = false;

				// Timing-constrained scheduling (taa-daa)
				if(!(args.fNoTCS)) {
					// If selecting the current node does not violate timing in any way, proceed
					if(tcSched.tryAllocate(nodeID))
						(profile.*tryAllocateInt)(microops.at(nodeID), true);
					// Else, fail
					else
						timingConstrained = true;
				}

				// No timing-constraint, allocate
				if(!timingConstrained) {
					// If this node is in critical path, notify. This is used when allocation is being performed over the
					// intended cycle tick to consume the timing budget (if TCS is enabled)
					if(alap[nodeID] == asap[nodeID])
						criticalPathAllocated = true;

					selected.push_back(nodeID);
					ready.pop_front();
					readyChanged = true;
					rc[nodeID] = cycleTick;
				}
				// Timing contention, not able to allocate now
				else {
					break;
				}
			}
			// Resource contention, not able to allocate now
			else {
				break;
			}
		}
	}
}

void BaseDatapath::RCScheduler::trySelect(nodeTickTy &ready, selectedListTy &selected, bool (HardwareProfile::*tryAllocateMem)(std::string, bool)) {
	if(ready.size()) {
		selected.clear();

		// Sort nodes by their ALAP, smallest first (urgent nodes first)
		ready.sort(prioritiseSmallerALAP);
		size_t initialReadySize = ready.size();
		for(unsigned i = 0; i < initialReadySize; i++) {
			unsigned nodeID = ready.front().first;

			// Load/store resource allocation is based on the array name
			std::string arrayPartitionName = baseAddress.at(nodeID).first;

			// If allocation is successful (i.e. there is one operation unit available), select this operation
			// If timing-constrained scheduling is enabled, allocation is not yet performed, only attempted
			if((profile.*tryAllocateMem)(arrayPartitionName, args.fNoTCS)) {
				bool timingConstrained = false;

				// Timing-constrained scheduling (taa-daa)
				if(!(args.fNoTCS)) {
					// If selecting the current node does not violate timing in any way, proceed
					if(tcSched.tryAllocate(nodeID))
						(profile.*tryAllocateMem)(arrayPartitionName, true);
					// Else, fail
					else
						timingConstrained = true;
				}

				// No timing-constraint, allocate
				if(!timingConstrained) {
					// If this node is in critical path, notify. This is used when allocation is being performed over the
					// intended cycle tick to consume the timing budget (if TCS is enabled)
					if(alap[nodeID] == asap[nodeID])
						criticalPathAllocated = true;

					selected.push_back(nodeID);
					ready.pop_front();
					readyChanged = true;
					rc[nodeID] = cycleTick;
				}
				// Timing contention, not able to allocate now
				else {
					break;
				}
			}
			// Resource contention, not able to allocate now
			else {
				break;
			}
		}
	}
}

void BaseDatapath::RCScheduler::enqueueExecute(unsigned opcode, selectedListTy &selected, executingMapTy &executing, void (HardwareProfile::*release)()) {
	while(selected.size()) {
		unsigned selectedNodeID = selected.front();
		unsigned latency = profile.getLatency(opcode);

		// Latency 0 or 1: this node was solved already. Set as scheduled and assign its children as ready
		if(latency <= 1) {
			isNullCycle = false;

			if(args.showScheduling)
				dumpFile << "\t[RELEASED] [1/" <<  std::to_string(latency) << "] Node " << selectedNodeID << " (" << reverseOpcodeMap.at(opcode) << ")\n";

			setScheduledAndAssignReadyChildren(selectedNodeID);
		}
		// Multi-latency instruction: put into executing queue
		else {
			executing.insert(std::make_pair(selectedNodeID, latency));
		}

		selected.pop_front();
	}
}

void BaseDatapath::RCScheduler::enqueueExecute(selectedListTy &selected, executingMapTy &executing, void (HardwareProfile::*releaseInt)(unsigned)) {
	while(selected.size()) {
		unsigned selectedNodeID = selected.front();
		unsigned opcode = microops.at(selectedNodeID);
		unsigned latency = profile.getLatency(opcode);

		// Latency 0 or 1: this node was solved already. Set as scheduled and assign its children as ready
		if(latency <= 1) {
			isNullCycle = false;

			if(args.showScheduling)
				dumpFile << "\t[RELEASED] [1/" <<  std::to_string(latency) << "] Node " << selectedNodeID << " (" << reverseOpcodeMap.at(opcode) << ")\n";

			setScheduledAndAssignReadyChildren(selectedNodeID);
		}
		// Multi-latency instruction: put into executing queue
		else {
			executing.insert(std::make_pair(selectedNodeID, latency));
		}
			
		selected.pop_front();
	}
}

void BaseDatapath::RCScheduler::enqueueExecute(unsigned opcode, selectedListTy &selected, executingMapTy &executing, void (HardwareProfile::*releaseMem)(std::string)) {
	while(selected.size()) {
		unsigned selectedNodeID = selected.front();
		unsigned latency = profile.getLatency(opcode);
		std::string arrayName = baseAddress.at(selectedNodeID).first;

		// Latency 0 or 1: this node was solved already. Set as scheduled and assign its children as ready
		if(latency <= 1) {
			isNullCycle = false;

			if(args.showScheduling)
				dumpFile << "\t[RELEASED] [1/" <<  std::to_string(latency) << "] Node " << selectedNodeID << " (" << reverseOpcodeMap.at(opcode) << ")\n";

			setScheduledAndAssignReadyChildren(selectedNodeID);
		}
		// Multi-latency instruction: put into executing queue
		else {
			executing.insert(std::make_pair(selectedNodeID, latency));
		}

		selected.pop_front();
	}
}

void BaseDatapath::RCScheduler::tryRelease(unsigned opcode, executingMapTy &executing, executedListTy &executed, void (HardwareProfile::*release)()) {
	std::vector<unsigned> toErase;

	for(auto &it: executing) {
		unsigned executingNodeID = it.first;

		// Check if this node was already accounted in this clock tick. If positive, pass.
		if(executed.end() == std::find(executed.begin(), executed.end(), executingNodeID))
			executed.push_back(executingNodeID);
		else
			continue;

		isNullCycle = false;

		// Decrease one cycle
		(it.second)--;

		// All cycles were consumed, this operation is done, release resource
		if(!(it.second)) {
			if(args.showScheduling)
				dumpFile << "\t[RELEASED] [" << std::to_string(it.second + 1) << "/" <<  std::to_string(profile.getLatency(opcode)) << "] Node " << executingNodeID << " (" << reverseOpcodeMap.at(opcode) << ")\n";

			setScheduledAndAssignReadyChildren(executingNodeID);
			toErase.push_back(executingNodeID);

			// If operation is pipelined, the resource was already released before
			if(!(profile.isPipelined(opcode)))
				(profile.*release)();
		}
		else {
			if(args.showScheduling)
				dumpFile << "\t[ALLOCATED] [" << std::to_string(it.second + 1) << "/" <<  std::to_string(profile.getLatency(opcode)) << "] Node " << executingNodeID << " (" << reverseOpcodeMap.at(opcode) << ")\n";
		}
	}

	for(auto &it : toErase)
		executing.erase(it);
}

void BaseDatapath::RCScheduler::tryRelease(executingMapTy &executing, executedListTy &executed, void (HardwareProfile::*releaseInt)(unsigned)) {
	std::vector<unsigned> toErase;

	for(auto &it: executing) {
		unsigned executingNodeID = it.first;
		unsigned opcode = microops.at(executingNodeID);

		// Check if this node was already accounted in this clock tick. If positive, pass.
		if(executed.end() == std::find(executed.begin(), executed.end(), executingNodeID))
			executed.push_back(executingNodeID);
		else
			continue;

		isNullCycle = false;

		// Decrease one cycle
		(it.second)--;

		// All cycles were consumed, this operation is done, release resource
		if(!(it.second)) {
			if(args.showScheduling)
				dumpFile << "\t[RELEASED] [" << std::to_string(it.second + 1) << "/" <<  std::to_string(profile.getLatency(opcode)) << "] Node " << executingNodeID << " (" << reverseOpcodeMap.at(opcode) << ")\n";

			setScheduledAndAssignReadyChildren(executingNodeID);
			toErase.push_back(executingNodeID);

			// If operation is pipelined, the resource was already released before
			if(!(profile.isPipelined(opcode)))
				(profile.*releaseInt)(opcode);
		}
		else {
			if(args.showScheduling)
				dumpFile << "\t[ALLOCATED] [" << std::to_string(it.second + 1) << "/" <<  std::to_string(profile.getLatency(opcode)) << "] Node " << executingNodeID << " (" << reverseOpcodeMap.at(opcode) << ")\n";
		}
	}

	for(auto &it : toErase)
		executing.erase(it);
}

void BaseDatapath::RCScheduler::tryRelease(unsigned opcode, executingMapTy &executing, executedListTy &executed, void (HardwareProfile::*releaseMem)(std::string)) {
	std::vector<unsigned> toErase;

	for(auto &it: executing) {
		unsigned executingNodeID = it.first;
		std::string arrayName = baseAddress.at(executingNodeID).first;

		// Check if this node was already accounted in this clock tick. If positive, pass.
		if(executed.end() == std::find(executed.begin(), executed.end(), executingNodeID))
			executed.push_back(executingNodeID);
		else
			continue;

		isNullCycle = false;

		// Decrease one cycle
		(it.second)--;

		// All cycles were consumed, this operation is done, release resource
		if(!(it.second)) {
			if(args.showScheduling)
				dumpFile << "\t[RELEASED] [" << std::to_string(it.second + 1) << "/" <<  std::to_string(profile.getLatency(opcode)) << "] Node " << executingNodeID << " (" << reverseOpcodeMap.at(opcode) << ")\n";

			setScheduledAndAssignReadyChildren(executingNodeID);
			toErase.push_back(executingNodeID);

			// If operation is pipelined, the resource was already released before
			if(!(profile.isPipelined(opcode)))
				(profile.*releaseMem)(arrayName);
		}
		else {
			if(args.showScheduling)
				dumpFile << "\t[ALLOCATED] [" << std::to_string(it.second + 1) << "/" <<  std::to_string(profile.getLatency(opcode)) << "] Node " << executingNodeID << " (" << reverseOpcodeMap.at(opcode) << ")\n";
		}
	}

	for(auto &it : toErase)
		executing.erase(it);
}

void BaseDatapath::RCScheduler::setScheduledAndAssignReadyChildren(unsigned nodeID) {
	scheduledNodeCount++;

	OutEdgeIterator outEdgei, outEdgeEnd;
	for(std::tie(outEdgei, outEdgeEnd) = boost::out_edges(nameToVertex.at(nodeID), graph); outEdgei != outEdgeEnd; outEdgei++) {
		Vertex childVertex = boost::target(*outEdgei, graph);
		unsigned childNodeID = vertexToName[childVertex];
		numParents[childNodeID]--;

		// Assign this child node as ready if all its parents were scheduled and it's not an isolated node
		if(!numParents[childNodeID] && !finalIsolated[childNodeID])
			pushReady(childNodeID, alap[childNodeID]);
	}
}

BaseDatapath::TCScheduler::TCScheduler(
	const std::vector<int> &microops,
	const Graph &graph, unsigned numOfTotalNodes,
	const std::unordered_map<unsigned, Vertex> &nameToVertex, const VertexNameMap &vertexToName,
	HardwareProfile &profile
) :
	microops(microops),
	graph(graph), numOfTotalNodes(numOfTotalNodes),
	nameToVertex(nameToVertex), vertexToName(vertexToName),
	profile(profile)
{
	effectivePeriod = (1000 / args.frequency) - (10 * args.uncertainty / args.frequency);
	clear();
}

void BaseDatapath::TCScheduler::clear() {
	paths.clear();
}

std::pair<std::vector<BaseDatapath::TCScheduler::pathTy *>, std::vector<BaseDatapath::TCScheduler::pathTy>> BaseDatapath::TCScheduler::findDependencies(unsigned nodeID) {
	std::vector<pathTy *> pathsToModify;
	std::vector<pathTy> pathsToCreate;

	// Check if this node is part of any ongoing path
	for(auto &it : paths) {
		// Check if this node is connected to any part of the current ongoing path
		for(auto &it2 : it.second) {
			// Connection found
			if(boost::edge(nameToVertex.at(it2), nameToVertex.at(nodeID), graph).second) {
				// If this is the last node of the path, mark the path
				if(it.second.back() == it2) {
					pathsToModify.push_back(&it);
				}
				// Else, create a new path with the common nodes
				else {
					pathTy newPath;
					newPath.first = 0;

					for(auto it3 = it.second.begin(); *it3 != it2; it3++) {
						newPath.first += profile.getInCycleLatency(microops.at(*it3));
						newPath.second.push_back(*it3);
					}

					// We must also add the last common node!
					newPath.first += profile.getInCycleLatency(microops.at(it2));
					newPath.second.push_back(it2);
					pathsToCreate.push_back(newPath);
				}
			}
		}
	}

	return std::make_pair(pathsToModify, pathsToCreate);
}

bool BaseDatapath::TCScheduler::tryAllocate(unsigned nodeID, bool checkTiming) {
	std::pair<std::vector<pathTy *>, std::vector<pathTy>> pathsFound = findDependencies(nodeID);
	double inCycleLatency = profile.getInCycleLatency(microops.at(nodeID));

	// If no dependency was found for this node, create a new path and return
	if(!(pathsFound.first.size() || pathsFound.second.size())) {
		pathTy newPath;
		newPath.first = inCycleLatency;
		newPath.second.push_back(nodeID);
		paths.push_back(newPath);

		return true;
	}

	if(checkTiming) {
		// Check if any modified path violates timing
		for(auto &it : pathsFound.first) {
			if((it->first + inCycleLatency) > effectivePeriod)
				return false;
		}

		// Check if any new path violates timing
		for(auto &it : pathsFound.second) {
			if((it.first + inCycleLatency) > effectivePeriod)
				return false;
		}
	}

	// No path violations reported. Create/modify the paths and return
	for(auto &it : pathsFound.first) {
		it->first += inCycleLatency;
		it->second.push_back(nodeID);
	}
	for(auto &it : pathsFound.second) {
		it.first += inCycleLatency;
		it.second.push_back(nodeID);
		paths.push_back(it);
	}

	return true;
}

double BaseDatapath::TCScheduler::getCriticalPath() {
	double criticalPath = -1;

	for(auto &it : paths) {
		if(it.first > criticalPath)
			criticalPath = it.first;
	}

	return criticalPath;
}

BaseDatapath::ColorWriter::ColorWriter(
	Graph &graph,
	VertexNameMap &vertexNameMap,
	const std::vector<std::string> &bbNames,
	const std::vector<std::string> &funcNames,
	std::vector<int> &opcodes,
	llvm::bbFuncNamePair2lpNameLevelPairMapTy &bbFuncNamePair2lpNameLevelPairMap
) : graph(graph), vertexNameMap(vertexNameMap), bbNames(bbNames), funcNames(funcNames), opcodes(opcodes), bbFuncNamePair2lpNameLevelPairMap(bbFuncNamePair2lpNameLevelPairMap) { }

template<class VE> void BaseDatapath::ColorWriter::operator()(std::ostream &out, const VE &v) const {
	unsigned nodeID = vertexNameMap[v];

	assert(nodeID < bbNames.size() && "Node ID out of bounds (bbNames)");
	assert(nodeID < funcNames.size() && "Node ID out of bounds (funcNames)");
	assert(nodeID < opcodes.size() && "Node ID out of bounds (opcodes)");

	std::string bbName = bbNames.at(nodeID);
	std::string funcName = funcNames.at(nodeID);
	llvm::bbFuncNamePairTy bbFuncPair = std::make_pair(bbName, funcName);

	llvm::bbFuncNamePair2lpNameLevelPairMapTy::iterator found = bbFuncNamePair2lpNameLevelPairMap.find(bbFuncPair);
	if(found != bbFuncNamePair2lpNameLevelPairMap.end()) {
		std::string colorString = "color=";
		switch((ColorEnum) found->second.second) {
			case RED: colorString += "red"; break;
			case GREEN: colorString += "green"; break;
			case BLUE: colorString += "blue"; break;
			case CYAN: colorString += "cyan"; break;
			case GOLD: colorString += "gold"; break;
			case HOTPINK: colorString += "hotpink"; break;
			case NAVY: colorString += "navy"; break;
			case ORANGE: colorString += "orange"; break;
			case OLIVEDRAB: colorString += "olivedrab"; break;
			case MAGENTA: colorString += "magenta"; break;
			default: colorString += "black"; break;
		}

		int op = opcodes.at(nodeID);
		if(isBranchOp(op)) {
			out << "[style=filled " << colorString << " label=\"{" << nodeID << " | br}\"]";
		}
		else if(isLoadOp(op)) {
			out << "[shape=polygon sides=5 peripheries=2 " << colorString << " label=\"{" << nodeID << " | ld}\"]";
		}
		else if(isStoreOp(op)) {
			out << "[shape=polygon sides=4 peripheries=2 " << colorString << " label=\"{" << nodeID << " | st}\"]";
		}
		else if(isAddOp(op)) {
			out << "[" << colorString << " label=\"{" << nodeID << " | add}\"]";
		}
		else if(isMulOp(op)) {
			out << "[" << colorString << " label=\"{" << nodeID << " | mul}\"]";
		}
		else if(isIndexOp(op)) {
			out << "[" << colorString << " label=\"{" << nodeID << " | index}\"]";
		}
		else if(isFloatOp(op)) {
			if(isFAddOp(op)) {
				out << "[shape=diamond " << colorString << " label=\"{" << nodeID << " | fadd}\"]";
			}
			if(isFSubOp(op)) {
				out << "[shape=diamond " << colorString << " label=\"{" << nodeID << " | fsub}\"]";
			}
			if(isFMulOp(op)) {
				out << "[shape=diamond " << colorString << " label=\"{" << nodeID << " | fmul}\"]";
			}
			if(isFDivOp(op)) {
				out << "[shape=diamond " << colorString << " label=\"{" << nodeID << " | fdiv}\"]";
			}
			if(isFCmpOp(op)) {
				out << "[shape=diamond " << colorString << " label=\"{" << nodeID << " | fcmp}\"]";
			}
		}
		else if(isPhiOp(op)) {
			out << "[shape=polygon sides=4 style=filled color=gold label=\"{" << nodeID << " | phi}\"]";
		}
		else if(isBitOp(op)) {
			out << "[" << colorString << " label=\"{" << nodeID << " | bit}\"]";
		}
		else if(isCallOp(op)) {
			out << "[" << colorString << " label=\"{" << nodeID << " | call}\"]";
		}
		else {
			out << "[" << colorString << " label=\"{" << nodeID << " | " << reverseOpcodeMap.at(op) << "}\"]";
		}
	}
}

BaseDatapath::EdgeColorWriter::EdgeColorWriter(Graph &graph, EdgeWeightMap &edgeWeightMap) : graph(graph), edgeWeightMap(edgeWeightMap) { }

template<class VE> void BaseDatapath::EdgeColorWriter::operator()(std::ostream &out, const VE &e) const {
	unsigned weight = edgeWeightMap[e];

	if(BaseDatapath::EDGE_CONTROL == weight)
		out << "[color=red label=" << weight << "]";
	else
		out << "[color=black label=" << weight << "]";
}
