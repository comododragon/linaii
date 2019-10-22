#include "profile_h/MemoryModel.h"

#include "profile_h/BaseDatapath.h"

extern memoryTraceMapTy memoryTraceMap;

MemoryModel::MemoryModel(BaseDatapath *datapath) :
	datapath(datapath), microops(datapath->getMicroops()), graph(datapath->getDDDG()),
	nameToVertex(datapath->getNameToVertex()), vertexToName(datapath->getVertexToName()), edgeToWeight(datapath->getEdgeToWeight()),
	baseAddress(datapath->getBaseAddress()), CM(datapath->getConfigurationManager()), PC(datapath->getParsedTraceContainer()) { }

MemoryModel *MemoryModel::createInstance(BaseDatapath *datapath) {
	switch(args.target) {
		case ArgPack::TARGET_XILINX_VC707:
			assert(args.fNoMMA && "Memory model analysis is currently not supported with the selected platform. Please activate the \"--fno-mma\" flag");
			return nullptr;
		case ArgPack::TARGET_XILINX_ZCU102:
		case ArgPack::TARGET_XILINX_ZCU104:
			return new XilinxZCUMemoryModel(datapath);
		case ArgPack::TARGET_XILINX_ZC702:
		default:
			assert(args.fNoMMA && "Memory model analysis is currently not supported with the selected platform. Please activate the \"--fno-mma\" flag");
			return nullptr;
	}
}

void MemoryModel::analyseAndTransform() { }

void XilinxZCUMemoryModel::findInBursts(
		std::unordered_map<unsigned, uint64_t> &foundNodes,
		std::vector<unsigned> &behavedNodes,
		std::unordered_map<unsigned, std::tuple<uint64_t, uint64_t, std::vector<unsigned>>> &burstedNodes,
		std::function<bool(unsigned, unsigned)> comparator
) {
	// Ignore if behavedNodes is empty
	if(0 == behavedNodes.size())
		return;

	// Sort transactions, so that we can find continuities
	std::sort(behavedNodes.begin(), behavedNodes.end(), comparator);

	// Kickstart: logic for the first node
	unsigned currRootNode = behavedNodes[0];
	uint64_t currBaseAddress = foundNodes[currRootNode];
	uint64_t currOffset = 0;
	std::vector<unsigned> currNodes({currRootNode});
	uint64_t lastVisitedBaseAddress = currBaseAddress;

	// Now iterate for all the remaining nodes
	for(unsigned i = 1; i < behavedNodes.size(); i++) {
		unsigned node = behavedNodes[i];

		// If the current address is the same as the last, this is a redundant node that could be optimised
		// We consider part of a burst
		if(lastVisitedBaseAddress == foundNodes[node]) {
			currNodes.push_back(node);
		}
		// Else, check if current address is contiguous to the last one
		// TODO: For now we are assuming that all words are 32-bit
		else if(lastVisitedBaseAddress + 4 == foundNodes[node]) {
			currNodes.push_back(node);
			currOffset++;
		}
		// Discontinuity found. Close current burst and reset variables
		else {
			burstedNodes[currRootNode] = std::make_tuple(currBaseAddress, currOffset, currNodes);

			// Reset variables
			currRootNode = node;
			currBaseAddress = foundNodes[node];
			currOffset = 0;
			currNodes.clear();
			currNodes.push_back(node);
		}

		lastVisitedBaseAddress = foundNodes[node];
	}

	// Close the last burst
	burstedNodes[currRootNode] = std::make_tuple(currBaseAddress, currOffset, currNodes);
}

std::string XilinxZCUMemoryModel::generateInstID(unsigned opcode, std::vector<std::string> instIDList) {
	static uint64_t idCtr = 0;

	// TODO: completamente ineficiente!
	// Create an instID, checking if the name does not exist already
	std::string instID;
	do {
#ifdef LEGACY_SEPARATOR
		instID = reverseOpcodeMap.at(opcode) + "-" + std::to_string(idCtr++);
#else
		instID = reverseOpcodeMap.at(opcode) + GLOBAL_SEPARATOR + std::to_string(idCtr++);
#endif
	} while(std::find(instIDList.begin(), instIDList.end(), instID) != instIDList.end());

	return instID;
}

XilinxZCUMemoryModel::XilinxZCUMemoryModel(BaseDatapath *datapath) : MemoryModel(datapath) { }

void XilinxZCUMemoryModel::analyseAndTransform() {
	const ConfigurationManager::arrayInfoCfgMapTy arrayInfoCfgMap = CM.getArrayInfoCfgMap();
	const std::unordered_map<int, std::pair<int64_t, unsigned>> &memoryTraceList = PC.getMemoryTraceList();

	// Change all load/stores marked as offchip to DDR read/writes (and mark their locations)
	VertexIterator vi, viEnd;
	for(std::tie(vi, viEnd) = vertices(graph); vi != viEnd; vi++) {
		Vertex currNode = *vi;
		unsigned nodeID = vertexToName[currNode];
		int nodeMicroop = microops.at(nodeID);

		if(!isMemoryOp(nodeMicroop))
			continue;

		// Only consider offchip arrays
		if(arrayInfoCfgMap.at(baseAddress[nodeID].first).type != ConfigurationManager::arrayInfoCfgTy::ARRAY_TYPE_OFFCHIP)
			continue;

		if(isLoadOp(nodeMicroop)) {
			microops.at(nodeID) = LLVM_IR_DDRRead;
			loadNodes[nodeID] = memoryTraceList.at(nodeID).first;
		}

		if(isStoreOp(nodeMicroop)) {
			microops.at(nodeID) = LLVM_IR_DDRWrite;
			storeNodes[nodeID] = memoryTraceList.at(nodeID).first;
		}
	}

	// Try to find contiguous loads inside the DDDG.
	std::vector<unsigned> behavedLoads;
	for(auto const &loadNode : loadNodes) {
		// For simplicity, we only consider loads that are either dominated by getelementptr or nothing
		bool behaved = true;
		InEdgeIterator inEdgei, inEdgeEnd;
		for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(nameToVertex[loadNode.first], graph); inEdgei != inEdgeEnd; inEdgei++) {
			unsigned parentID = vertexToName[boost::source(*inEdgei, graph)];
			if(microops.at(parentID) != LLVM_IR_GetElementPtr)
				behaved = false;
		}

		if(behaved)
			behavedLoads.push_back(loadNode.first);
	}
	// Sort the behaved nodes in terms of address
	auto loadComparator = [this](unsigned a, unsigned b) {
		return this->loadNodes[a] < this->loadNodes[b];
	};
	findInBursts(loadNodes, behavedLoads, burstedLoads, loadComparator);

	// Try to find contiguous stores inside the DDDG.
	std::vector<unsigned> behavedStores;
	for(auto const &storeNode : storeNodes) {
		// For simplicity, we only consider stores that are terminal
		if(0 == boost::out_degree(nameToVertex[storeNode.first], graph))
			behavedStores.push_back(storeNode.first);
	}
	// Sort the behaved nodes in terms of address
	auto storeComparator = [this](unsigned a, unsigned b) {
		return this->storeNodes[a] < this->storeNodes[b];
	};
	findInBursts(storeNodes, behavedStores, burstedStores, storeComparator);

	// Try to find contiguous loads between loop iterations
	// TODO

	// Try to find contiguous stores between loop iterations
	// TODO

	// Add the relevant DDDG nodes for offchip load
	for(auto &burst : burstedLoads) {
		unsigned newID = microops.size();

		// Create a node with opcode LLVM_IR_DDRReadReq
		datapath->insertMicroop(LLVM_IR_DDRReadReq);

		// Since we will add new nodes to the DDDG, we must update the auxiliary structures accordingly
		// Get values from the first load, we will use most of them
		std::string currDynamicFunction = PC.getFuncList()[burst.first];
		std::string currInstID = generateInstID(LLVM_IR_DDRReadReq, PC.getInstIDList());
		int lineNo = PC.getLineNoList()[burst.first];
		std::string prevBB = PC.getPrevBBList()[burst.first];
		std::string currBB = PC.getCurrBBList()[burst.first];
		// TODO: checar se está tudo sendo atualizado apropriadamente nas próximas linhas
		PC.unlock();
		PC.openAllFilesForWrite();
		// Update ParsedTraceContainer containers with the new node.
		// XXX: We use the values from the first load
		PC.appendToFuncList(currDynamicFunction);
		PC.appendToInstIDList(currInstID);
		PC.appendToLineNoList(lineNo);
		PC.appendToPrevBBList(prevBB);
		PC.appendToCurrBBList(currBB);
		// Finished
		PC.closeAllFiles();
		PC.lock();

		// Disconnect the edges incoming to the first load and connect to the read request
		std::set<Edge> edgesToRemove;
		std::vector<edgeTy> edgesToAdd;
		InEdgeIterator inEdgei, inEdgeEnd;
		for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(nameToVertex[burst.first], graph); inEdgei != inEdgeEnd; inEdgei++) {
			unsigned sourceID = vertexToName[boost::source(*inEdgei, graph)];
			edgesToRemove.insert(*inEdgei);
			edgesToAdd.push_back({sourceID, newID, edgeToWeight[*inEdgei]});
		}
		// Connect this node to the first load
		// XXX: Does the edge weight matter here?
		edgesToAdd.push_back({newID, burst.first, 0});

		// Now, chain the loads to create the burst effect
		std::vector<unsigned> burstChain = std::get<2>(burst.second);
		for(unsigned i = 1; i < burstChain.size(); i++)
			edgesToAdd.push_back({burstChain[i - 1], burstChain[i], 0});

		// Update DDDG
		datapath->updateRemoveDDDGEdges(edgesToRemove);
		datapath->updateAddDDDGEdges(edgesToAdd);
	}

	// Add the relevant DDDG nodes for offchip store
	for(auto &burst : burstedStores) {
		// DDRWriteReq is positioned before the burst

		unsigned newID = microops.size();

		// Create a node with opcode LLVM_IR_DDRWriteReq
		microops.push_back(LLVM_IR_DDRWriteReq);

		// Since we will add new nodes to the DDDG, we must update the auxiliary structures accordingly
		// Get values from the first store, we will use most of them
		std::string currDynamicFunction = PC.getFuncList()[burst.first];
		std::string currInstID = generateInstID(LLVM_IR_DDRWriteReq, PC.getInstIDList());
		int lineNo = PC.getLineNoList()[burst.first];
		std::string prevBB = PC.getPrevBBList()[burst.first];
		std::string currBB = PC.getCurrBBList()[burst.first];
		// TODO: checar se está tudo sendo atualizado apropriadamente nas próximas linhas
		PC.unlock();
		PC.openAllFilesForWrite();
		// Update ParsedTraceContainer containers with the new node.
		// XXX: We use the values from the first store
		PC.appendToFuncList(currDynamicFunction);
		PC.appendToInstIDList(currInstID);
		PC.appendToLineNoList(lineNo);
		PC.appendToPrevBBList(prevBB);
		PC.appendToCurrBBList(currBB);
		// Finished
		PC.closeAllFiles();
		PC.lock();

		// Connect this node to the first store
		// XXX: Does the edge weight matter here?
		boost::add_edge(newID, burst.first, EdgeProperty(0), graph);

		// Now, chain the stores to create the burst effect
		std::vector<unsigned> burstChain = std::get<2>(burst.second);
		for(unsigned i = 1; i < burstChain.size(); i++)
			boost::add_edge(burstChain[i - 1], burstChain[i], EdgeProperty(0), graph);

		// DDRWriteResp is positioned after the last burst beat
		newID = microops.size();
		unsigned lastStore = std::get<2>(burst.second).back();

		// Create a node with opcode LLVM_IR_DDRWriteResp
		microops.push_back(LLVM_IR_DDRWriteResp);

		// Since we will add new nodes to the DDDG, we must update the auxiliary structures accordingly
		// Get values from the last store, we will use most of them
		currDynamicFunction = PC.getFuncList()[lastStore];
		currInstID = generateInstID(LLVM_IR_DDRWriteResp, PC.getInstIDList());
		lineNo = PC.getLineNoList()[lastStore];
		prevBB = PC.getPrevBBList()[lastStore];
		currBB = PC.getCurrBBList()[lastStore];
		// TODO: checar se está tudo sendo atualizado apropriadamente nas próximas linhas
		PC.unlock();
		PC.openAllFilesForWrite();
		// Update ParsedTraceContainer containers with the new node.
		// XXX: We use the values from the last store
		PC.appendToFuncList(currDynamicFunction);
		PC.appendToInstIDList(currInstID);
		PC.appendToLineNoList(lineNo);
		PC.appendToPrevBBList(prevBB);
		PC.appendToCurrBBList(currBB);
		// Finished
		PC.closeAllFiles();
		PC.lock();

		// TODO: implementar a mesma lógica do burst load para reconectar os edges que tao indo pro DDRWrite
		// agora precisa analisar se o mesmo precisa ser feito em ambos writereq e writeresp ou só um deles... etc, etc.

		// Connect this node to the last store
		// XXX: Does the edge weight matter here?
		boost::add_edge(lastStore, newID, EdgeProperty(0), graph);
	}

	errs() << "-- behavedLoads\n";
	for(auto const &x : behavedLoads)
		errs() << "-- " << std::to_string(x) << ": " << std::to_string(loadNodes[x]) << "\n";

	errs() << "-- behavedStores\n";
	for(auto const &x : behavedStores)
		errs() << "-- " << std::to_string(x) << ": " << std::to_string(storeNodes[x]) << "\n";

	errs() << "-- baseAddress\n";
	for(auto const &x : baseAddress)
		errs() << "-- " << std::to_string(x.first) << ": <" << x.second.first << ", " << std::to_string(x.second.second) << ">\n";

	errs() << "-- memoryTraceList\n";
	for(auto const &x : PC.getMemoryTraceList())
		errs() << "-- " << std::to_string(x.first) << ": <" << std::to_string(x.second.first) << ", " << std::to_string(x.second.second) << ">\n";

	errs() << "-- getElementPtrList\n";
	for(auto const &x : PC.getGetElementPtrList())
		errs() << "-- " << std::to_string(x.first) << ": <" << x.second.first << ", " << std::to_string(x.second.second) << ">\n";

	//errs() << "-- memoryTraceMap\n";
	//for(auto const &x : memoryTraceMap) {
	//	errs() << "-- <" << x.first.first << "," << x.first.second << ">:\n";
	//	for(auto const &y : x.second)
	//		errs() << "---- " << std::to_string(y) << "\n";
	//}

	//std::unordered_map<int, std::pair<int64_t, unsigned>> memoryTraceList;
	//std::unordered_map<int, std::pair<std::string, int64_t>> getElementPtrList;

	// TODO: Ideia inicial
	// 1. Itera sobre o DDDG. Loads e stores à arrays offchip tem os opcodes trocados por LLVM_IR_DDRRead/LLVM_IR_DDRWrite
	// 2. Analisar onde inserir writeReq e writeResp: se serao inseridos dentro do DDDG (aproveitando alguma rajada) ou se serao jogados pra fora do DDDG (fora do loop)
	// 3. Analisar onde inserir readReq, na mesma lógica de 2

	datapath->refreshDDDG();

// TODO TODO TODO TODO
// TODO TODO TODO TODO
// TODO TODO TODO TODO
// TODO TODO TODO TODO
// TODO TODO TODO TODO
//#ifdef DBG_PRINT_ALL
#if 1
	printDatabase();
#endif
}

// TODO TODO TODO TODO
// TODO TODO TODO TODO
// TODO TODO TODO TODO
// TODO TODO TODO TODO
// TODO TODO TODO TODO
//#ifdef DBG_PRINT_ALL
#if 1
void XilinxZCUMemoryModel::printDatabase() {
	errs() << "-- loadNodes\n";
	for(auto const &x : loadNodes)
		errs() << "-- " << std::to_string(x.first) << ": " << std::to_string(x.second) << "\n";
	errs() << "-- ---------\n";

	errs() << "-- burstedLoads\n";
	for(auto const &x : burstedLoads) {
		errs() << "-- " << x.first << ": <" << std::to_string(std::get<0>(x.second)) << ", " << std::to_string(std::get<1>(x.second)) << ",\n";
		for(auto const &y : std::get<2>(x.second))
			errs() << "-- -- " << std::to_string(y) << "\n";
		errs() << "-- >\n";
	}
	errs() << "-- ------------\n";

	errs() << "-- storeNodes\n";
	for(auto const &x : storeNodes)
		errs() << "-- " << std::to_string(x.first) << ": " << std::to_string(x.second) << "\n";
	errs() << "-- ----------\n";

	errs() << "-- burstedStores\n";
	for(auto const &x : burstedStores) {
		errs() << "-- " << x.first << ": <" << std::to_string(std::get<0>(x.second)) << ", " << std::to_string(std::get<1>(x.second)) << ",\n";
		for(auto const &y : std::get<2>(x.second))
			errs() << "-- -- " << std::to_string(y) << "\n";
		errs() << "-- >\n";
	}
	errs() << "-- -------------\n";
}
#endif
