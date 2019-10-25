#include "profile_h/MemoryModel.h"

#include "profile_h/BaseDatapath.h"

extern memoryTraceMapTy memoryTraceMap;
extern bool memoryTraceGenerated;

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

	// Our current assumption for inter-iteration bursting is very restrictive. It should happen only when
	// there is one (and one only) load/store transaction (which may be bursted) inside a loop iteration
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

	return outBurstFound;
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

XilinxZCUMemoryModel::XilinxZCUMemoryModel(BaseDatapath *datapath) : MemoryModel(datapath) {
	loadOutBurstFound = false;
	storeOutBurstFound = false;
	readActive = false;
	writeActive = false;
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

	if(!(args.fNoMMABurst)) {
		std::string wholeLoopName = appendDepthToLoopName(datapath->getTargetLoopName(), datapath->getTargetLoopLevel());
		const std::vector<std::string> &instIDList = PC.getInstIDList();

		// Try to find contiguous loads between loop iterations
		loadOutBurstFound = findOutBursts(burstedLoads, wholeLoopName, instIDList);
		if(loadOutBurstFound)
			errs() << ">>>>>>>>> Congratulations! We found a load out-burst\n";

		// Try to find contiguous stores between loop iterations
		storeOutBurstFound = findOutBursts(burstedStores, wholeLoopName, instIDList);
		if(storeOutBurstFound)
			errs() << ">>>>>>>>> Congratulations! We found a store out-burst\n";
	}

	// Add the relevant DDDG nodes for offchip load
	for(auto &burst : burstedLoads) {
		unsigned newID = microops.size();

		// Add this new node to the mapping map (lol)
		ddrNodesToRootLS[newID] = burst.first;

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

		// Add this new node to the mapping map (lol)
		ddrNodesToRootLS[newID] = burst.first;

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
			edgesToAdd.push_back({sourceID, newID, weight});
		}
		// Connect this node to the first store
		// XXX: Does the edge weight matter here?
		edgesToAdd.push_back({newID, burst.first, 0});

		// Now, chain the stores to create the burst effect
		std::vector<unsigned> burstChain = std::get<2>(burst.second);
		for(unsigned i = 1; i < burstChain.size(); i++)
			edgesToAdd.push_back({burstChain[i - 1], burstChain[i], 0});

		// Update DDDG
		datapath->updateRemoveDDDGEdges(edgesToRemove);
		datapath->updateAddDDDGEdges(edgesToAdd);

		// DDRWriteResp is positioned after the last burst beat
		newID = microops.size();
		unsigned lastStore = std::get<2>(burst.second).back();

		// Add this new node to the mapping map (lol)
		ddrNodesToRootLS[newID] = burst.first;

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

		// Disconnect the edges outcoming from the last store and connect to the write response
		edgesToRemove.clear();
		edgesToAdd.clear();
		OutEdgeIterator outEdgei, outEdgeEnd;
		for(std::tie(outEdgei, outEdgeEnd) = boost::out_edges(nameToVertex[lastStore], graph); outEdgei != outEdgeEnd; outEdgei++) {
			unsigned destID = vertexToName[boost::target(*outEdgei, graph)];

			edgesToRemove.insert(*outEdgei);
			edgesToAdd.push_back({newID, destID, edgeToWeight[*outEdgei]});
		}
		// Connect this node to the last store
		// XXX: Does the edge weight matter here?
		edgesToAdd.push_back({lastStore, newID, 0});

		// Update DDDG
		datapath->updateRemoveDDDGEdges(edgesToRemove);
		datapath->updateAddDDDGEdges(edgesToAdd);
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

	//errs() << "-- memoryTraceList\n";
	//for(auto const &x : PC.getMemoryTraceList())
	//	errs() << "-- " << std::to_string(x.first) << ": <" << std::to_string(x.second.first) << ", " << std::to_string(x.second.second) << ">\n";

	//errs() << "-- getElementPtrList\n";
	//for(auto const &x : PC.getGetElementPtrList())
	//	errs() << "-- " << std::to_string(x.first) << ": <" << x.second.first << ", " << std::to_string(x.second.second) << ">\n";

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

bool XilinxZCUMemoryModel::tryAllocate(unsigned node, int opcode, bool commit) {
	// The current memory policy here implemented consider read/write transactions as regions:
	// - Read and write transactions can coexist if their regions do not overlap;
	// - A read request can be issued when a read transaction is working only if the current read transaction is unbursted;
	// - Write requests are exclusive.

	if(LLVM_IR_DDRReadReq == opcode) {
		unsigned nodeToRootLS = ddrNodesToRootLS.at(node);

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

		// - If there is an active write, it must be for a different region.
		bool activeWriteDoNotOverlap = true;
		if(writeActive) {
			uint64_t writeBase = std::get<0>(burstedStores.at(activeWrite));
			// XXX: Once again, considering 32-bit
			uint64_t writeEnd = writeBase + std::get<1>(burstedStores.at(activeWrite)) * 4;
			uint64_t readBase = std::get<0>(burstedLoads.at(nodeToRootLS));
			// XXX: Once again, considering 32-bit
			uint64_t readEnd = readBase + std::get<1>(burstedLoads.at(nodeToRootLS)) * 4;

			if(writeEnd >= readBase && readEnd >= writeBase)
				activeWriteDoNotOverlap = false;
		}

		bool finalCond = (!readActive && !writeActive) || (allActiveReadsAreUnbursted && activeWriteDoNotOverlap);

		if(finalCond) {
			if(commit) {
				activeReads.insert(nodeToRootLS);
				readActive = true;
			}
		}

		return finalCond;
	}
	else if(LLVM_IR_DDRRead == opcode) {
		// Read transactions can always be issued, because their issuing is conditional to ReadReq
		return true;
	}
	else if(LLVM_IR_DDRWriteReq == opcode) {
		unsigned nodeToRootLS = ddrNodesToRootLS.at(node);

		// Write requests are issued when:

		// - No other active write
		// XXX: writeActive already handles this

		bool finalCond = !writeActive;

		if(finalCond) {
			if(commit) {
				activeWrite = nodeToRootLS;
				writeActive = true;
			}
		}

		return finalCond;
	}
	else if(LLVM_IR_DDRWrite == opcode) {
		// Write transactions can always be issued, because their issuing is conditional to WriteReq
		return true;
	}
	else if(LLVM_IR_DDRWriteResp == opcode) {
		// Write responses (when the data is actually sent to DDR) are issued when:

		// - No active read
		// XXX: readActive already handles this

		// - If there are active reads, they must be for different regions
		bool activeReadsDoNotOverlap = true;
		if(readActive) {
			for(auto &it : activeReads) {
				uint64_t writeBase = std::get<0>(burstedStores.at(activeWrite));
				// XXX: Once again, considering 32-bit
				uint64_t writeEnd = writeBase + std::get<1>(burstedStores.at(activeWrite)) * 4;
				uint64_t readBase = std::get<0>(burstedLoads.at(it));
				// XXX: Once again, considering 32-bit
				uint64_t readEnd = readBase + std::get<1>(burstedLoads.at(it)) * 4;

				if(writeEnd >= readBase && readEnd >= writeBase)
					activeReadsDoNotOverlap = false;
			}
		}

		bool finalCond = !readActive || activeReadsDoNotOverlap;

		return finalCond;
	}
	else {
		return true;
	}
}

void XilinxZCUMemoryModel::release(unsigned node, int opcode) {
	if(LLVM_IR_DDRRead == opcode) {
		// If this is the last load, we must close this burst
		for(auto &it : burstedLoads) {
			if(std::get<2>(it.second).back() == node) {
				activeReads.erase(it.first);
			}
		}

		if(!(activeReads.size()))
			readActive = false;
	}
	else if(LLVM_IR_DDRWriteResp == opcode) {
		// Close the write transaction
		writeActive = false;
	}
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
