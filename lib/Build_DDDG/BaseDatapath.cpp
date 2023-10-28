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
	std::string kernelName, ConfigurationManager &CM, ContextManager &CtxM, std::ofstream *summaryFile, SharedDynamicTrace &traceFile,
	std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor,
	bool enablePipelining, uint64_t asapII
) :
	kernelName(kernelName), CM(CM), CtxM(CtxM), summaryFile(summaryFile), traceFile(traceFile),
	loopName(loopName), loopLevel(loopLevel), loopUnrollFactor(loopUnrollFactor), datapathType(DatapathType::NORMAL_LOOP),
	enablePipelining(enablePipelining), asapII(asapII), PC(kernelName)
{
	builder = nullptr;
	profile = nullptr;
	microops.clear();

	// Create hardware profile based on selected platform
	profile = HardwareProfile::createInstance();

	// Create memory model based on selected platform
	memmodel = MemoryModel::createInstance(this);
	profile->setMemoryModel(memmodel);

	if(args.fNoMMA || args.mmaMode != ArgPack::MMA_MODE_USE) {
		VERBOSE_PRINT(errs() << "\tBuild initial DDDG\n");

		builder = new DDDGBuilder(this, PC);
		builder->buildInitialDDDG(traceFile);

		if(ArgPack::MMA_MODE_GEN == args.mmaMode) {
			VERBOSE_PRINT(errs() << "\tSaving context for later use\n");
			std::string wholeLoopName = appendDepthToLoopName(loopName, loopLevel);
			CtxM.saveParsedTraceContainer(wholeLoopName, datapathType, loopUnrollFactor, PC);
			CtxM.saveDDDG(wholeLoopName, datapathType, loopUnrollFactor, *builder, microops);
		}

		delete builder;
		builder = nullptr;
	}
	else {
		VERBOSE_PRINT(errs() << "\t\"--mma-mode\" is set to \"use\", skipping DDDG build\n");
		VERBOSE_PRINT(errs() << "\tRecovering context from previous execution\n");
		std::string wholeLoopName = appendDepthToLoopName(loopName, loopLevel);
		CtxM.getParsedTraceContainer(wholeLoopName, datapathType, loopUnrollFactor, &PC);
		CtxM.getDDDG(wholeLoopName, datapathType, loopUnrollFactor, this);
	}

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

	dummySinkCreated = false;
}

// This constructor does not perform DDDG generation. It should be generated externally via
// child classes (e.g. DynamicDatapath)
BaseDatapath::BaseDatapath(
	std::string kernelName, ConfigurationManager &CM, ContextManager &CtxM, std::ofstream *summaryFile, SharedDynamicTrace &traceFile,
	std::string loopName, unsigned loopLevel, uint64_t loopUnrollFactor, unsigned datapathType
) :
	kernelName(kernelName), CM(CM), CtxM(CtxM), summaryFile(summaryFile), traceFile(traceFile),
	loopName(loopName), loopLevel(loopLevel), loopUnrollFactor(loopUnrollFactor), datapathType(datapathType),
	enablePipelining(false), asapII(0), PC(kernelName)
{
	builder = nullptr;
	profile = nullptr;
	microops.clear();

	// Create hardware profile based on selected platform
	profile = HardwareProfile::createInstance();

	// Create memory model based on selected platform
	memmodel = MemoryModel::createInstance(this);
	profile->setMemoryModel(memmodel);

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

	dummySinkCreated = false;
}

BaseDatapath::~BaseDatapath() {
	if(builder)
		delete builder;
	if(profile)
		delete profile;
	if(memmodel)
		delete memmodel;
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

const std::vector<MemoryModel::nodeExportTy> &BaseDatapath::getExportedNodesToBeforeDDDG() {
	return memmodel->getNodesToBeforeDDDG();
}

const std::vector<MemoryModel::nodeExportTy> &BaseDatapath::getExportedNodesToAfterDDDG() {
	return memmodel->getNodesToAfterDDDG();
}

void BaseDatapath::importNodes(std::vector<MemoryModel::nodeExportTy> nodesToImport) {
	std::vector<edgeTy> edgesToAdd;

	// Create the imported nodes
	for(auto &it : nodesToImport) {
		memmodel->importNode(it);
	}

	refreshDDDG();
}

void BaseDatapath::postDDDGBuild() {
	refreshDDDG();

	for(auto &it : PC.getFuncList()) {
#ifdef LEGACY_SEPARATOR
		size_t tagPos = it.find("-");
#else
		size_t tagPos = it.find(GLOBAL_SEPARATOR);
#endif
		std::string functionName = it.substr(0, tagPos);

		functionNames.insert(functionName);
	}

#ifdef BYTE_OPS
	for(auto &it : PC.getResultSizeList()) {
		if(8 == it.second) {
			if(LLVM_IR_Add == microops.at(it.first)) microops[it.first] = LLVM_IR_Add8;
			if(LLVM_IR_Sub == microops.at(it.first)) microops[it.first] = LLVM_IR_Sub8;
			if(LLVM_IR_Mul == microops.at(it.first)) microops[it.first] = LLVM_IR_Mul8;
			if(LLVM_IR_UDiv == microops.at(it.first)) microops[it.first] = LLVM_IR_UDiv8;
			if(LLVM_IR_SDiv == microops.at(it.first)) microops[it.first] = LLVM_IR_SDiv8;
			if(LLVM_IR_And == microops.at(it.first)) microops[it.first] = LLVM_IR_And8;
			if(LLVM_IR_Or == microops.at(it.first)) microops[it.first] = LLVM_IR_Or8;
			if(LLVM_IR_Xor == microops.at(it.first)) microops[it.first] = LLVM_IR_Xor8;
			if(LLVM_IR_Shl == microops.at(it.first)) microops[it.first] = LLVM_IR_Shl8;
			if(LLVM_IR_AShr == microops.at(it.first)) microops[it.first] = LLVM_IR_AShr8;
			if(LLVM_IR_LShr == microops.at(it.first)) microops[it.first] = LLVM_IR_LShr8;
		}
	}
#endif
}

void BaseDatapath::refreshDDDG() {
	// XXX: Changed from old logic that seemed buggish (i.e. numOfTotalNodes had always one more isolated node)
	// that was read from getTraceLineFromTo()
	numOfTotalNodes = getNumNodes();

	BGL_FORALL_VERTICES(v, graph, Graph) nameToVertex[boost::get(boost::vertex_index, graph, v)] = v;
	vertexToName = boost::get(boost::vertex_index, graph);

	edgeToWeight = boost::get(boost::edge_weight, graph);
}

void BaseDatapath::setForDDDGImport() {
	graph.clear();
	microops.clear();
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

artificialNodeTy BaseDatapath::createArtificialNode(artificialNodeTy &aNode, int opcode) {
	aNode.ID = microops.size();
	aNode.currInstID = generateInstID(opcode, PC.getInstIDList());

	// Create the node
	insertMicroop(opcode);

	// TODO: checar se está tudo sendo atualizado apropriadamente nas próximas linhas
	PC.unlock();
	PC.openAllFilesForWrite();
	// Update ParsedTraceContainer containers with the new node.
	// XXX: We use the values from the first store
	PC.appendToFuncList(aNode.currDynamicFunction);
	PC.appendToInstIDList(aNode.currInstID);
	PC.appendToLineNoList(aNode.lineNo);
	PC.appendToPrevBBList(aNode.prevBB);
	PC.appendToCurrBBList(aNode.currBB);
	// Finished
	PC.closeAllFiles();
	PC.lock();

	return aNode;
}

artificialNodeTy BaseDatapath::createArtificialNode(unsigned baseNode, int opcode) {
	artificialNodeTy aNode;

	// Since we will add new nodes to the DDDG, we must update the auxiliary structures accordingly
	aNode.opcode = opcode;
	aNode.nonSilentLatency = profile->getLatency(getNonSilentOpcode(opcode));
	aNode.currDynamicFunction = PC.getFuncList()[baseNode];
	aNode.lineNo = PC.getLineNoList()[baseNode];
	aNode.prevBB = PC.getPrevBBList()[baseNode];
	aNode.currBB = PC.getCurrBBList()[baseNode];

	return createArtificialNode(aNode, opcode);
}

unsigned BaseDatapath::createDummySink() {
	std::vector<edgeTy> edgesToAdd;
	bool branchNodeFound = false;
	unsigned branchNode;
	std::vector<unsigned> leafNodes;

	// We will search for 2 things here:
	// - The branch node that is usually isolated: We will use its info, since it has a somewhat similar positioning as the dummy node ought to be
	// - Leaf nodes that are going to be connected to the dummy node
	for(unsigned nodeID = 0; nodeID < microops.size(); nodeID++) {
		if(!boost::out_degree(nameToVertex[nodeID], graph))
			leafNodes.push_back(nodeID);

		if(isBranchOp(microops.at(nodeID))) {
			branchNodeFound = true;
			branchNode = nodeID;
		}
	}
	assert(branchNodeFound && "No branch node was found to be used as a dummy node");

	artificialNodeTy dummyNode = createArtificialNode(branchNode, LLVM_IR_Dummy);

	// Connect leaf nodes to the dummy node
	for(auto &it : leafNodes)
		edgesToAdd.push_back({it, dummyNode.ID, 0});

	updateAddDDDGEdges(edgesToAdd);

	dummySinkCreated = true;
	dummySink = dummyNode.ID;

	return dummySink;
}

unsigned BaseDatapath::getDatapathType() {
	return datapathType;
}

ConfigurationManager &BaseDatapath::getConfigurationManager() {
	return CM;
}

ParsedTraceContainer &BaseDatapath::getParsedTraceContainer() {
	return PC;
}

Graph &BaseDatapath::getDDDG() {
	return graph;
}

VertexNameMap &BaseDatapath::getVertexToName() {
	return vertexToName;
}

EdgeWeightMap &BaseDatapath::getEdgeToWeight() {
	return edgeToWeight;
}

std::unordered_map<unsigned, Vertex> &BaseDatapath::getNameToVertex() {
	return nameToVertex;
}

std::vector<int> &BaseDatapath::getMicroops() {
	return microops;
}

std::unordered_map<int, std::pair<std::string, int64_t>> &BaseDatapath::getBaseAddress() {
	return baseAddress;
}

void BaseDatapath::initBaseAddress() {
	const ConfigurationManager::partitionCfgMapTy &partitionMap = CM.getPartitionCfgMap();
	const ConfigurationManager::partitionCfgMapTy &completePartitionMap = CM.getCompletePartitionCfgMap();
	const std::unordered_map<int, std::pair<std::string, int64_t>> &getElementPtrMap = PC.getGetElementPtrList();

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

	if(!(args.fNoMMA)) {
		if(ArgPack::MMA_MODE_USE == args.mmaMode) {
			VERBOSE_PRINT(errs() <<"\t\"--mma-mode\" is set to \"use\", setting up memory model\n");
			memmodel->setUp(CtxM);
		}

		VERBOSE_PRINT(errs() << "\tPerforming DDDG memory model-based analysis and transform\n");
		profile->performMemoryModelAnalysis();

		if(ArgPack::MMA_MODE_GEN == args.mmaMode) {
			VERBOSE_PRINT(errs() <<"\t\"--mma-mode\" is set to \"gen\", halting now\n");
			return 0;
		}
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

	if(!(args.fNoMMA)) {
		memmodel->enableReport();

		if(ArgPack::MMA_MODE_USE == args.mmaMode) {
			VERBOSE_PRINT(errs() <<"\t\"--mma-mode\" is set to \"use\", setting up memory model\n");
			memmodel->setUp(CtxM);
		}

		VERBOSE_PRINT(errs() << "\tPerforming DDDG memory model-based analysis and transform\n");
		profile->performMemoryModelAnalysis();

		if(ArgPack::MMA_MODE_GEN == args.mmaMode) {
			memmodel->save(CtxM);
			VERBOSE_PRINT(errs() <<"\t\"--mma-mode\" is set to \"gen\", halting now\n");
			return 0;
		}
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
	profile->fillPack(P, loopLevel, datapathType, enablePipelining? maxII : 0);
	for(auto &it : P.getStructure()) {
		std::string name = std::get<0>(it);

		// Names starting with "_" are not printed (used for internal calculations)
		if('_' == name[0])
			continue;

		VERBOSE_PRINT(errs() << "\t" << name << ": ");

		switch(std::get<2>(it)) {
			case Pack::TYPE_UNSIGNED:
				VERBOSE_PRINT(errs() << std::to_string(P.getElements<uint64_t>(name)[0]) << "\n");
				break;
			case Pack::TYPE_SIGNED:
				VERBOSE_PRINT(errs() << std::to_string(P.getElements<int64_t>(name)[0]) << "\n");
				break;
			case Pack::TYPE_FLOAT:
				VERBOSE_PRINT(errs() << std::to_string(P.getElements<float>(name)[0]) << "\n");
				break;
			case Pack::TYPE_STRING:
				VERBOSE_PRINT(errs() << P.getElements<std::string>(name)[0] << "\n");
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

	// Number of loads and stores are calculated for resource estimation
	unsigned nStore = 0, nLoad = 0;
	VertexIterator vi, viEnd;
	for(std::tie(vi, viEnd) = boost::vertices(graph); vi != viEnd; vi++) {
		int nodeMicroop = microops.at(vertexToName[*vi]);

		if(isStoreOp(nodeMicroop)) nStore++;
		if(isLoadOp(nodeMicroop)) nLoad++;
	}

	loopName2levelUnrollVecMapTy::iterator found = loopName2levelUnrollVecMap.find(loopName);
	assert(found != loopName2levelUnrollVecMap.end() && "Could not find loop in loopName2levelUnrollVecMap");
	std::vector<unsigned> targetUnroll = found->second;
	// Accumulated unroll factor is used to multiply the load/stores according to unroll factors
	// This factor is dependent on the type of DDDG:
	// - If normal loop or before/ater DDDG, we multiply by all above unroll factors (excluding this loop level)
	// - If a between DDDG, we multiply by all above unroll factors and by this loop level unroll factor - 1
	uint64_t accUnrollFactor = (DatapathType::NON_PERFECT_BETWEEN == datapathType)? (targetUnroll.at(loopLevel - 1) - 1) : 1;
	for(unsigned i = loopLevel - 2; i + 1; i--)
		accUnrollFactor *= targetUnroll.at(i);

	P.addDescriptor("_nStore", Pack::MERGE_MULSUM, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("_nStore", nStore);
	P.addElement<uint64_t>("_nStore", accUnrollFactor);
	P.addDescriptor("_nLoad", Pack::MERGE_MULSUM, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("_nLoad", nLoad);
	P.addElement<uint64_t>("_nLoad", accUnrollFactor);

	P.addDescriptor("_tRcIL", Pack::MERGE_SUM, Pack::TYPE_UNSIGNED);
	P.addElement<uint64_t>("_tRcIL", rcIL * accUnrollFactor);

	// TODO Resource estimation is being performed in dumpSummary (not a very good place for this eh?)
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
#ifdef CONSTRAIN_INT_OP
				case HardwareProfile::LIMITED_BY_INTOP:
					P.addElement<std::string>("Units limited by DSP usage", "int op");
					break;
#endif
			}
		}
	}

	if(!(args.fNoMMA))
		memmodel->finishReport();

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
#ifndef BYTE_OPS
			if(LLVM_IR_Add == microops.at(nodeID))
				microops.at(nodeID) = LLVM_IR_IndexAdd;
			else if(LLVM_IR_Sub == microops.at(nodeID))
				microops.at(nodeID) = LLVM_IR_IndexSub;
#else
			if(LLVM_IR_Add == microops.at(nodeID) || LLVM_IR_Add8 == microops.at(nodeID))
				microops.at(nodeID) = LLVM_IR_IndexAdd;
			else if(LLVM_IR_Sub == microops.at(nodeID) || LLVM_IR_Sub8 == microops.at(nodeID))
				microops.at(nodeID) = LLVM_IR_IndexSub;
#endif
		}
		else {
			InEdgeIterator inEdgei, inEdgeEnd;
			for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(*vi, graph); inEdgei != inEdgeEnd; inEdgei++) {
				unsigned parentID = vertexToName[boost::source(*inEdgei, graph)];
				std::string parentInstID = instID.at(parentID);

				if(std::string::npos == parentInstID.find("indvars") && !isIndexOp(microops.at(parentID)))
					continue;

#ifndef BYTE_OPS
				if(LLVM_IR_Add == microops.at(nodeID))
					microops.at(nodeID) = LLVM_IR_IndexAdd;
				else if(LLVM_IR_Sub == microops.at(nodeID))
					microops.at(nodeID) = LLVM_IR_IndexSub;
#else
				if(LLVM_IR_Add == microops.at(nodeID) || LLVM_IR_Add8 == microops.at(nodeID))
					microops.at(nodeID) = LLVM_IR_IndexAdd;
				else if(LLVM_IR_Sub == microops.at(nodeID) || LLVM_IR_Sub8 == microops.at(nodeID))
					microops.at(nodeID) = LLVM_IR_IndexSub;
#endif
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
				finalAddress = relAddress / std::ceil(nextPowerOf2(size) / pFactor);
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
			// If both --fno-slr and --f-slr are omitted, lina will activate it if the inntermost loop is fully unrolled
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

	// XXX: Logic was not changed to handle offchip (if even needed)

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
		// XXX: offchip not being considered

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
		// XXX: offchip not being considered

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

	std::vector<Vertex> topologicalSortedNodes;
	boost::topological_sort(graph, std::back_inserter(topologicalSortedNodes));

	// TODO: this loop was iterated with a node index only and it worked
	// since the "virgin" DDDG is naturally topologically sorted.
	// After the memorymodel, this is not the case anymore, so we need to sort it before running ASAP/ALAP
	// To avoid that, another approach would maintain the DDDG unchanged in terms of nodes.
	// This is possible by creating composite nodes, such as DDRWriteReq+DDRWrite and DDRWrite+DDRWriteResp
	// for example.
	for(auto vi = topologicalSortedNodes.rbegin(); vi != topologicalSortedNodes.rend(); vi++) {
		Vertex currNode = *vi;
		unsigned nodeID = vertexToName[currNode];

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
			unsigned parentOpcode = microops.at(parentNodeID);
#ifdef CHECK_VISITED_NODES
			assert(visitedNodes.find(parentNodeID) != visitedNodes.end() && "Node was not yet visited!");
#endif

			// Inherit dependability from parents
			inheritLoadDepMap(nodeID, parentNodeID);
			inheritStoreDepMap(nodeID, parentNodeID);
			// Also for offchip transactions (in this case MemoryModel is responsible)
			memmodel->inheritLoadDepMap(nodeID, parentNodeID);
			memmodel->inheritStoreDepMap(nodeID, parentNodeID);

			// If parent operation is a header for memory access operation, save this information
			if(LLVM_IR_Load == parentOpcode) addToLoadDepMap(nodeID, parentNodeID);
			if(LLVM_IR_Store == parentOpcode) addToStoreDepMap(nodeID, parentNodeID);
			// Again MemoryModel is responsible for offchip
			if(LLVM_IR_DDRReadReq == parentOpcode) memmodel->addToLoadDepMap(nodeID, parentNodeID);
			if(LLVM_IR_DDRWriteReq == parentOpcode) memmodel->addToStoreDepMap(nodeID, parentNodeID);

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
	VERBOSE_PRINT(errs() << "\t\tASAP scheduling finished\n");

	return std::make_tuple(maxCycles, maxScheduledTime);
}

void BaseDatapath::alapScheduling(std::tuple<uint64_t, uint64_t> asapResult) {
	VERBOSE_PRINT(errs() << "\t\tALAP scheduling started\n");

	alapScheduledTime.assign(numOfTotalNodes, 0);

	std::map<uint64_t, std::set<unsigned>> minTimesNodesMap;
#ifdef CHECK_VISITED_NODES
	std::set<unsigned> visitedNodes;
#endif

	std::vector<Vertex> topologicalSortedNodes;
	boost::topological_sort(graph, std::back_inserter(topologicalSortedNodes));

	// TODO: See the same loop from asap for more info
	for(auto vi = topologicalSortedNodes.begin(); vi != topologicalSortedNodes.end(); vi++) {
		Vertex currNode = *vi;
		unsigned nodeID = vertexToName[currNode];

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

		minTimesNodesMap[minCurrStartTime].insert(nodeID);
	}

	if(dummySinkCreated) {
		VERBOSE_PRINT(errs() << "\t\tAdjusting ALAP values from 0-latency nodes directly connected to the dummy sink\n");

		// Iterate over the nodes connected to the dummy node. If any has 0-latency, subtract 1 from their ALAP
		// if it doesn't violate ASAP. 0-latency nodes can always be executed within a cycle regardless of timing budget,
		// which means they will be allocated to the same cycle as the dummy cycle (as late as possible). Since the dummy
		// cycle does not actually exist, it makes no sense to leave the 0-latency nodes with it. So we bring it back.
		InEdgeIterator inEdgei, inEdgeEnd;
		for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(dummySink, graph); inEdgei != inEdgeEnd; inEdgei++) {
			unsigned parentNodeID = vertexToName[boost::source(*inEdgei, graph)];

			if(!(profile->getLatency(microops.at(parentNodeID)))) {
				unsigned minCurrStartTime = alapScheduledTime[parentNodeID];

				minTimesNodesMap[minCurrStartTime--].erase(parentNodeID);
				minTimesNodesMap[minCurrStartTime].insert(parentNodeID);

				alapScheduledTime[parentNodeID] = minCurrStartTime;
			}
		}
	}

	// Calculate required resources for current scheduling, without imposing any restrictions
	const ConfigurationManager::arrayInfoCfgMapTy &arrayInfoCfgMap = CM.getArrayInfoCfgMap();
	profile->calculateRequiredResources(microops, arrayInfoCfgMap, baseAddress, minTimesNodesMap);

	std::map<uint64_t, std::set<unsigned>>().swap(minTimesNodesMap);

	P.clear();
	profile->fillPack(P, loopLevel, datapathType, 0);
	for(auto &it : P.getStructure()) {
		std::string name = std::get<0>(it);

		// Names starting with "_" are not printed (used for other purposes)
		if('_' == name[0])
			continue;

		VERBOSE_PRINT(errs() << "\t\t" << name << ": ");

		switch(std::get<2>(it)) {
			case Pack::TYPE_UNSIGNED:
				VERBOSE_PRINT(errs() << std::to_string(P.getElements<uint64_t>(name)[0]) << "\n");
				break;
			case Pack::TYPE_SIGNED:
				VERBOSE_PRINT(errs() << std::to_string(P.getElements<int64_t>(name)[0]) << "\n");
				break;
			case Pack::TYPE_FLOAT:
				VERBOSE_PRINT(errs() << std::to_string(P.getElements<float>(name)[0]) << "\n");
				break;
			case Pack::TYPE_STRING:
				VERBOSE_PRINT(errs() << P.getElements<std::string>(name)[0] << "\n");
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
		microops, PC.getResultSizeList(), graph, numOfTotalNodes, nameToVertex, vertexToName,
		*profile, baseAddress, asapScheduledTime, alapScheduledTime, rcScheduledTime
	);
	std::pair<uint64_t, double> rcPair = rcSched.schedule();

	VERBOSE_PRINT(errs() << "\t\tResource-constrained scheduling finished\n");
	return rcPair;
}

std::tuple<std::string, uint64_t> BaseDatapath::calculateResIIMem() {
	// New calculation of ResIIMem is based on two new values:
	// - ResIIMemPort: port-related minimum II constraint
	// - ResIIMemRec: minimum II constrained by memory interface recurrence

	std::tuple<std::string, uint64_t> resIIMemPort = calculateResIIMemPort();
	std::tuple<std::string, uint64_t> resIIMemRec = calculateResIIMemRec();

	return (std::get<1>(resIIMemPort) > std::get<1>(resIIMemRec))? resIIMemPort : resIIMemRec;
}

std::tuple<std::string, uint64_t> BaseDatapath::calculateResIIMemPort() {
	const ConfigurationManager::arrayInfoCfgMapTy arrayInfoCfgMap = CM.getArrayInfoCfgMap();
	const std::map<std::string, std::tuple<uint64_t, uint64_t, uint64_t, unsigned>> &arrayConfig = profile->arrayGetConfig();
	// Boolean flag is false for onchip transactions, true for offchip
	std::map<std::string, uint64_t> arrayPartitionToResII;
	std::map<std::string, bool> isArrayOffchip;
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
				arrayPartitionToResII.insert(std::make_pair(arrayPartitionName, 0));
				isArrayOffchip.insert(std::make_pair(arrayPartitionName, false));
			}
		}
		// Complete or no partitioning
		else {
			arrayPartitionToResII.insert(std::make_pair(arrayName, 0));
			isArrayOffchip.insert(std::make_pair(arrayName, false));
		}
	}

	// Insert offchip arrays. No partitioning is possible in this case
	for(auto &it : arrayInfoCfgMap) {
		if(it.second.type != ConfigurationManager::arrayInfoCfgTy::ARRAY_TYPE_OFFCHIP)
			continue;

		arrayPartitionToResII.insert(std::make_pair(it.first, 0));
		isArrayOffchip.insert(std::make_pair(it.first, true));
	}

	std::map<uint64_t, std::vector<unsigned>> rcToNodes;
	for(unsigned nodeID = 0; nodeID < numOfTotalNodes; nodeID++) {
		if(!boost::degree(nameToVertex[nodeID], graph))
			continue;

		rcToNodes[rcScheduledTime[nodeID]].push_back(nodeID);
	}

	// XXX: Without calculating the load/store distances, we could
	// transform this loop to something simpler and remove the previous loop
	for(auto &it : rcToNodes) {
		//uint64_t currentSched = it.first;

		for(auto &it2 : it.second) {
			unsigned opcode = microops.at(it2);

			if(!(isMemoryOp(opcode) || isDDRLoad(opcode) || isDDRStore(opcode)))
				continue;

			// From this point only loads and stores are considered (including offchip!)

			std::string partitionName = baseAddress.at(it2).first;
#ifdef LEGACY_SEPARATOR
			std::string arrayName = partitionName.substr(0, partitionName.find("-"));
#else
			std::string arrayName = partitionName.substr(0, partitionName.find(GLOBAL_SEPARATOR));
#endif

#if 1
			if(isLoadOp(opcode)) {
				// Complete partitioning, no need to analyse
				if(!(std::get<0>(arrayConfig.at(arrayName))))
					continue;

				arrayPartitionToNumOfReads[partitionName]++;
			}
			if(isDDRLoad(opcode))
				arrayPartitionToNumOfReads[partitionName] += memmodel->getCoalescedReadsFrom(it2);

			if(isStoreOp(opcode)) {
				// Complete partitioning, no need to analyse
				if(!(std::get<0>(arrayConfig.at(arrayName))))
					continue;

				arrayPartitionToNumOfWrites[partitionName]++;
			}
			if(isDDRStore(opcode))
				arrayPartitionToNumOfWrites[partitionName] += memmodel->getCoalescedWritesFrom(it2);
#else
			if(isLoadOp(opcode) || isDDRLoad(opcode)) {
				// Complete partitioning, no need to analyse
				if(!(isArrayOffchip.at(partitionName) || std::get<0>(arrayConfig.at(arrayName))))
					continue;

				arrayPartitionToNumOfReads[partitionName]++;
			}

			if(isStoreOp(opcode) || isDDRStore(opcode)) {
				// Complete partitioning, no need to analyse
				if(!(isArrayOffchip.at(partitionName) || std::get<0>(arrayConfig.at(arrayName))))
					continue;

				arrayPartitionToNumOfWrites[partitionName]++;
			}
#endif
		}
	}

	if(CM.getGlobalCfg<bool>(ConfigurationManager::globalCfgTy::GLOBAL_DDRBANKING)) {
		// With DDR banking, each off-chip array have its own interface, therefore we count it the same way as on-chip array partitions
		for(auto &it : arrayPartitionToResII) {
			std::string partitionName = it.first;

			// Analyse for read
			uint64_t readII = 0;
			std::map<std::string, uint64_t>::iterator found = arrayPartitionToNumOfReads.find(partitionName);
			if(found != arrayPartitionToNumOfReads.end()) {
				uint64_t numReads = found->second;
				uint64_t numReadPorts = isArrayOffchip.at(partitionName)? memmodel->getNumOfReadPorts() : profile->arrayGetPartitionReadPorts(partitionName);

				readII = std::ceil(numReads / (double) numReadPorts);
			}

			// Analyse for write
			uint64_t writeII = 0;
			std::map<std::string, uint64_t>::iterator found3 = arrayPartitionToNumOfWrites.find(partitionName);
			if(found3 != arrayPartitionToNumOfWrites.end()) {
				uint64_t numWrites = found3->second;
				uint64_t numWritePorts = isArrayOffchip.at(partitionName)? memmodel->getNumOfWritePorts() : profile->arrayGetPartitionWritePorts(partitionName);

				writeII = std::ceil(numWrites / (double) numWritePorts);
			}

			it.second = (readII > writeII)? readII : writeII;
		}
	}
	else {
		// Without DDR banking, all off-chip arrays reads use the same interface (likewise for write), thus we count them together
		uint64_t accumulatedOffchipReads = 0;
		uint64_t accumulatedOffchipWrites = 0;

		for(auto &it : arrayPartitionToResII) {
			std::string partitionName = it.first;

			// Analyse for read
			uint64_t readII = 0;
			std::map<std::string, uint64_t>::iterator found = arrayPartitionToNumOfReads.find(partitionName);
			if(found != arrayPartitionToNumOfReads.end()) {
				if(isArrayOffchip.at(partitionName)) {
					accumulatedOffchipReads += found->second;

					// We set readII to 0 to make it irrelevant through the max search
					readII = 0;
				}
				else {
					uint64_t numReads = found->second;
					uint64_t numReadPorts = profile->arrayGetPartitionReadPorts(partitionName);

					readII = std::ceil(numReads / (double) numReadPorts);
				}
			}

			// Analyse for write
			uint64_t writeII = 0;
			std::map<std::string, uint64_t>::iterator found3 = arrayPartitionToNumOfWrites.find(partitionName);
			if(found3 != arrayPartitionToNumOfWrites.end()) {
				if(isArrayOffchip.at(partitionName)) {
					accumulatedOffchipWrites += found->second;

					// We set writeII to 0 to make it irrelevant through the max search
					writeII = 0;
				}
				else {
					uint64_t numWrites = found3->second;
					uint64_t numWritePorts = profile->arrayGetPartitionWritePorts(partitionName);

					writeII = std::ceil(numWrites / (double) numWritePorts);
				}
			}

			it.second = (readII > writeII)? readII : writeII;
		}

		// If any offchip read or write was counted, we insert an additional candidate for resIIMem considering the offchip interface
		if(accumulatedOffchipReads || accumulatedOffchipWrites) {
			uint64_t readII = std::ceil(accumulatedOffchipReads / (double) memmodel->getNumOfReadPorts());
			uint64_t writeII = std::ceil(accumulatedOffchipWrites / (double) memmodel->getNumOfWritePorts());

			arrayPartitionToResII.insert(std::make_pair("(gmem)", (readII > writeII)? readII : writeII));
		}
	}

	std::map<std::string, uint64_t>::iterator maxIt = std::max_element(arrayPartitionToResII.begin(), arrayPartitionToResII.end(), prioritiseLargerResIIMem);

	if(arrayPartitionToResII.size() && maxIt->second > 1)
		return std::make_tuple(maxIt->first, maxIt->second);
	else
		return std::make_tuple("none", 1);
}

std::tuple<std::string, uint64_t> BaseDatapath::calculateResIIMemRec() {
	std::pair<std::string, uint64_t> gMax = std::make_pair("none", 1);
	std::pair<std::string, uint64_t> lMax = std::make_pair("none", 1);

	// Logic for global memory

	if(!(args.fNoMMA))
		memmodel->calculateResIIMemRec(rcScheduledTime);

	// Logic for local memory

	std::set<unsigned> visited;
	std::set<unsigned> toVisit;
	std::function<void(const unsigned &, std::set<unsigned> &, std::unordered_map<unsigned, std::set<unsigned>> &)> recursiveBlock;
	recursiveBlock = [&recursiveBlock, &visited](const unsigned &v, std::set<unsigned> &connected, std::unordered_map<unsigned, std::set<unsigned>> &depMap) {
		visited.insert(v);
		connected.insert(v);

		for(auto &it : depMap.at(v)) {
			if(!(visited.count(it)))
				recursiveBlock(it, connected, depMap);
		}
	};
	// Using this struct instead of a normal pair in the map customises the default initialiser
	struct minMaxPair {
		std::pair<uint64_t, uint64_t> value = std::make_pair(std::numeric_limits<uint64_t>::max(), 0);
		uint64_t distance() const { return value.second - value.first; }
	};

	// XXX Loads do not have recurrence constraint on local memories. The whole segment was commented to reduce execution time
#if 0
	// Logic for loads

	std::vector<std::pair<std::string, uint64_t>> loadMaxs;
	std::unordered_map<std::string, uint64_t> connectedLoadGraphs;

	// Consider only loads (and make them symmetric to facilitate calculation)
	for(auto &it : loadDepMap) {
		if(microops.at(it.first) != LLVM_IR_Load)
			continue;

		toVisit.insert(it.first);

		for(auto &it2 : it.second)
			loadDepMap[it2].insert(it.first);
	}

	for(auto &it : toVisit) {
		if(!(visited.count(it))) {
			std::set<unsigned> connected;
			recursiveBlock(it, connected, loadDepMap);
			std::set<std::string> consideredInterfaces;

			// Find smallest and largest allocation value for each memory interface
			// (if banking is disabled, all global loads share the same interface)
			std::unordered_map<std::string, minMaxPair> minMaxPerInterface;
			for(auto &it2 : connected) {
				std::string arrayPartitionName = baseAddress.at(it2).first;
				if(rcScheduledTime[it2] < minMaxPerInterface[arrayPartitionName].value.first)
					minMaxPerInterface[arrayPartitionName].value.first = rcScheduledTime[it2];
				if(rcScheduledTime[it2] > minMaxPerInterface[arrayPartitionName].value.second)
					minMaxPerInterface[arrayPartitionName].value.second = rcScheduledTime[it2];

				if(!(consideredInterfaces.count(arrayPartitionName))) {
					consideredInterfaces.insert(arrayPartitionName);
					(connectedLoadGraphs[arrayPartitionName])++;
				}
			}

			// Save all distances to the load max vector
			for(auto &it2 : minMaxPerInterface)
				loadMaxs.push_back(std::make_pair(it2.first, it2.second.distance()));
		}
	}
#endif

	// Logic for stores

	std::vector<std::pair<std::string, uint64_t>> storeMaxs;
	std::unordered_map<std::string, uint64_t> connectedStoreGraphs;

	visited.clear();
	toVisit.clear();

	// Consider only stores (and make them symmetric to facilitate calculation)
	for(auto &it : storeDepMap) {
		if(microops.at(it.first) != LLVM_IR_Store)
			continue;

		toVisit.insert(it.first);

		for(auto &it2 : it.second)
			storeDepMap[it2].insert(it.first);
	}

	for(auto &it : toVisit) {
		if(!(visited.count(it))) {
			std::set<unsigned> connected;
			recursiveBlock(it, connected, storeDepMap);
			std::set<std::string> consideredInterfaces;

			// Find smallest and largest allocation value for each memory interface
			// (if banking is disabled, all global stores share the same interface)
			std::unordered_map<std::string, minMaxPair> minMaxPerInterface;
			for(auto &it2 : connected) {
				std::string arrayPartitionName = baseAddress.at(it2).first;
				if(rcScheduledTime[it2] < minMaxPerInterface[arrayPartitionName].value.first)
					minMaxPerInterface[arrayPartitionName].value.first = rcScheduledTime[it2];
				if(rcScheduledTime[it2] > minMaxPerInterface[arrayPartitionName].value.second)
					minMaxPerInterface[arrayPartitionName].value.second = rcScheduledTime[it2];

				if(!(consideredInterfaces.count(arrayPartitionName))) {
					consideredInterfaces.insert(arrayPartitionName);
					(connectedStoreGraphs[arrayPartitionName])++;
				}
			}

			// Save all distances to the store max vector
			for(auto &it2 : minMaxPerInterface)
				storeMaxs.push_back(std::make_pair(it2.first, it2.second.distance()));
		}
	}

	// There is a key difference between Lina and Vivado regarding allocation of load/stores.
	// While Lina always allocate no before than ALAP, Vivado create some "allocation pits"
	// and attempt to concentrate all loads around these pits. The same applies for stores.
	// Vivado does even input some bubbles to move these load/stores around.
	// The intention is to reduce the distance among dependent loads and therefore reduce the
	// II constraint that is being calculated here. Lina does not perform this type of allocation
	// and problems might arise, for example when two dependence graphs can be allocated at the
	// same window and have the same distance. Naturally they cannot be allocated to the same
	// cycle as it violates port usage. In this case we approximate the correct behaviour by
	// adding the amount of dependence graphs that we found, guaranteeing that the II here
	// calculated will still respect interface usage

	// Load segment was commented to reduce execution time
#if 0
	auto prioritiseLoadLargerCompensatedDistance = [&connectedLoadGraphs](const std::pair<std::string, uint64_t> &a, const std::pair<std::string, uint64_t> &b) {
		return (a.second + connectedLoadGraphs.at(a.first)) < (b.second + connectedLoadGraphs.at(b.first));
	};
	std::vector<std::pair<std::string, uint64_t>>::iterator loadMaxIt = std::max_element(loadMaxs.begin(), loadMaxs.end(), prioritiseLoadLargerCompensatedDistance);
	std::pair<std::string, uint64_t> loadMax = (loadMaxs.size() && loadMaxIt->second > 1)?
		std::make_pair(loadMaxIt->first, loadMaxIt->second + connectedLoadGraphs.at(loadMaxIt->first)) : std::make_pair("none", 1);
#endif

	auto prioritiseStoreLargerCompensatedDistance = [&connectedStoreGraphs](const std::pair<std::string, uint64_t> &a, const std::pair<std::string, uint64_t> &b) {
		return (a.second + connectedStoreGraphs.at(a.first)) < (b.second + connectedStoreGraphs.at(b.first));
	};
	std::vector<std::pair<std::string, uint64_t>>::iterator storeMaxIt = std::max_element(storeMaxs.begin(), storeMaxs.end(), prioritiseStoreLargerCompensatedDistance);
	std::pair<std::string, uint64_t> storeMax = (storeMaxs.size() && storeMaxIt->second > 1)?
		std::make_pair(storeMaxIt->first, storeMaxIt->second + connectedStoreGraphs.at(storeMaxIt->first)) : std::make_pair("none", 1);

	// Load segment was commented to reduce execution time
#if 0
	// At last choose the largest
	lMax = (loadMax.second > storeMax.second)? loadMax : storeMax;
#else
	lMax = storeMax;
#endif

	// And finally get the biggest among global and local memory
	return (gMax.second > lMax.second)? gMax : lMax;
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
					assert(timeStamp <= currAsapII && "Did not find any critical path nodes in the timestamp window search");
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

		uint64_t extraEnter = EXTRA_ENTER_LOOP_LATENCY;
		uint64_t extraExit = EXTRA_EXIT_LOOP_LATENCY;
		bool allocatedDDDGBefore = false;
		bool allocatedDDDGAfter = false;

		// For the first loop level right after the DDDGs, we analyse for out-bursts
		if(loopLevel - 1 == i) {
			// If there are out-burst nodes to allocate before DDDG, we add on the extra cycles
			for(auto &it : memmodel->getNodesToBeforeDDDG()) {
				// We don't use profile->getLatency() here because the opcode might be silent
				// and here we want the non-silent case. We could call getNonSilentOpcode(),
				// but since we already have this calculated, why not use it?
				unsigned latency = it.node.nonSilentLatency;
				if(latency >= extraEnter)
					extraEnter = latency;

				allocatedDDDGBefore = true;
			}
			// If there are out-burst nodes to allocate after DDDG, we add on the extra cycles
			for(auto &it : memmodel->getNodesToAfterDDDG()) {
				// Look explanation on the previous loop
				unsigned latency = it.node.nonSilentLatency;
				if(latency >= extraExit)
					extraExit = latency;

				allocatedDDDGAfter = true;
			}
		}

		uint64_t extraEnterExit = extraEnter + extraExit;

		if(loopLevel - 1 == i)
			noPipelineLatency = rcIL * (currentLoopBound / unrollFactor) + extraEnterExit;
		else if(i)
			noPipelineLatency = noPipelineLatency * (currentLoopBound / unrollFactor) + extraEnterExit;
		else
			noPipelineLatency = noPipelineLatency * (currentLoopBound / unrollFactor);

		if(i) {
			unsigned upperLoopUnrollFactor = targetUnroll.at(i - 1);

			noPipelineLatency *= upperLoopUnrollFactor;

			bool shouldShrink =
				(loopLevel - 1 == i)? (upperLoopUnrollFactor > 1) && !(allocatedDDDGBefore && allocatedDDDGAfter && memmodel->canOutBurstsOverlap()) : true;

			// We consider EXTRA_ENTER_EXIT_LOOP_LATENCY as the overhead latency for a loop. When two consecutive loops
			// are present, a cycle for each loop overhead can be merged (i.e. the exit condition of a loop can be evaluated
			// at the same time as the enter condition of the following loop). Since right now consecutive inner loops are only
			// possible with unroll, we compensate this cycle difference with the loop unroll factor
			// XXX: However, if there are DDR operations before AND after the DDDG, we do not merge if the address space overlap
			// TODO: Requires further testing
			if(shouldShrink)
				noPipelineLatency -= (upperLoopUnrollFactor - 1) * std::min(extraEnter, extraExit);
		}
	}

	// TODO: Requires further testing (possible unconsidered cases regarding out-bursted transactions)
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

		uint64_t extraEnter = EXTRA_ENTER_LOOP_LATENCY;
		uint64_t extraExit = EXTRA_EXIT_LOOP_LATENCY;
		// If there are out-burst nodes to allocate before DDDG, we add on the extra cycles
		for(auto &it : memmodel->getNodesToBeforeDDDG()) {
			// We don't use profile->getLatency() here because the opcode might be silent
			// and here we want the non-silent case. We could call getNonSilentOpcode(),
			// but since we already have this calculated, why not use it?
			unsigned latency = it.node.nonSilentLatency;
			if(latency >= extraEnter)
				extraEnter = latency;
		}
		// If there are out-burst nodes to allocate after DDDG, we add on the extra cycles
		for(auto &it : memmodel->getNodesToAfterDDDG()) {
			// Look explanation on the previous loop
			unsigned latency = it.node.nonSilentLatency;
			if(latency >= extraExit)
				extraExit = latency;
		}
		uint64_t extraEnterExit = extraEnter + extraExit;

		pipelinedLatency = (maxII * (currentIterations - 1) + rcIL + extraEnterExit) * totalIterations;
	}

	return enablePipelining? pipelinedLatency : noPipelineLatency;
}

void BaseDatapath::inheritLoadDepMap(unsigned targetID, unsigned sourceID) {
	loadDepMap[targetID].insert(loadDepMap[sourceID].begin(), loadDepMap[sourceID].end());
}

void BaseDatapath::inheritStoreDepMap(unsigned targetID, unsigned sourceID) {
	storeDepMap[targetID].insert(storeDepMap[sourceID].begin(), storeDepMap[sourceID].end());
}

void BaseDatapath::addToLoadDepMap(unsigned targetID, unsigned toAddID) {
	// XXX falseDeps deactivated so far because no control edges are added in the Lina logic outside of MemoryModel
	//if(falseDeps.count(std::make_pair(toAddID, targetID)))
		loadDepMap[targetID].insert(toAddID);
}

void BaseDatapath::addToStoreDepMap(unsigned targetID, unsigned toAddID) {
	// XXX falseDeps deactivated so far because no control edges are added in the Lina logic outside of MemoryModel
	//if(falseDeps.count(std::make_pair(toAddID, targetID)))
		storeDepMap[targetID].insert(toAddID);
}

void BaseDatapath::dumpSummary(
	uint64_t numCycles, uint64_t asapII, double achievedPeriod,
	uint64_t maxII, std::tuple<std::string, uint64_t> resIIMem, std::tuple<std::string, uint64_t> resIIOp, uint64_t recII
) {
	*summaryFile << "=======================================================================\n";
	if(args.fNoTCS)
		*summaryFile << "Time-constrained scheduling disabled\n";
	*summaryFile << "Target clock: " << std::to_string(args.frequency) << " MHz\n";
	*summaryFile << "Clock uncertainty: " << std::to_string(args.uncertainty) << " %\n";
	*summaryFile << "Target clock period: " << std::to_string(1000 / args.frequency) << " ns\n";
	*summaryFile << "Effective clock period: " << std::to_string((1000 / args.frequency) - (10 * args.uncertainty / args.frequency)) << " ns\n";
	*summaryFile << "Achieved clock period: " << std::to_string(achievedPeriod) << " ns\n";
	*summaryFile << "Loop name: " << loopName << "\n";
	*summaryFile << "Loop level: " << std::to_string(loopLevel) << "\n";

	bool isFullBody = false;
	*summaryFile << "DDDG type: ";
	switch(datapathType) {
		case DatapathType::NON_PERFECT_BEFORE:
			*summaryFile << "anterior part of loop body (before any nested loops)\n";
			break;
		case DatapathType::NON_PERFECT_AFTER:
			*summaryFile << "posterior part of loop body (after any nested loops)\n";
			break;
		case DatapathType::NON_PERFECT_BETWEEN:
			*summaryFile << "posterior + anterior (between unrolled iterations)\n";
			break;
		default:
			isFullBody = true;
			*summaryFile << "full loop body\n";
	}

	*summaryFile << "Loop unrolling factor: " << std::to_string(loopUnrollFactor) << "\n";
	*summaryFile << "Loop pipelining enabled? " << (enablePipelining? "yes" : "no") << "\n";
	*summaryFile << "Total cycles: " << std::to_string(numCycles) << "\n";
	if(args.fNPLA && isFullBody) {
		*summaryFile << "NOTE: the cycle count above does not consider non-perfect loop nests!\n";
		*summaryFile << "      If applicable (e.g. the loop is non-perfect), please see section\n";
		*summaryFile << "      \"Non-perfect loop analysis results\" at the end of this file\n";
		*summaryFile << "      for correct cycle count.\n";
	}
	*summaryFile << "-----------------------------------------------------------------------\n";

	if(sharedLoadsRemoved)
		*summaryFile << "Number of shared loads detected: " << std::to_string(sharedLoadsRemoved) << "\n";
	if(repeatedStoresRemoved)
		*summaryFile << "Number of repeated stores detected: " << std::to_string(repeatedStoresRemoved) << "\n";
	if(sharedLoadsRemoved || repeatedStoresRemoved)
		*summaryFile << "-----------------------------------------------------------------------\n";

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
	*summaryFile << "-----------------------------------------------------------------------\n";

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
#ifdef CONSTRAIN_INT_OP
				case HardwareProfile::LIMITED_BY_INTOP:
					unitName = "int op";
					break;
#endif
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
		*summaryFile << "-----------------------------------------------------------------------\n";
	}

	/* XXX Resource estimation! */

	// Finalise shared resources calculation
	unsigned sharedFU = 0;
	unsigned sharedDSP = 0;
	unsigned sharedFF = 0;
	unsigned sharedLUT = 0;
#ifdef LEGACY_SEPARATOR
	std::string value = P.mergeElements<Pack::resourceNodeTy>("_shared~fadd");
#else
	std::string value = P.mergeElements<Pack::resourceNodeTy>("_shared" GLOBAL_SEPARATOR "fadd");
#endif
	// Unpacking the values from the string. Each variable has 10 characters allocated
	// I know this is ugly, please don't kill me...
	sharedFU += stol(value.substr(0, 10));
	sharedDSP += stol(value.substr(10, 10));
	sharedFF += stol(value.substr(20, 10));
	sharedLUT += stol(value.substr(30, 10));
#ifdef LEGACY_SEPARATOR
	value = P.mergeElements<Pack::resourceNodeTy>("_shared~fsub");
#else
	std::string value = P.mergeElements<Pack::resourceNodeTy>("_shared" GLOBAL_SEPARATOR "fsub");
#endif
	sharedFU += stol(value.substr(0, 10));
	sharedDSP += stol(value.substr(10, 10));
	sharedFF += stol(value.substr(20, 10));
	sharedLUT += stol(value.substr(30, 10));
#ifdef LEGACY_SEPARATOR
	value = P.mergeElements<Pack::resourceNodeTy>("_shared~fmul");
#else
	std::string value = P.mergeElements<Pack::resourceNodeTy>("_shared" GLOBAL_SEPARATOR "fmul");
#endif
	sharedFU += stol(value.substr(0, 10));
	sharedDSP += stol(value.substr(10, 10));
	sharedFF += stol(value.substr(20, 10));
	sharedLUT += stol(value.substr(30, 10));
#ifdef LEGACY_SEPARATOR
	value = P.mergeElements<Pack::resourceNodeTy>("_shared~fdiv");
#else
	std::string value = P.mergeElements<Pack::resourceNodeTy>("_shared" GLOBAL_SEPARATOR "fdiv");
#endif
	sharedFU += stol(value.substr(0, 10));
	sharedDSP += stol(value.substr(10, 10));
	sharedFF += stol(value.substr(20, 10));
	sharedLUT += stol(value.substr(30, 10));

	// Finalise unshared resources calculation
	unsigned unsharedFU = 0;
	unsigned unsharedDSP = 0;
	unsigned unsharedFF = 0;
	unsigned unsharedLUT = 0;
	for(auto &it : P.getStructure()) {
		std::string name = std::get<0>(it);

#ifdef LEGACY_SEPARATOR
		if(!(name.compare(0, 10, "_unshared~"))) {
#else
		if(!(name.compare(0, 10, "_unshared" GLOBAL_SEPARATOR))) {
#endif
			value = P.mergeElements<Pack::resourceNodeTy>(name);
			unsharedFU += stol(value.substr(0, 10));
			unsharedDSP += stol(value.substr(10, 10));
			unsharedFF += stol(value.substr(20, 10));
			unsharedLUT += stol(value.substr(30, 10));
		}
	}

	std::string wholeLoopName = appendDepthToLoopName(loopName, 1);
	wholeloopName2loopBoundMapTy::iterator found = wholeloopName2loopBoundMap.find(wholeLoopName);
	uint64_t loopBound = found->second;
	// Use all bounds
	for(unsigned i = 2; i <= LpName2numLevelMap.at(loopName); i++) {
		wholeLoopName = appendDepthToLoopName(loopName, i);
		found = wholeloopName2loopBoundMap.find(wholeLoopName);
		loopBound *= found->second;
	}

	assert("true" == P.mergeElements<uint64_t>("_memlogicFF") && "Merged values from datapaths differ where it should not differ (_memlogicFF)");
	unsigned mlFF = P.getElements<uint64_t>("_memlogicFF")[0];
	assert("true" == P.mergeElements<uint64_t>("_memlogicLUT") && "Merged values from datapaths differ where it should not differ (_memlogicLUT)");
	unsigned mlLUT = P.getElements<uint64_t>("_memlogicLUT")[0];

	uint64_t nStore = stol(P.mergeElements<uint64_t>("_nStore"));
	uint64_t nLoad = stol(P.mergeElements<uint64_t>("_nLoad"));
	uint64_t nOp = sharedFU + unsharedFU;
	uint64_t tRcIL = stol(P.mergeElements<uint64_t>("_tRcIL"));
	unsigned lK = LpName2numLevelMap.at(loopName);
	unsigned e = logNextPowerOf2(loopBound);
	unsigned V1 = e + 1, V2 = 2 * e, V3 = e + 2;

	for(auto &it : P.getStructure()) {
		std::string name = std::get<0>(it);

		// Names starting with "_" are not printed (used for other purposes)
		if('_' == name[0])
			continue;

		// Special treament for certain merges (other arithmetics are performed instead of simple merge)
		if("DSPs" == name) {
			uint64_t value = sharedDSP + unsharedDSP;

			*summaryFile << name << ": " << value << "\n";
			continue;
		}
		else if("FFs" == name) {
			unsigned rFF = 32 * (nLoad + nStore + nOp) + tRcIL + (1 == lK? 1 : 2) * V1 * lK;
			uint64_t value = sharedFF + unsharedFF + rFF + mlFF;

			*summaryFile << name << ": " << value << "\n";
			continue;
		}
		else if("LUTs" == name) {
			unsigned mLUT = 32 * (nStore + nOp) + 14 * nLoad + V1 * lK;
			// Use all unrolls
			loopName2levelUnrollVecMapTy::iterator found2 = loopName2levelUnrollVecMap.find(loopName);
			assert(found2 != loopName2levelUnrollVecMap.end() && "Could not find loop in loopName2levelUnrollVecMap");
			std::vector<unsigned> targetUnroll = found2->second;
			uint64_t accUnrollFactor = 1;
			for(unsigned i = loopLevel - 1; i + 1; i--)
				accUnrollFactor *= targetUnroll.at(i);
			unsigned exLUT = (V1 + V2 + V3) * lK + V1 * (accUnrollFactor - 1);

			uint64_t value = sharedLUT + unsharedLUT + mLUT + exLUT + mlLUT;

			*summaryFile << name << ": " << value << "\n";
			continue;
		}

		*summaryFile << name << ": ";

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
		(DatapathType::NON_PERFECT_BEFORE == datapathType)? "_before" : ((DatapathType::NON_PERFECT_AFTER == datapathType)? "_after" : ((DatapathType::NON_PERFECT_BETWEEN == datapathType)? "_inter" : "" ))
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
	const std::vector<int> &microops, const std::unordered_map<int, unsigned> &resultSizeList,
	const Graph &graph, unsigned numOfTotalNodes,
	const std::unordered_map<unsigned, Vertex> &nameToVertex, const VertexNameMap &vertexToName,
	HardwareProfile &profile, const std::unordered_map<int, std::pair<std::string, int64_t>> &baseAddress,
	const std::vector<uint64_t> &asap, const std::vector<uint64_t> &alap, std::vector<uint64_t> &rc
) :
	microops(microops), resultSizeList(resultSizeList),
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
	ddrOpReady.clear();
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
	ddrOpSelected.clear();

	fAddExecuting.clear();
	fSubExecuting.clear();
	fMulExecuting.clear();
	fDivExecuting.clear();
	fCmpExecuting.clear();
	loadExecuting.clear();
	storeExecuting.clear();
	intOpExecuting.clear();
	callExecuting.clear();
	ddrOpExecuting.clear();

	// Select root connected nodes to start scheduling
	VertexIterator vi, vEnd;
	for(std::tie(vi, vEnd) = boost::vertices(graph); vi != vEnd; vi++) {
		unsigned currNodeID = vertexToName[*vi];

		if(!(boost::degree(*vi, graph)))
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
			(DatapathType::NON_PERFECT_BEFORE == datapathType)? "_before" : ((DatapathType::NON_PERFECT_AFTER == datapathType)? "_after" : ((DatapathType::NON_PERFECT_BETWEEN == datapathType)? "_inter" : "" ))
		);
		dumpFile.open(args.outWorkDir + appendDepthToLoopName(loopName, loopLevel) + datapathTypeStr + ".sched.rpt");

		dumpFile << "=======================================================================\n";
		dumpFile << "Lina scheduling report file\n";
		dumpFile << "Loop name: " << loopName << "\n";
		if(args.fNoTCS)
			dumpFile << "Time-constrained scheduling disabled\n";
		dumpFile << "Target clock: " << std::to_string(args.frequency) << " MHz\n";
		dumpFile << "Clock uncertainty: " << std::to_string(args.uncertainty) << " %\n";
		dumpFile << "Target clock period: " << std::to_string(1000 / args.frequency) << " ns\n";
		dumpFile << "Effective clock period: " << std::to_string((1000 / args.frequency) - (10 * args.uncertainty / args.frequency)) << " ns\n";
		dumpFile << "-----------------------------------------------------------------------\n";
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

		// Before selecting, we must deduce the in-cycle latency that is being held by running instructions
		if(!(args.fNoTCS))
			tcSched.clearFinishedNodes();

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
		ddrOpExecuted.clear();

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
		dumpFile << "=======================================================================\n";
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
	trySelect(ddrOpReady, ddrOpSelected, &HardwareProfile::ddrOpTryAllocate);
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
	enqueueExecute(ddrOpSelected, ddrOpExecuting, &HardwareProfile::ddrOpRelease);
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
	tryRelease(ddrOpExecuting, ddrOpExecuted, &HardwareProfile::ddrOpRelease);
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
#ifdef CONSTRAIN_INT_OP
		case LLVM_IR_And:
		case LLVM_IR_Or:
		case LLVM_IR_Xor:
		case LLVM_IR_Shl:
		case LLVM_IR_AShr:
		case LLVM_IR_LShr:
#ifdef BYTE_OPS
		case LLVM_IR_Add8:
		case LLVM_IR_Sub8:
		case LLVM_IR_Mul8:
		case LLVM_IR_UDiv8:
		case LLVM_IR_SDiv8:
		case LLVM_IR_And8:
		case LLVM_IR_Or8:
		case LLVM_IR_Xor8:
		case LLVM_IR_Shl8:
		case LLVM_IR_AShr8:
		case LLVM_IR_LShr8:
#endif
#ifdef CUSTOM_OPS
		case LLVM_IR_APAdd:
		case LLVM_IR_APSub:
		case LLVM_IR_APMul:
		case LLVM_IR_APDiv:
#endif
#endif
			intOpReady.push_back(std::make_pair(nodeID, tick));
			break;
		case LLVM_IR_Call:
			callReady.push_back(std::make_pair(nodeID, tick));
			break;
		case LLVM_IR_DDRReadReq:
		case LLVM_IR_DDRRead:
		case LLVM_IR_DDRWriteReq:
		case LLVM_IR_DDRWrite:
		case LLVM_IR_DDRWriteResp:
		case LLVM_IR_DDRSilentReadReq:
		case LLVM_IR_DDRSilentRead:
		case LLVM_IR_DDRSilentWriteReq:
		case LLVM_IR_DDRSilentWrite:
		case LLVM_IR_DDRSilentWriteResp:
			ddrOpReady.push_back(std::make_pair(nodeID, tick));
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

void BaseDatapath::RCScheduler::trySelect(nodeTickTy &ready, selectedListTy &selected, bool (HardwareProfile::*tryAllocateOp)(int, bool)) {
	if(ready.size()) {
		selected.clear();

		// Sort nodes by their ALAP, smallest first (urgent nodes first)
		ready.sort(prioritiseSmallerALAP);
#ifdef CONSTRAIN_INT_OP
		for(auto it = ready.begin(); it != ready.end();) {
			bool iteratorWasInvalidated = false;
			unsigned nodeID = it->first;
#else
		size_t initialReadySize = ready.size();
		for(unsigned i = 0; i < initialReadySize; i++) {
			unsigned nodeID = ready.front().first;
#endif

			// If allocation is successful (i.e. there is one operation unit available), select this operation
			// If timing-constrained scheduling is enabled, allocation is not yet performed, only attempted
			if((profile.*tryAllocateOp)(microops.at(nodeID), args.fNoTCS)) {
				bool timingConstrained = false;

				// Timing-constrained scheduling (taa-daa)
				if(!(args.fNoTCS)) {
					// If selecting the current node does not violate timing in any way, proceed
					if(tcSched.tryAllocate(nodeID))
						(profile.*tryAllocateOp)(microops.at(nodeID), true);
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
#ifdef CONSTRAIN_INT_OP
					it = ready.erase(it);
					iteratorWasInvalidated = true;
#else
					ready.pop_front();
#endif
					readyChanged = true;
					rc[nodeID] = cycleTick;
				}
#ifdef CONSTRAIN_INT_OP
				// Timing contention, not able to allocate now (but the next, less-prioritised node might allocate, so no break here)
#else
				// Timing contention, not able to allocate now
				else {
					break;
				}
#endif
			}
#ifdef CONSTRAIN_INT_OP
			// Resource contention, not able to allocate now (but the next, less-prioritised node might allocate, so no break here)

			// Increment logic of iterator depends whether the iterator was invalidated by an erase (and later reconstructed)
			if(!iteratorWasInvalidated)
				it++;
#else
			// Resource contention, not able to allocate now
			else {
				break;
			}
#endif
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

void BaseDatapath::RCScheduler::trySelect(nodeTickTy &ready, selectedListTy &selected, bool (HardwareProfile::*tryAllocateDDRMem)(unsigned, int, bool)) {
	// XXX: On the other trySelect() logics, if there is timing contention and/or resource contention for
	// the first candidate, trySelect() already fails (i.e. the else { break } statements). This is expected
	// for operations where if one fails, for sure the next one won't be able to succeed.
	// But in the DDR case, if the most prioritised node fails, the next one might still succeed.
	// XXX: Please also note that since elements from the middle of this ready queue can be selected
	// (which doesn't happen in the other trySelects), this iteration loop is slightly different

	if(ready.size()) {
		selected.clear();

		// Sort nodes by their ALAP, smallest first (urgent nodes first)
		ready.sort(prioritiseSmallerALAP);
		for(auto it = ready.begin(); it != ready.end(); it++) {
			unsigned nodeID = it->first;
			int microop = microops.at(nodeID);

			// If allocation is successful (i.e. there is one operation unit available), select this operation
			// If timing-constrained scheduling is enabled, allocation is not yet performed, only attempted
			if((profile.*tryAllocateDDRMem)(nodeID, microop, args.fNoTCS)) {
				bool timingConstrained = false;

				// Timing-constrained scheduling (taa-daa)
				if(!(args.fNoTCS)) {
					// If selecting the current node does not violate timing in any way, proceed
					if(tcSched.tryAllocate(nodeID))
						(profile.*tryAllocateDDRMem)(nodeID, microop, true);
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
					it = ready.erase(it);
					readyChanged = true;
					rc[nodeID] = cycleTick;
				}
				// Timing contention, not able to allocate now (but the next, less-prioritised node might allocate, so no break here)
			}
			// Resource contention, not able to allocate now (but the next, less-prioritised node might allocate, so no break here)
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

void BaseDatapath::RCScheduler::enqueueExecute(selectedListTy &selected, executingMapTy &executing, void (HardwareProfile::*releaseOp)(int)) {
	while(selected.size()) {
		unsigned selectedNodeID = selected.front();
		int opcode = microops.at(selectedNodeID);
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

void BaseDatapath::RCScheduler::enqueueExecute(selectedListTy &selected, executingMapTy &executing, void (HardwareProfile::*releaseDDRMem)(unsigned, int)) {
	while(selected.size()) {
		unsigned selectedNodeID = selected.front();
		int opcode = microops.at(selectedNodeID);
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

			// Inform TCS that this node should be accounted from the next cycle timing budget as it is still running
			if(!(args.fNoTCS))
				tcSched.markAsRunning(executingNodeID);
		}
	}

	for(auto &it : toErase)
		executing.erase(it);
}

void BaseDatapath::RCScheduler::tryRelease(executingMapTy &executing, executedListTy &executed, void (HardwareProfile::*releaseOp)(int)) {
	std::vector<unsigned> toErase;

	for(auto &it: executing) {
		unsigned executingNodeID = it.first;
		int opcode = microops.at(executingNodeID);

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
				(profile.*releaseOp)(opcode);
		}
		else {
			if(args.showScheduling)
				dumpFile << "\t[ALLOCATED] [" << std::to_string(it.second + 1) << "/" <<  std::to_string(profile.getLatency(opcode)) << "] Node " << executingNodeID << " (" << reverseOpcodeMap.at(opcode) << ")\n";

			// Inform TCS that this node should be accounted from the next cycle timing budget as it is still running
			if(!(args.fNoTCS))
				tcSched.markAsRunning(executingNodeID);
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

			// Inform TCS that this node should be accounted from the next cycle timing budget as it is still running
			if(!(args.fNoTCS))
				tcSched.markAsRunning(executingNodeID);
		}
	}

	for(auto &it : toErase)
		executing.erase(it);
}

void BaseDatapath::RCScheduler::tryRelease(executingMapTy &executing, executedListTy &executed, void (HardwareProfile::*releaseDDRMem)(unsigned, int)) {
	std::vector<unsigned> toErase;

	for(auto &it: executing) {
		unsigned executingNodeID = it.first;
		int opcode = microops.at(executingNodeID);

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
				(profile.*releaseDDRMem)(executingNodeID, opcode);
		}
		else {
			if(args.showScheduling)
				dumpFile << "\t[ALLOCATED] [" << std::to_string(it.second + 1) << "/" <<  std::to_string(profile.getLatency(opcode)) << "] Node " << executingNodeID << " (" << reverseOpcodeMap.at(opcode) << ")\n";

			// Inform TCS that this node should be accounted from the next cycle timing budget as it is still running
			if(!(args.fNoTCS))
				tcSched.markAsRunning(executingNodeID);
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
	delayMap.clear();
	runningNodes.clear();
}

void BaseDatapath::TCScheduler::clearFinishedNodes() {
	// XXX Maybe this logic could be further optimised

	// Clear delay map and add the nodes that are still executing
	// Note that order matters! This is why std::vector<> is being used
	delayMap.clear();
	for(auto &it : runningNodes)
		tryAllocate(it, false);

	runningNodes.clear();
}

void BaseDatapath::TCScheduler::markAsRunning(unsigned nodeID) {
	runningNodes.push_back(nodeID);
}

bool BaseDatapath::TCScheduler::tryAllocate(unsigned nodeID, bool checkTiming) {
	double inCycleLatency = profile.getInCycleLatency(microops.at(nodeID));

	// Calculate the delay up to this node according to its parent nodes
	double nodeDelay = inCycleLatency;
	double parentLargestDelay = 0;
	InEdgeIterator inEdgei, inEdgeEnd;
	for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(nameToVertex.at(nodeID), graph); inEdgei != inEdgeEnd; inEdgei++) {
		unsigned parentID = vertexToName[boost::source(*inEdgei, graph)];
		std::unordered_map<unsigned, double>::iterator found = delayMap.find(parentID);
		double parentDelay = (delayMap.end() == found)? 0 : found->second;
		if(parentDelay > parentLargestDelay)
			parentLargestDelay = parentDelay;
	}
	nodeDelay += parentLargestDelay;

	// Fail if adding this new node violates timing
	if(checkTiming && nodeDelay > effectivePeriod)
		return false;

	// Add node to the delay map
	delayMap[nodeID] = nodeDelay;

	return true;
}

double BaseDatapath::TCScheduler::getCriticalPath() {
	double criticalPath = -1;

	for(auto &it : delayMap) {
		if(it.second > criticalPath)
			criticalPath = it.second;
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

#ifdef CUSTOM_OPS
		if(isCustomOp(op)) {
			out << "[" << colorString << " label=\"{" << nodeID << " | " << reverseOpcodeMap.at(op) << "}\"]";
		}
		else
#endif
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
		else if(isDDRReadOp(getNonSilentOpcode(op))) {
			out << "[shape=polygon sides=5 peripheries=2 " << colorString << " label=\"{" << nodeID << " | " << reverseOpcodeMap.at(op) << "}\"]";
		}
		else if(isDDRWriteOp(getNonSilentOpcode(op))) {
			out << "[shape=polygon sides=4 peripheries=2 " << colorString << " label=\"{" << nodeID << " | " << reverseOpcodeMap.at(op) << "}\"]";
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
