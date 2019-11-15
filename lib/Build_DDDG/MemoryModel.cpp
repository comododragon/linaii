#include "profile_h/MemoryModel.h"

#include "profile_h/BaseDatapath.h"

extern memoryTraceMapTy memoryTraceMap;
extern bool memoryTraceGenerated;

bool anyDDRTransactionFound;

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

bool XilinxZCUMemoryModel::findOutBursts(
	std::unordered_map<unsigned, std::tuple<uint64_t, uint64_t, std::vector<unsigned>>> &burstedNodes,
	std::string &wholeLoopName,
	const std::vector<std::string> &instIDList
) {
	bool outBurstFound = false;

	// If DDR scheduling policy is conservative, we only proceed if no DDR transaction was found yet.
	// With other policies, we proceed anyway
	if(!(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched && anyDDRTransactionFound)) {
		// Our current assumption for inter-iteration bursting is very restrictive. It should happen only when
		// there is one (and one only) load/store transaction (which may be bursted) inside a loop iteration,
		// excluding imported out-bursted nodes from inner loops
		if(1 == burstedNodes.size()) {
			// We are optimistic at first
			outBurstFound = true;

			// In this case, all transactions between iterations must be contiguous with no overlap
			// This means that all entangled nodes (i.e. DDDG nodes that represent the same instruction but
			// different instances) must continuously increase their address by the burst size (offset)
			// If any fails, we assume that inter-iteration bursting is not possible.
			std::tuple<uint64_t, uint64_t, std::vector<unsigned>> burstedNode = burstedNodes.begin()->second;
			uint64_t offset = std::get<1>(burstedNode) + 1;
			std::vector<unsigned> burstedNodesVec = std::get<2>(burstedNode);

			for(auto &it : burstedNodesVec) {
				std::pair<std::string, std::string> wholeLoopNameInstNamePair = std::make_pair(wholeLoopName, instIDList[it]);

				// Check if all instances of this instruction are well behaved in the memory trace
				// XXX: We do not sort the list here like when searching for inner bursts, since we do not support loop reordering (for now)
				std::vector<uint64_t> addresses = memoryTraceMap.at(wholeLoopNameInstNamePair);
				uint64_t nextAddress = addresses[0] + 4 * offset;
				for(unsigned i = 1; i < addresses.size(); i++) {
					// TODO: For now we are assuming that all words are 32-bit
					if(nextAddress != addresses[i]) {
						outBurstFound = false;
						break;
					}
					else {
						nextAddress += 4 * offset;
					}
				}

				if(!outBurstFound)
					break;
			}
		}
	}

	return outBurstFound;
}

XilinxZCUMemoryModel::XilinxZCUMemoryModel(BaseDatapath *datapath) : MemoryModel(datapath) {
	loadOutBurstFound = false;
	storeOutBurstFound = false;
	readActive = false;
	writeActive = false;
	//completedTransactions = 0;
	readReqImported = false;
	writeReqImported = false;
	writeRespImported = false;
	//allUnimportedComplete = false;
	//importedWriteRespComplete = true;
}

void XilinxZCUMemoryModel::analyseAndTransform() {
	if(!(args.fNoMMABurst)) {
		// The memory trace map can be generated in two ways:
		// - Running Lina with "--mem-trace" and any other mode than "--mode=estimation"
		// - After running Lina once with the aforementioned configuration, the file "mem_trace.txt" will be available and can be used

		// If memory trace map was not constructed yet, try to generate it from "mem_trace.txt"
		if(!memoryTraceGenerated) {
			std::string line;
			std::string traceFileName = args.workDir + FILE_MEM_TRACE;
			std::ifstream traceFile;

			traceFile.open(traceFileName);
			assert(traceFile.is_open() && "No memory trace found. Please run Lina with \"--mem-trace\" flag (leave it enabled) and any mode other than \"--mode=estimation\" (only once is needed) to generate it or deactivate inter-iteration burst analysis with \"--fno-mmaburst\"");

			while(!traceFile.eof()) {
				std::getline(traceFile, line);

				char buffer[BUFF_STR_SZ];
				char buffer2[BUFF_STR_SZ];
				uint64_t address;
				sscanf(line.c_str(), "%[^,],%*d,%[^,],%*[^,],%*d,%lu,%*d", buffer, buffer2, &address);
				std::pair<std::string, std::string> wholeLoopNameInstNamePair = std::make_pair(std::string(buffer), std::string(buffer2));
				memoryTraceMap[wholeLoopNameInstNamePair].push_back(address);
			}

			traceFile.close();
			memoryTraceGenerated = true;
		}
	}

	const ConfigurationManager::arrayInfoCfgMapTy arrayInfoCfgMap = CM.getArrayInfoCfgMap();
	//const std::unordered_map<int, std::pair<int64_t, unsigned>> &memoryTraceList = PC.getMemoryTraceList();

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
			//loadNodes[nodeID] = memoryTraceList.at(nodeID).first;
			loadNodes[nodeID] = baseAddress.at(nodeID).second;
		}

		if(isStoreOp(nodeMicroop)) {
			microops.at(nodeID) = LLVM_IR_DDRWrite;
			//storeNodes[nodeID] = memoryTraceList.at(nodeID).first;
			storeNodes[nodeID] = baseAddress.at(nodeID).second;
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

	// TODO: why && burstedStores.size()????
	if(!(args.fNoMMABurst) && burstedStores.size()) {
		std::string wholeLoopName = appendDepthToLoopName(datapath->getTargetLoopName(), datapath->getTargetLoopLevel());
		const std::vector<std::string> &instIDList = PC.getInstIDList();

		// Try to find contiguous loads between loop iterations
		loadOutBurstFound = findOutBursts(burstedLoads, wholeLoopName, instIDList);

		// Try to find contiguous stores between loop iterations
		storeOutBurstFound = findOutBursts(burstedStores, wholeLoopName, instIDList);

		// TODO: Perhaps if loadOutBurstFound and storeOutBurstFound are true, we should check if
		// the spaces do not overlap before outbursting both
	}

	// After out-burst analysis, we update the DDR transaction flag to block further out-burst attempts (when scheduling policy is conservative)
	// TODO: We are being conservative here. IF ANY operation is found, we already block any out-bursting, without checking if it is read or write
	//       perhaps the best way would be to test for overlapness, but this was left as a TODO, as you can see.
	if(burstedLoads.size() || burstedStores.size())
		anyDDRTransactionFound = true;

	// Add the relevant DDDG nodes for offchip load
	for(auto &burst : burstedLoads) {
		// Create a node with opcode LLVM_IR_DDRReadReq
		artificialNodeTy newNode = datapath->createArtificialNode(burst.first, loadOutBurstFound? LLVM_IR_DDRSilentReadReq : LLVM_IR_DDRReadReq);

		// Add this new node to the mapping map (lol)
		ddrNodesToRootLS[newNode.ID] = burst.first;

		// If this is an out-bursted load, we must export the LLVM_IR_DDRReadReq node to be allocated to the DDDG before the current
		if(loadOutBurstFound)
			nodesToBeforeDDDG.push_back(std::make_tuple(newNode, std::get<0>(burst.second), std::get<1>(burst.second)));

		// Disconnect the edges incoming to the first load and connect to the read request
		std::set<Edge> edgesToRemove;
		std::vector<edgeTy> edgesToAdd;
		InEdgeIterator inEdgei, inEdgeEnd;
		for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(nameToVertex[burst.first], graph); inEdgei != inEdgeEnd; inEdgei++) {
			unsigned sourceID = vertexToName[boost::source(*inEdgei, graph)];
			edgesToRemove.insert(*inEdgei);

			// If an out-burst was detected, we isolate these nodes instead of connecting them, since they will be "executed" outside this DDDG
			// TODO: Perhaps we should isolate only relevant nodes (e.g. getelementptr and indexadd/sub)
			if(!loadOutBurstFound)
				edgesToAdd.push_back({sourceID, newNode.ID, edgeToWeight[*inEdgei]});
		}
		// Connect this node to the first load
		// XXX: Does the edge weight matter here?
		edgesToAdd.push_back({newNode.ID, burst.first, 0});

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
		artificialNodeTy newNode = datapath->createArtificialNode(burst.first, storeOutBurstFound? LLVM_IR_DDRSilentWriteReq : LLVM_IR_DDRWriteReq);

		// Add this new node to the mapping map (lol)
		ddrNodesToRootLS[newNode.ID] = burst.first;

		// If this is an out-bursted store, we must export the LLVM_IR_DDRWriteReq node to be allocated to the DDDG before the current
		if(storeOutBurstFound)
			nodesToBeforeDDDG.push_back(std::make_tuple(newNode, std::get<0>(burst.second), std::get<1>(burst.second)));

		// Two type of edges can come to a store: data and address
		// For data, we keep it at the appropriate writes
		// For address, we disconnect the incoming to the first store and connect to the write request
		std::set<Edge> edgesToRemove;
		std::vector<edgeTy> edgesToAdd;
		InEdgeIterator inEdgei, inEdgeEnd;
		for(std::tie(inEdgei, inEdgeEnd) = boost::in_edges(nameToVertex[burst.first], graph); inEdgei != inEdgeEnd; inEdgei++) {
			unsigned sourceID = vertexToName[boost::source(*inEdgei, graph)];
			uint8_t weight = edgeToWeight[*inEdgei];

			// Store can receive two parameters: 1 is data and 2 is address
			// We only want to move the address edges (weight 2)
			if(weight != 2)
				continue;

			edgesToRemove.insert(*inEdgei);

			// If an out-burst was detected, we isolate these nodes instead of connecting them, since they will be "executed" outside this DDDG
			if(!storeOutBurstFound)
				edgesToAdd.push_back({sourceID, newNode.ID, weight});
		}
		// Connect this node to the first store
		// XXX: Does the edge weight matter here?
		edgesToAdd.push_back({newNode.ID, burst.first, 0});

		// Now, chain the stores to create the burst effect
		std::vector<unsigned> burstChain = std::get<2>(burst.second);
		for(unsigned i = 1; i < burstChain.size(); i++)
			edgesToAdd.push_back({burstChain[i - 1], burstChain[i], 0});

		// Update DDDG
		datapath->updateRemoveDDDGEdges(edgesToRemove);
		datapath->updateAddDDDGEdges(edgesToAdd);

		// DDRWriteResp is positioned after the last burst beat
		unsigned lastStore = std::get<2>(burst.second).back();
		newNode = datapath->createArtificialNode(lastStore, storeOutBurstFound? LLVM_IR_DDRSilentWriteResp : LLVM_IR_DDRWriteResp);

		// Add this new node to the mapping map (lol)
		ddrNodesToRootLS[newNode.ID] = burst.first;

		// If this is an out-bursted store, we must export the LLVM_IR_DDRWriteResp node to be allocated to the DDDG after the current
		if(storeOutBurstFound)
			nodesToAfterDDDG.push_back(std::make_tuple(newNode, std::get<0>(burst.second), std::get<1>(burst.second)));

		// Disconnect the edges outcoming from the last store and connect to the write response
		edgesToRemove.clear();
		edgesToAdd.clear();
		OutEdgeIterator outEdgei, outEdgeEnd;
		for(std::tie(outEdgei, outEdgeEnd) = boost::out_edges(nameToVertex[lastStore], graph); outEdgei != outEdgeEnd; outEdgei++) {
			unsigned destID = vertexToName[boost::target(*outEdgei, graph)];

			edgesToRemove.insert(*outEdgei);
			edgesToAdd.push_back({newNode.ID, destID, edgeToWeight[*outEdgei]});
		}
		// Connect this node to the last store
		// XXX: Does the edge weight matter here?
		edgesToAdd.push_back({lastStore, newNode.ID, 0});

		// Update DDDG
		datapath->updateRemoveDDDGEdges(edgesToRemove);
		datapath->updateAddDDDGEdges(edgesToAdd);
	}

	// Temporary map used for the recently-imported nodes. The main structures are only updated after the imported nodes
	// are successfully imported
	std::unordered_map<unsigned, unsigned> ddrNodesToRootLSTmp;

	// Add the relevant DDDG nodes for imported offchip load
	for(auto &burst : importedLoads) {
		// Create a node with opcode LLVM_IR_DDRReadReq
		artificialNodeTy newNode = datapath->createArtificialNode(burst.first, LLVM_IR_DDRReadReq);

		// Add this new node to the mapping map (lol)
		ddrNodesToRootLSTmp[newNode.ID] = burst.first;

		// Connect this node to the first load
		// XXX: Does the edge weight matter here?
		std::vector<edgeTy> edgesToAdd;
		edgesToAdd.push_back({newNode.ID, burst.first, 0});

		// With conservative DDR scheduling policy, this imported transaction must execute after all DDR transactions from this DDDG
		if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
			// Connect imported LLVM_IR_DDRReadReq to all last DDR transactions (LLVM_IR_DDRRead's and LLVM_IR_DDRWriteResp)

			// Find for LLVM_IR_DDRWriteResp
			for(auto &it : ddrNodesToRootLS) {
				int opcode = microops.at(it.first);
				// XXX: Does the edge weight matter here?
				if(LLVM_IR_DDRWriteResp == opcode)
					edgesToAdd.push_back({it.first, newNode.ID, 0});
			}

			// Also, connect to the last node of DDR read transactions
			for(auto &it : burstedLoads) {
				unsigned lastLoad = std::get<2>(it.second).back();
				edgesToAdd.push_back({lastLoad, newNode.ID, 0});
			}
		}

		// Update DDDG
		datapath->updateAddDDDGEdges(edgesToAdd);
	}

	// Add the relevant DDDG nodes for imported offchip store
	for(auto &burst : importedStores) {
		// DDRWriteReq is positioned before the burst
		artificialNodeTy newNode = datapath->createArtificialNode(burst.first, writeRespImported? LLVM_IR_DDRSilentWriteReq : LLVM_IR_DDRWriteReq);

		// Add this new node to the mapping map (lol)
		ddrNodesToRootLSTmp[newNode.ID] = burst.first;

		// Connect this node to the first store
		// XXX: Does the edge weight matter here?
		std::vector<edgeTy> edgesToAdd;
		edgesToAdd.push_back({newNode.ID, burst.first, 0});

		// With conservative DDR scheduling policy, this imported transaction must execute after (for LLVM_IR_DDRWriteReq)
		// all DDR transactions from this DDDG
		if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
			// Connect imported LLVM_IR_DDRWriteReq to all last DDR transactions (LLVM_IR_DDRRead's and LLVM_IR_DDRWriteResp)
			if(writeReqImported) {
				// Find for LLVM_IR_DDRWriteResp
				for(auto &it : ddrNodesToRootLS) {
					int opcode = microops.at(it.first);
					// XXX: Does the edge weight matter here?
					if(LLVM_IR_DDRWriteResp == opcode)
						edgesToAdd.push_back({it.first, newNode.ID, 0});
				}

				// Also, connect to the last node of DDR read transactions
				for(auto &it : burstedLoads) {
					unsigned lastLoad = std::get<2>(it.second).back();
					edgesToAdd.push_back({lastLoad, newNode.ID, 0});
				}
			}
		}

		// Update DDDG
		datapath->updateAddDDDGEdges(edgesToAdd);

		// DDRWriteResp is positioned after the last burst beat
		unsigned lastStore = std::get<2>(burst.second).back();
		newNode = datapath->createArtificialNode(lastStore, writeReqImported? LLVM_IR_DDRSilentWriteResp : LLVM_IR_DDRWriteResp);

		// Add this new node to the mapping map (lol)
		ddrNodesToRootLSTmp[newNode.ID] = burst.first;

		// Connect this node to the last store
		// XXX: Does the edge weight matter here?
		edgesToAdd.clear();
		edgesToAdd.push_back({lastStore, newNode.ID, 0});

		// With conservative DDR scheduling policy, this imported transaction must execute before (for LLVM_IR_DDRWriteResp)
		// all DDR transactions from this DDDG
		if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
			// Connect imported LLVM_IR_DDRWriteResp to all root DDR transactions (LLVM_IR_DDRReadReq's and LLVM_IR_DDRWriteReq's)
			if(writeRespImported) {
				// Find for LLVM_IR_DDRReadReq and LLVM_IR_DDRWriteReq
				for(auto &it : ddrNodesToRootLS) {
					int opcode = microops.at(it.first);
					// XXX: Does the edge weight matter here?
					if(LLVM_IR_DDRReadReq == opcode || LLVM_IR_DDRWriteReq == opcode)
						edgesToAdd.push_back({newNode.ID, it.first, 0});
				}
			}
		}

		// Update DDDG
		datapath->updateAddDDDGEdges(edgesToAdd);
	}

	// Add the imported loads to the bursted loads structure
	for(auto &burst : importedLoads) {
		burstedLoads.insert(burst);
	}
	// Add the imported stores to the bursted stores structure
	for(auto &burst : importedStores) {
		burstedStores.insert(burst);
	}
	// And also commit the changes to ddrNodesToRootLS
	ddrNodesToRootLS.insert(ddrNodesToRootLSTmp.begin(), ddrNodesToRootLSTmp.end());

	datapath->refreshDDDG();

	// Create a dummy last node and connect the DDDG leaves and the imported nodes to it,
	// so that the imported nodes (if any) won't stay disconnected
	// TODO: maybe this should be performed somewhere else, in a different timing?
	if(writeRespImported || readReqImported || writeReqImported)
		datapath->createDummySink();

	errs() << "-- behavedLoads\n";
	for(auto const &x : behavedLoads)
		errs() << "-- " << std::to_string(x) << ": " << std::to_string(loadNodes[x]) << "\n";

	errs() << "-- behavedStores\n";
	for(auto const &x : behavedStores)
		errs() << "-- " << std::to_string(x) << ": " << std::to_string(storeNodes[x]) << "\n";

	errs() << "-- baseAddress\n";
	for(auto const &x : baseAddress)
		errs() << "-- " << std::to_string(x.first) << ": <" << x.second.first << ", " << std::to_string(x.second.second) << ">\n";

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

bool XilinxZCUMemoryModel::tryAllocate(unsigned node, int opcode, bool commit) {
	// Two different memory policies are implemented here:
	// Conservative:
	// - Read and write transactions can coexist if their regions do not overlap;
	// - A read request can be issued when a read transaction is working only if the current read transaction is unbursted;
	// - Write requests are exclusive;
	// Overlapping:
	// - Any transaction can coexist if their regions do not overlap;

	if(LLVM_IR_DDRReadReq == opcode || LLVM_IR_DDRSilentReadReq == opcode) {
		unsigned nodeToRootLS = ddrNodesToRootLS.at(node);
		uint64_t readBase = std::get<0>(burstedLoads.at(nodeToRootLS));
		// XXX: Once again, considering 32-bit
		uint64_t readEnd = readBase + std::get<1>(burstedLoads.at(nodeToRootLS)) * 4;

		bool finalCond;
		if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
			// Read requests are issued when:

			// - No active transactions;
			// XXX: readActive and writeActive already handle this

			// - If there are active reads, they must be unbursted;
			bool allActiveReadsAreUnbursted = true;
			if(readActive) {
				for(auto &it : activeReads) {
					if(std::get<1>(burstedLoads.at(it))) {
						allActiveReadsAreUnbursted = false;
						break;
					}
				}
			}

			// - If there are active writes, they must be for different regions
			bool activeWritesDoNotOverlap = true;
			if(writeActive) {
				for(auto &it : activeWrites) {
					uint64_t otherBase = std::get<0>(burstedStores.at(it));
					// XXX: Once again, considering 32-bit
					uint64_t otherEnd = otherBase + std::get<1>(burstedStores.at(it)) * 4;

					if(otherEnd >= readBase && readEnd >= otherBase)
						activeWritesDoNotOverlap = false;
				}
			}

			finalCond = (!readActive && !writeActive) || (allActiveReadsAreUnbursted && activeWritesDoNotOverlap);
		}
		else if(ArgPack::DDR_POLICY_CAN_OVERLAP == args.ddrSched) {
			// Read requests are issued when:

			// - No active transactions;
			// XXX: readActive and writeActive already handle this

			// - If there are active reads, they must be for different regions
			bool activeReadsDoNotOverlap = true;
			if(readActive) {
				for(auto &it : activeReads) {
					uint64_t otherBase = std::get<0>(burstedLoads.at(it));
					// XXX: Once again, considering 32-bit
					uint64_t otherEnd = otherBase + std::get<1>(burstedLoads.at(it)) * 4;

					if(otherEnd >= readBase && readEnd >= otherBase)
						activeReadsDoNotOverlap = false;
				}
			}

			// - If there are active writes, they must be for different regions
			bool activeWritesDoNotOverlap = true;
			if(writeActive) {
				for(auto &it : activeWrites) {
					uint64_t otherBase = std::get<0>(burstedStores.at(it));
					// XXX: Once again, considering 32-bit
					uint64_t otherEnd = otherBase + std::get<1>(burstedStores.at(it)) * 4;

					if(otherEnd >= readBase && readEnd >= otherBase)
						activeWritesDoNotOverlap = false;
				}
			}

			finalCond = (!readActive && !writeActive) || (activeReadsDoNotOverlap && activeWritesDoNotOverlap);
		}
		else {
			assert(false && "Invalid DDR scheduling policy selected (this assert should never execute)");
		}

		if(finalCond) {
			if(commit) {
				activeReads.insert(nodeToRootLS);
				readActive = true;
			}
		}

		return finalCond;
	}
	else if(LLVM_IR_DDRRead == opcode || LLVM_IR_DDRSilentRead == opcode) {
		// Read transactions can always be issued, because their issuing is conditional to ReadReq

		// Since LLVM_IR_DDRRead takes only one cycle, its release logic is located here (MemoryModel::release() is not executed)
		if(commit) {
			// If this is the last load, we must close this burst
			for(auto &it : burstedLoads) {
				if(std::get<2>(it.second).back() == node)
					activeReads.erase(it.first);
			}

			if(!(activeReads.size()))
				readActive = false;
		}

		return true;
	}
	else if(LLVM_IR_DDRWriteReq == opcode || LLVM_IR_DDRSilentWriteReq == opcode) {
		unsigned nodeToRootLS = ddrNodesToRootLS.at(node);
		uint64_t writeBase = std::get<0>(burstedStores.at(nodeToRootLS));
		// XXX: Once again, considering 32-bit
		uint64_t writeEnd = writeBase + std::get<1>(burstedStores.at(nodeToRootLS)) * 4;

		bool finalCond;
		if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
			// Write requests are issued when:

			// - No other active write.
			finalCond = !writeActive;
		}
		else if(ArgPack::DDR_POLICY_CAN_OVERLAP == args.ddrSched) {
			// Write requests are issued when:

			// - No active transactions;
			// XXX: readActive and writeActive already handle this

			// - If there are active reads, they must be for different regions
			bool activeReadsDoNotOverlap = true;
			if(readActive) {
				for(auto &it : activeReads) {
					uint64_t otherBase = std::get<0>(burstedLoads.at(it));
					// XXX: Once again, considering 32-bit
					uint64_t otherEnd = otherBase + std::get<1>(burstedLoads.at(it)) * 4;

					if(otherEnd >= writeBase && writeEnd >= otherBase)
						activeReadsDoNotOverlap = false;
				}
			}

			// - If there are active writes, they must be for different regions
			bool activeWritesDoNotOverlap = true;
			if(writeActive) {
				for(auto &it : activeWrites) {
					uint64_t otherBase = std::get<0>(burstedStores.at(it));
					// XXX: Once again, considering 32-bit
					uint64_t otherEnd = otherBase + std::get<1>(burstedStores.at(it)) * 4;

					if(otherEnd >= writeBase && writeEnd >= otherBase)
						activeWritesDoNotOverlap = false;
				}
			}

			finalCond = (!readActive && !writeActive) || (activeReadsDoNotOverlap && activeWritesDoNotOverlap);
		}

		if(finalCond) {
			if(commit) {
				activeWrites.insert(nodeToRootLS);
				writeActive = true;
			}
		}

		return finalCond;
	}
	else if(LLVM_IR_DDRWrite == opcode || LLVM_IR_DDRSilentWrite == opcode) {
		// Write transactions can always be issued, because their issuing is conditional to WriteReq
		return true;
	}
	else if(LLVM_IR_DDRWriteResp == opcode || LLVM_IR_DDRSilentWriteResp == opcode) {
		unsigned nodeToRootLS = ddrNodesToRootLS.at(node);
		uint64_t writeBase = std::get<0>(burstedStores.at(nodeToRootLS));
		// XXX: Once again, considering 32-bit
		uint64_t writeEnd = writeBase + std::get<1>(burstedStores.at(nodeToRootLS)) * 4;

		bool finalCond;
		if(ArgPack::DDR_POLICY_CANNOT_OVERLAP == args.ddrSched) {
			// Write responses (when the data is actually sent to DDR) are issued when:

			// - No active read
			// XXX: readActive already handles this

			// - If there are active reads, they must be for different regions
			bool activeReadsDoNotOverlap = true;
			if(readActive) {
				for(auto &it : activeReads) {
					uint64_t otherBase = std::get<0>(burstedLoads.at(it));
					// XXX: Once again, considering 32-bit
					uint64_t otherEnd = otherBase + std::get<1>(burstedLoads.at(it)) * 4;

					if(otherEnd >= writeBase && writeEnd >= otherBase)
						activeReadsDoNotOverlap = false;
				}
			}

			finalCond = !readActive || activeReadsDoNotOverlap;
		}
		else if(ArgPack::DDR_POLICY_CAN_OVERLAP == args.ddrSched) {
			// Write responses can always be issued, because their issuing is conditional to WriteReq
			// XXX: Should the overlap check logic from LLVM_IR_DDRWriteReq be here as well, or only here?
			finalCond = true;
		}

		return finalCond;
	}
	else {
		return true;
	}
}

void XilinxZCUMemoryModel::release(unsigned node, int opcode) {
	if(LLVM_IR_DDRWriteResp == opcode || LLVM_IR_DDRSilentWriteResp == opcode) {
		unsigned nodeToRootLS = ddrNodesToRootLS.at(node);
		activeWrites.erase(nodeToRootLS);

		if(!(activeWrites.size()))
			writeActive = false;
	}
}

bool XilinxZCUMemoryModel::outBurstsOverlap() {
	// Check if the scheduled outbursts overlap
	// XXX: Since for now only one write and one read outbursts are allowed, this check is somewhat simple
	std::tuple<uint64_t, uint64_t, std::vector<unsigned>> &outBurstedWrite = burstedStores.begin()->second;
	std::tuple<uint64_t, uint64_t, std::vector<unsigned>> &outBurstedRead = burstedLoads.begin()->second;
	uint64_t writeBase = std::get<0>(outBurstedWrite);
	// XXX: Once again, considering 32-bit
	uint64_t writeEnd = writeBase + std::get<1>(outBurstedWrite) * 4;
	uint64_t readBase = std::get<0>(outBurstedRead);
	// XXX: Once again, considering 32-bit
	uint64_t readEnd = readBase + std::get<1>(outBurstedRead) * 4;

	return writeEnd >= readBase && readEnd >= writeBase;
}

void XilinxZCUMemoryModel::importNode(nodeExportTy &exportedNode) {
	artificialNodeTy node = std::get<0>(exportedNode);
	uint64_t baseAddress = std::get<1>(exportedNode);
	uint64_t offset = std::get<2>(exportedNode);

	if(LLVM_IR_DDRSilentReadReq == node.opcode) {
		readReqImported = true;

		artificialNodeTy newNode = datapath->createArtificialNode(node, LLVM_IR_DDRSilentRead);

		std::vector<unsigned> nodes;
		nodes.push_back(newNode.ID);
		importedLoads[newNode.ID] = std::make_tuple(baseAddress, offset, nodes);

		//importedReadReq = newNode.ID;
	}
	else if(LLVM_IR_DDRSilentWriteReq == node.opcode) {
		writeReqImported = true;

		artificialNodeTy newNode = datapath->createArtificialNode(node, LLVM_IR_DDRSilentWrite);

		std::vector<unsigned> nodes;
		nodes.push_back(newNode.ID);
		importedStores[newNode.ID] = std::make_tuple(baseAddress, offset, nodes);

		//importedWriteReq = newNode.ID;
	}
	else if(LLVM_IR_DDRSilentWriteResp == node.opcode) {
		writeRespImported = true;

		artificialNodeTy newNode = datapath->createArtificialNode(node, LLVM_IR_DDRSilentWrite);

		std::vector<unsigned> nodes;
		nodes.push_back(newNode.ID);
		importedStores[newNode.ID] = std::make_tuple(baseAddress, offset, nodes);

		//importedWriteResp = newNode.ID;
	}
	else {
		assert(false && "Invalid type of node imported to the memory model");
	}
}

std::vector<nodeExportTy> &XilinxZCUMemoryModel::getNodesToBeforeDDDG() {
	return nodesToBeforeDDDG;
}

std::vector<nodeExportTy> &XilinxZCUMemoryModel::getNodesToAfterDDDG() {
	return nodesToAfterDDDG;
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
