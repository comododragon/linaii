#include "profile_h/DDDGBuilder.h"

#include "profile_h/BaseDatapath.h"

ParsedTraceContainer::ParsedTraceContainer(BaseDatapath *datapath, std::string kernelName) : datapath(datapath) {
	funcFileName = args.outWorkDir + kernelName + "_dynamicfuncid.gz";
	instIDFileName = args.outWorkDir + kernelName + "_instid.gz";
	lineNoFileName = args.outWorkDir + kernelName + "_linenum.gz";
	memoryTraceFileName = args.outWorkDir + kernelName + "_memaddr.gz";
	getElementPtrFileName = args.outWorkDir + kernelName + "_getelementptr.gz";
	prevBasicBlockFileName = args.outWorkDir + kernelName + "_prevbasicblock.gz";
	currBasicBlockFileName = args.outWorkDir + kernelName + "_currbasicblock.gz";

	funcFile = Z_NULL;
	instIDFile = Z_NULL;
	lineNoFile = Z_NULL;
	memoryTraceFile = Z_NULL;
	getElementPtrFile = Z_NULL;
	prevBasicBlockFile = Z_NULL;
	currBasicBlockFile = Z_NULL;

	numOfInstructions = -1;
	lastParameter = true;
	prevBB = "-1";
	numOfRegDeps = 0;
	numOfMemDeps = 0;
}

ParsedTraceContainer::~ParsedTraceContainer() {
	if(funcFile)
		gzclose(funcFile);
	if(instIDFile)
		gzclose(instIDFile);
	if(lineNoFile)
		gzclose(lineNoFile);
	if(memoryTraceFile)
		gzclose(memoryTraceFile);
	if(getElementPtrFile)
		gzclose(getElementPtrFile);
	if(prevBasicBlockFile)
		gzclose(prevBasicBlockFile);
	if(currBasicBlockFile)
		gzclose(currBasicBlockFile);
}

void ParsedTraceContainer::parseTraceFile(gzFile &traceFile, lineFromToTy fromToPair) {
	if(args.compressed) {
		funcFile = gzopen(funcFileName.c_str(), "w");
		assert(funcFile != Z_NULL && "Could not open dynamic funcID file for write");
		instIDFile = gzopen(instIDFileName.c_str(), "w");
		assert(instIDFile != Z_NULL && "Could not open instID file for write");
		lineNoFile = gzopen(lineNoFileName.c_str(), "w");
		assert(lineNoFile != Z_NULL && "Could not open line num file for write");
		memoryTraceFile = gzopen(memoryTraceFileName.c_str(), "w");
		assert(memoryTraceFile != Z_NULL && "Could not open memory trace file for write");
		getElementPtrFile = gzopen(getElementPtrFileName.c_str(), "w");
		assert(getElementPtrFile != Z_NULL && "Could not open getelementptr file for write");
		prevBasicBlockFile = gzopen(prevBasicBlockFileName.c_str(), "w");
		assert(prevBasicBlockFile != Z_NULL && "Could not open prev BB file for write");
		currBasicBlockFile = gzopen(currBasicBlockFileName.c_str(), "w");
		assert(currBasicBlockFile != Z_NULL && "Could not open curr BB file for write");
	}

	funcList.clear();
	instIDList.clear();
	lineNoList.clear();
	memoryTraceList.clear();
	getElementPtrList.clear();
	prevBBList.clear();
	currBBList.clear();

	uint64_t from = fromToPair.first, to = fromToPair.second;
	uint64_t instCount = 0;
	bool parseInst = false;
	char buffer[BUFF_STR_SZ];

	// Iterate through dynamic trace, but only process the specified interval
	gzrewind(traceFile);
	while(!gzeof(traceFile)) {
		if(Z_NULL == gzgets(traceFile, buffer, sizeof(buffer)))
			continue;

		std::string line(buffer);
		size_t tagPos = line.find(",");

		if(std::string::npos == tagPos)
			continue;

		std::string tag = line.substr(0, tagPos);
		rest = line.substr(tagPos + 1);

		if(!tag.compare("0")) {
			// If log0 is within specified interval, process
			if(instCount >= from && instCount <= to) {
				parseInstructionLine();
				parseInst = true;
			}
			else {
				parseInst = false;
			}
			instCount++;
		}
		else if(parseInst) {
			if(!tag.compare("r"))
				parseResult();
			else if(!tag.compare("f"))
				parseForward();
			else
				parseParameter(std::atoi(tag.c_str()));
		}
		// No need to keep looking after interval, just stop
		else if(instCount > to) {
			break;
		}
	}

	if(args.compressed) {
		gzclose(funcFile);
		gzclose(instIDFile);
		gzclose(lineNoFile);
		gzclose(memoryTraceFile);
		gzclose(getElementPtrFile);
		gzclose(prevBasicBlockFile);
		gzclose(currBasicBlockFile);
		funcFile = Z_NULL;
		instIDFile = Z_NULL;
		lineNoFile = Z_NULL;
		memoryTraceFile = Z_NULL;
		getElementPtrFile = Z_NULL;
		prevBasicBlockFile = Z_NULL;
		currBasicBlockFile = Z_NULL;
	}
}

void ParsedTraceContainer::parseInstructionLine() {
	int lineNo;
	char buffer[BUFF_STR_SZ];
	char buffer2[BUFF_STR_SZ];
	char buffer3[BUFF_STR_SZ];
	int microop;
	int count;
	sscanf(rest.c_str(), "%d,%[^,],%[^,],%[^,],%d,%d\n", &lineNo, buffer, buffer2, buffer3, &microop, &count);
	std::string currStaticFunction(buffer);
	std::string bbID(buffer2);
	std::string instID(buffer3);

	prevMicroop = currMicroop;
	currMicroop = (uint8_t) microop;
	datapath->insertMicroop(currMicroop);
	currInstID = instID;

	// Not first run
	if(!activeMethod.empty()) {
		std::string prevStaticFunction = activeMethod.top().first;
		int prevCount = activeMethod.top().second;

		// Function name in stack differs from current name, i.e. we are in a different function now
		if(currStaticFunction.compare(prevStaticFunction)) {
			s2uMap::iterator found = functionCounter.find(currStaticFunction);
			// Add information from this function and reset counter to 0
			if(functionCounter.end() == found) {
				functionCounter.insert(std::make_pair(currStaticFunction, 0));
				currDynamicFunction = currStaticFunction + "-0";
				activeMethod.push(std::make_pair(currStaticFunction, 0));
			}
			// Update (increment) counter for this function
			else {
				found->second++;
				currDynamicFunction = currStaticFunction + "-" + std::to_string(found->second);
				activeMethod.push(std::make_pair(currStaticFunction, found->second));
			}

			/*
			if(LLVM_IR_Call == prevMicroop) {
				assert(calleeFunction == currStaticFunction && "Current static function differs from called instruction");

				// TODO: push to methodCallGraph, but apparently this variable is never used
				// Implement logic if necessary
			}
			*/
		}
		// Function name in stack equals to current name, either nothing changed or this is a recursive call
		else {
			// Last opcode was a call to this same function, increment counter
			if(LLVM_IR_Call == prevMicroop && calleeFunction == currStaticFunction) {
				s2uMap::iterator found = functionCounter.find(currStaticFunction);
				assert(found != functionCounter.end() && "Current static function not found in function counter");

				found->second++;
				currDynamicFunction = currStaticFunction + "-" + std::to_string(found->second);
				activeMethod.push(std::make_pair(currStaticFunction, found->second));
			}
			// Nothing changed, just change the current dynamic function
			else {
				currDynamicFunction = prevStaticFunction + "-" + std::to_string(prevCount);
			}
		}

		// This is a return, pop the active function
		if(LLVM_IR_Ret == microop)
			activeMethod.pop();
	}
	// First run, add information about this function to stack
	else {
		s2uMap::iterator found = functionCounter.find(currStaticFunction);
		// Add information from this function and reset counter to 0
		if(functionCounter.end() == found) {
			functionCounter.insert(std::make_pair(currStaticFunction, 0));
			currDynamicFunction = currStaticFunction + "-0";
			activeMethod.push(std::make_pair(currStaticFunction, 0));
			functionCounter.insert(std::make_pair(currStaticFunction, 0));
		}
		// Update (increment) counter for this function
		else {
			found->second++;
			currDynamicFunction = currStaticFunction + "-" + std::to_string(found->second);
			activeMethod.push(std::make_pair(currStaticFunction, found->second));
		}
	}

	// If this is a PHI instruction and last instruction was a branch, update BB pointers
	if(isPhiOp(microop) && LLVM_IR_Br == prevMicroop)
		prevBB = currBB;
	currBB = bbID;

	// Store collected info to compressed files or memory lists
	if(args.compressed) {
		gzprintf(funcFile, "%s\n", currDynamicFunction.c_str());
		gzprintf(instIDFile, "%s\n", currInstID.c_str());
		gzprintf(lineNoFile, "%d\n", lineNo);
		gzprintf(prevBasicBlockFile, "%s\n", prevBB.c_str());
		gzprintf(currBasicBlockFile, "%s\n", currBB.c_str());
	}
	else {
		funcList.push_back(currDynamicFunction);
		instIDList.push_back(currInstID);
		lineNoList.push_back(lineNo);
		prevBBList.push_back(prevBB);
		currBBList.push_back(currBB);
	}

	// Reset variables for the following lines
	numOfInstructions++;
	lastParameter = true;
	parameterValuePerInst.clear();
	parameterSizePerInst.clear();
	parameterLabelPerInst.clear();
}

void ParsedTraceContainer::parseResult() {
	int size;
	double value;
	int isReg;
	char buffer[BUFF_STR_SZ];
	sscanf(rest.c_str(), "%d,%lf,%d,%[^\n]\n", &size, &value, &isReg, buffer);
	std::string label(buffer);

	assert(isReg && "Result trace line must be a register");

	std::string uniqueRegID = currDynamicFunction + "-" + label;

	// Store the instruction where this register was written
	s2uMap::iterator found = registerLastWritten.find(uniqueRegID);
	if(found != registerLastWritten.end())
		found->second = numOfInstructions;
	else
		registerLastWritten.insert(std::make_pair(uniqueRegID, numOfInstructions));

	// Register an allocation request
	if(LLVM_IR_Alloca == currMicroop) {
		if(args.compressed)
			gzprintf(getElementPtrFile, "%d,%s,%lld\n", numOfInstructions, label, (int64_t) value);
		else
			getElementPtrList.push_back(std::make_tuple(numOfInstructions, label, value));
	}
	// Register a load
	else if(isLoadOp(currMicroop)) {
		int64_t addr = parameterValuePerInst.back();

		if(args.compressed)
			gzprintf(memoryTraceFile, "%d,%lld,%u\n", numOfInstructions, addr, size);
		else
			memoryTraceList.push_back(std::make_tuple(numOfInstructions, addr, size));
	}
	// Register a DMA request
	else if(isDMAOp(currMicroop)) {
		int64_t addr = parameterValuePerInst[1];
		unsigned memSize = parameterValuePerInst[2];

		if(args.compressed)
			gzprintf(memoryTraceFile, "%d,%lld,%u\n", numOfInstructions, addr, memSize);
		else
			memoryTraceList.push_back(std::make_tuple(numOfInstructions, addr, memSize));
	}
}

void ParsedTraceContainer::parseForward() {
	int size, isReg;
	double value;
	char buffer[BUFF_STR_SZ];
	sscanf(rest.c_str(), "%d,%lf,%d,%[^\n]\n", &size, &value, &isReg, buffer);
	std::string label(buffer);

	assert(isReg && "Forward trace line must be a register");
	assert(isCallOp(currMicroop) && "Invalid forward line found in trace with no attached DMA/call instruction");

	std::string uniqueRegID = calleeDynamicFunction + "-" + label;

	int tmpWrittenInst = (lastCallSource != -1)? lastCallSource : numOfInstructions;

	s2uMap::iterator found = registerLastWritten.find(uniqueRegID);
	if(found != registerLastWritten.end())
		found->second = tmpWrittenInst;
	else
		registerLastWritten.insert(std::make_pair(uniqueRegID, tmpWrittenInst));
}

void ParsedTraceContainer::parseParameter(int param) {
	int size, isReg;
	double value;
	char buffer[BUFF_STR_SZ];
	sscanf(rest.c_str(), "%d,%lf,%d,%[^\n]\n", &size, &value, &isReg, buffer);
	std::string label(buffer);

	// First line after log0 is the last parameter (parameters are traced backwards!)
	if(lastParameter) {
		//numOfParameters = param;

		// This is a call, save the called function
		if(LLVM_IR_Call == currMicroop)
			calleeFunction = label;

		// Update dynamic function
		s2uMap::iterator found = functionCounter.find(calleeFunction);
		if(found != functionCounter.end())
			calleeDynamicFunction = calleeFunction + "-" + std::to_string(found->second + 1);
		else
			calleeDynamicFunction = calleeFunction + "-0";
	}

	// Note that the last parameter is listed first in the trace, hence this non-intuitive logic
	lastParameter = false;
	lastCallSource = -1;

	if(isReg) {
		// Must check if PHI processing is necessary to find where this operand comes from
		bool processPhi = true;
		if(isPhiOp(currMicroop)) {
			std::string operandBB = instName2bbNameMap.at(label);
			if(operandBB != prevBB)
				processPhi = false;
		}

		if(processPhi) {
			std::string uniqueRegID = currDynamicFunction + "-" + label;

			// Update, register a new register dependency, storing the instruction that writes the register
			s2uMap::iterator found = registerLastWritten.find(uniqueRegID);
			if(found != registerLastWritten.end()) {
				edgeNodeInfo tmp;
				tmp.sink = numOfInstructions;
				tmp.paramID = param;

				registerEdgeTable.insert(std::make_pair(found->second, tmp));
				numOfRegDeps++;

				if(LLVM_IR_Call == currMicroop)
					lastCallSource = found->second;
			}
		}
	}

	// Handle load/store/memory parameter
	if(isMemoryOp(currMicroop) || LLVM_IR_GetElementPtr == currMicroop || isDMAOp(currMicroop)) {
		parameterValuePerInst.push_back((int64_t) value);
		parameterSizePerInst.push_back(size);
		parameterLabelPerInst.push_back(label);

		// First parameter
		if(1 == param && isLoadOp(currMicroop)) {
			int64_t addr = parameterValuePerInst.back();
			i642uMap::iterator found = addressLastWritten.find(addr);

			if(found != addressLastWritten.end()) {
				unsigned source = found->second;
				auto sameSource = memoryEdgeTable.equal_range(source);
				bool exists = false;

				for(auto sink = sameSource.first; sink != sameSource.second; sink++) {
					if(numOfInstructions == sink->second.sink) {
						exists = true;
						break;
					}
				}

				if(!exists) {
					edgeNodeInfo tmp;
					tmp.sink = numOfInstructions;
					tmp.paramID = -1;
					memoryEdgeTable.insert(std::make_pair(source, tmp));
					numOfMemDeps++;
				}
			}

			//int64_t baseAddr = parameterValuePerInst.back();
			std::string baseLabel = parameterLabelPerInst.back();

			if(args.compressed)
				gzprintf(getElementPtrFile, "%d,%s,%lld\n", numOfInstructions, baseLabel.c_str(), addr);
			else
				getElementPtrList.push_back(std::make_tuple(numOfInstructions, baseLabel, addr));
		}
		// Second parameter of store is the pointer
		else if(2 == param && isStoreOp(currMicroop)) {
			int64_t addr = parameterValuePerInst[0];
			std::string baseLabel = parameterLabelPerInst[0];

			i642uMap::iterator found = addressLastWritten.find(addr);
			if(found != addressLastWritten.end())
				found->second = numOfInstructions;
			else
				addressLastWritten.insert(std::make_pair(addr, numOfInstructions));

			if(args.compressed)
				gzprintf(getElementPtrFile, "%d,%s,%lld\n", numOfInstructions, baseLabel.c_str(), addr);
			else
				getElementPtrList.push_back(std::make_tuple(numOfInstructions, baseLabel, addr));
		}
		// First parameter of store is the value
		else if(1 == param && isStoreOp(currMicroop)) {
			int64_t addr = parameterValuePerInst[0];
			unsigned size = parameterSizePerInst.back();

			if(args.compressed)
				gzprintf(memoryTraceFile, "%d,%lld,%u\n", numOfInstructions, addr, size);
			else
				memoryTraceList.push_back(std::make_tuple(numOfInstructions, addr, size));
		}
		else if(1 == param && LLVM_IR_GetElementPtr == currMicroop) {
			int64_t addr = parameterValuePerInst.back();
			std::string label = parameterLabelPerInst.back();

			if(args.compressed)
				gzprintf(getElementPtrFile, "%d,%s,%lld\n", numOfInstructions, label.c_str(), addr);
			else
				getElementPtrList.push_back(std::make_tuple(numOfInstructions, label.c_str(), addr));
		}
	}
}

DDDGBuilder::DDDGBuilder(BaseDatapath *datapath, ParsedTraceContainer &PC) : datapath(datapath), PC(PC) {
}

bool DDDGBuilder::buildInitialDDDG() {
	std::string traceFileName = args.workDir + FILE_DYNAMIC_TRACE;
	gzFile traceFile;

	traceFile = gzopen(traceFileName.c_str(), "r");
	assert(traceFile != Z_NULL && "Could not open trace input file");

	lineFromToTy fromToPair = getTraceLineFromTo(traceFile);
	//std::cout << "--------- " << fromToPair.first << " " << fromToPair.second << "\n";

	PC.parseTraceFile(traceFile, fromToPair);

	return true;
}

lineFromToTy DDDGBuilder::getTraceLineFromTo(gzFile &traceFile) {
	std::string loopName = datapath->getTargetLoopName();
	unsigned loopLevel = datapath->getTargetLoopLevel();
	uint64_t unrollFactor = datapath->getTargetLoopUnrollFactor();
	std::string functionName = std::get<0>(parseLoopName(loopName));
	lpNameLevelStrPairTy lpNameLevelPair = std::make_pair(loopName, std::to_string(loopLevel));

	// Get name of header BB for this loop
	lpNameLevelPair2headBBnameMapTy::iterator found = lpNameLevelPair2headBBnameMap.find(lpNameLevelPair);
	assert(found != lpNameLevelPair2headBBnameMap.end() && "Could not find header BB of loop inside lpNameLevelPair2headBBnameMap");
	std::string headerBBName = found->second;

	// Get name of exiting BB for this loop
	lpNameLevelPair2headBBnameMapTy::iterator found2 = lpNameLevelPair2exitingBBnameMap.find(lpNameLevelPair);
	assert(found2 != lpNameLevelPair2exitingBBnameMap.end() && "Could not find exiting BB of loop inside lpNameLevelPair2exitingBBnameMap");
	std::string exitingBBName = found2->second;

	// Get ID of last instruction inside header BB
	std::pair<std::string, std::string> headerBBFuncNamePair = std::make_pair(headerBBName, functionName);
	headerBBFuncNamePair2lastInstMapTy::iterator found3 = headerBBFuncNamePair2lastInstMap.find(headerBBFuncNamePair);
	assert(found3 != headerBBFuncNamePair2lastInstMap.end() && "Could not find last inst of header BB of loop inside headerBBFuncNamePair2lastInstMap");
	std::string lastInstHeaderBB = found3->second;

	// Get ID of last instruction inside exiting BB
	std::pair<std::string, std::string> exitingBBFuncNamePair = std::make_pair(exitingBBName, functionName);
	headerBBFuncNamePair2lastInstMapTy::iterator found4 = exitingBBFuncNamePair2lastInstMap.find(exitingBBFuncNamePair);
	assert(found4 != exitingBBFuncNamePair2lastInstMap.end() && "Could not find last inst of exiting BB of loop inside headerBBFuncNamePair2lastInstMap");
	std::string lastInstExitingBB = found4->second;

	// Get number of instruction inside header BB
	std::pair<std::string, std::string> funcHeaderBBNamePair = std::make_pair(functionName, headerBBName);
	funcBBNmPair2numInstInBBMapTy::iterator found5 = funcBBNmPair2numInstInBBMap.find(funcHeaderBBNamePair);
	assert(found5 != funcBBNmPair2numInstInBBMap.end() && "Could not find number of instructions in header BB inside funcBBNmPair2numInstInBBMap");
	unsigned numInstInHeaderBB = found5->second;

	// Create database of headerBBName-lastInst -> loopName-level
	headerBBlastInst2loopNameLevelPairMapTy headerBBlastInst2loopNameLevelPairMap;
	for(auto &it : lpNameLevelPair2headBBnameMap) {
		std::string loopName = it.first.first;
		unsigned loopLevel = std::stoul(it.first.second);
		std::string funcName = std::get<0>(parseLoopName(loopName));
		std::string headerBBName = it.second;
		std::pair<std::string, std::string> headerBBFuncNamePair = std::make_pair(headerBBName, funcName);
		std::string headerBBLastInst = headerBBFuncNamePair2lastInstMap[headerBBFuncNamePair];
		std::pair<std::string, unsigned> loopNameLevelPair = std::make_pair(loopName, loopLevel);
		headerBBlastInst2loopNameLevelPairMap.insert(std::make_pair(headerBBLastInst, loopNameLevelPair));
	}

	std::string wholeLoopName = appendDepthToLoopName(loopName, loopLevel);
	uint64_t loopBound = wholeloopName2loopBoundMap.at(wholeLoopName);
	bool skipRuntimeLoopBound = (loopBound > 0);

	uint64_t from = 0, to = 0;
	bool firstTraverseHeader = true;
	uint64_t lastInstExitingCounter = 0;
	char buffer[BUFF_STR_SZ];

	// Iterate through dynamic trace
	gzrewind(traceFile);
	while(!gzeof(traceFile)) {
		if(Z_NULL == gzgets(traceFile, buffer, sizeof(buffer)))
			continue;

		std::string line(buffer);
		size_t tagPos = line.find(",");

		if(std::string::npos == tagPos)
			continue;

		std::string tag = line.substr(0, tagPos);
		std::string rest = line.substr(tagPos + 1);

		if(!tag.compare("0")) {
			char buffer2[BUFF_STR_SZ];
			int count;
			sscanf(rest.c_str(), "%*d,%*[^,],%*[^,],%[^,],%*d,%d\n", buffer2, &count);
			std::string instName(buffer2);

			// Mark the first line of the first iteration of this loop
			if(firstTraverseHeader && !instName.compare(lastInstHeaderBB)) {
				from = count - numInstInHeaderBB + 1;
				firstTraverseHeader = false;
			}

			// Mark the last line of the last iteration of this loop
			if(!instName.compare(lastInstExitingBB)) {
				lastInstExitingCounter++;
				if(unrollFactor == lastInstExitingCounter) {
					to = count;

					// If we don't need to calculate runtime loop bound, we can stop now
					if(skipRuntimeLoopBound)
						break;
				}
			}

			// Calculating loop bound at runtime: Increment loop bound counter 
			if(!skipRuntimeLoopBound) {
				headerBBlastInst2loopNameLevelPairMapTy::iterator found6 = headerBBlastInst2loopNameLevelPairMap.find(instName);
				if(found6 != headerBBlastInst2loopNameLevelPairMap.end()) {
					std::string wholeLoopName = appendDepthToLoopName(found6->second.first, found6->second.second);
					wholeloopName2loopBoundMap[wholeLoopName]++;
				}
			}
		}
	}

	// Post-process runtime loop bound calculations
	if(!skipRuntimeLoopBound) {
		for(auto &it : loopName2levelUnrollVecMap) {
			std::string loopName = it.first;
			unsigned levelSize = it.second.size();

			assert(levelSize >= 1 && "This loop level is less than 1");

			// Create temporary vector with values inside wholeloopName2loopBoundMap
			std::vector<unsigned> loopBounds(levelSize, 0);
			for(unsigned i = 0; i < levelSize; i++) {
				std::string wholeLoopName = appendDepthToLoopName(loopName, i + 1);
				wholeloopName2loopBoundMapTy::iterator found7 = wholeloopName2loopBoundMap.find(wholeLoopName);
				assert(found7 != wholeloopName2loopBoundMap.end() && "Could not find loop in wholeloopName2loopBoundMap");

				loopBounds[i] = found7->second;
			}

			// The trace only keep trace of the innermost loop that an instruction was executed.
			// This means that the runtime-calculated loop bounds are not reflecting actual nesting structure of the loops
			// We must correct/adjust the runtime-calculated loop bounds to reflect the actual nesting structure of the loops
			for(unsigned i = 1; i < levelSize; i++) {
				std::string wholeLoopName = appendDepthToLoopName(loopName, i + 1);
				wholeloopName2loopBoundMapTy::iterator found8 = wholeloopName2loopBoundMap.find(wholeLoopName);
				assert(found8 != wholeloopName2loopBoundMap.end() && "Could not find loop in wholeloopName2loopBoundMap");

				loopBounds[i] = loopBounds[i] / loopBounds[i - 1];
				wholeloopName2loopBoundMap[wholeLoopName] = loopBounds[i];
			}

			/*
			for(unsigned i = 0; i < levelSize; i++) {
				std::string wholeLoopName = appendDepthToLoopName(loopName, i + 1);
				wholeloopName2loopBoundMapTy::iterator found9 = wholeloopName2loopBoundMap.find(wholeLoopName);

				std::cout << ">>>>>>>>>>>>> " << found9->first << ": " << found9->second << "\n";
			}
			*/
		}
	}

	return std::make_pair(from, to);
}

#if 0
gzFile dynamic_func_file;
gzFile instid_file;
gzFile line_num_file;
gzFile memory_trace;
gzFile getElementPtr_trace;
gzFile prevBasicBlock_trace;
gzFile curBasicBlock_trace;

DDDG::DDDG(BaseDatapath *_datapath, std::string _trace_name, std::string input_path)
					: datapath(_datapath), trace_name(_trace_name), inputPath(input_path)
{
	std::cout << "DEBUG-INFO: [trace-analysis_trace-generation] Building Dynamic Data Dependence Graph\n";
  num_of_reg_dep = 0;
  num_of_mem_dep = 0;
  num_of_instructions = -1;
  last_parameter = 0;
  prev_bblock = "-1";
}

int DDDG::num_edges()
{
  return register_edge_table.size() + memory_edge_table.size();
}

int DDDG::num_nodes()
{
  return num_of_instructions + 1;
}

int DDDG::num_of_register_dependency()
{
  return num_of_reg_dep;
}

int DDDG::num_of_memory_dependency()
{
  return num_of_mem_dep;
}

void DDDG::output_method_call_graph(std::string bench)
{
  std::string output_file_name(bench);
  output_file_name += "_method_call_graph";
  write_string_file(output_file_name, method_call_graph.size(),
    method_call_graph);
}

void DDDG::output_dddg()
{

  for(auto it = register_edge_table.begin();
    it != register_edge_table.end(); ++it)
  {
    datapath->addDddgEdge(it->first, it->second.sink_node, it->second.par_id);
  }

  //Memory Dependency
  for(auto it = memory_edge_table.begin();
    it != memory_edge_table.end(); ++it)
  {
    datapath->addDddgEdge(it->first, it->second.sink_node, it->second.par_id);
  }
}

void DDDG::parse_instruction_line(std::string line)
{
  char curr_static_function[256];
  char instid[256], bblockid[256];
  int line_num;
  int microop;
  int count;
  sscanf(line.c_str(), "%d,%[^,],%[^,],%[^,],%d,%d\n", &line_num, curr_static_function, bblockid, instid, &microop, &count);

  prev_microop = curr_microop;
  curr_microop = (uint8_t)microop;

  datapath->insertMicroop((curr_microop));

  curr_instid = instid;

  if (!active_method.empty())
  {
     char prev_static_function[256];
     unsigned prev_counts;
     sscanf(active_method.top().c_str(), "%[^-]-%u", prev_static_function, &prev_counts);
     if (strcmp(curr_static_function, prev_static_function) != 0)
     {
       auto func_it = function_counter.find(curr_static_function);
       if (func_it == function_counter.end())
       {
         function_counter.insert(make_pair(curr_static_function, 0));
         ostringstream oss;
         oss << curr_static_function << "-0" ;
         curr_dynamic_function = oss.str();
       }
       else
       {
         func_it->second++;
         ostringstream oss;
         oss << curr_static_function << "-" << func_it->second;
         curr_dynamic_function = oss.str();
       }
       active_method.push(curr_dynamic_function);

        // if prev inst is a IRCALL instruction
       if (prev_microop == LLVM_IR_Call)
       {
         assert(callee_function == curr_static_function);

         ostringstream oss;
         oss << num_of_instructions << "," << prev_static_function << "-" << prev_counts  << "," << active_method.top();
         method_call_graph.push_back(oss.str());
       }
     }
     else
     //the same as last method
     {
       //calling it self
       if (prev_microop == LLVM_IR_Call && callee_function == curr_static_function)
       {
         //a new instantiation
         auto func_it = function_counter.find(curr_static_function);
         assert(func_it != function_counter.end());
         func_it->second++;
         ostringstream oss;
         oss << curr_static_function << "-" << func_it->second;
         curr_dynamic_function = oss.str();
         active_method.push(curr_dynamic_function);
       }
       else
         curr_dynamic_function = active_method.top();
     }
     if (microop == LLVM_IR_Ret)
       active_method.pop();
  }
  else
  {
    auto func_it = function_counter.find(curr_static_function);
    if (func_it != function_counter.end())
    {
      func_it->second++;
      ostringstream oss;
      oss << curr_static_function << "-" << func_it->second;
      curr_dynamic_function = oss.str();
    }
    else
    {
      function_counter.insert(make_pair(curr_static_function, 0));
      ostringstream oss;
      oss << curr_static_function << "-0" ;
      curr_dynamic_function = oss.str();
    }
    active_method.push(curr_dynamic_function);
  }
	/*
	//Bug here: If a basic block contains more than one PHI instructions at the beginning of a BB, 
	//          then prev_bblock will be changed to curr_bblock, which is wrong 
  if (microop == LLVM_IR_PHI)
    prev_bblock = curr_bblock;
  curr_bblock = bblockid;
	*/

	if ( (microop == LLVM_IR_PHI) && (prev_microop == LLVM_IR_Br) ) {
		prev_bblock = curr_bblock;
	}
	curr_bblock = bblockid;

  gzprintf(prevBasicBlock_trace, "%s\n", prev_bblock.c_str());
	gzprintf(curBasicBlock_trace, "%s\n", curr_bblock.c_str());
  gzprintf(dynamic_func_file, "%s\n", curr_dynamic_function.c_str());
  gzprintf(instid_file, "%s\n", curr_instid.c_str());
  gzprintf(line_num_file, "%d\n", line_num);
  num_of_instructions++;
  last_parameter = 0;
  parameter_value_per_inst.clear();
  parameter_size_per_inst.clear();
  parameter_label_per_inst.clear();
}

void DDDG::parse_parameter(std::string line, int param_tag)
{
  int size, is_reg;
  double value;
  char label[256];
  sscanf(line.c_str(), "%d,%lf,%d,%[^\n]\n", &size, &value, &is_reg, label);
  if (!last_parameter)
  { // The param_tag'th oprand of this instruction
    num_of_parameters = param_tag;
    if (curr_microop == LLVM_IR_Call)
      callee_function = label;
    auto func_it = function_counter.find(callee_function);
    ostringstream oss;
    if (func_it != function_counter.end())
      oss << callee_function << "-" << func_it->second + 1;
    else
      oss << callee_function << "-0" ;
    callee_dynamic_function = oss.str();
  }
  last_parameter = 1;
  last_call_source = -1;
  if (is_reg)
  {
		//Need to check PHI instruction where its operand comes from.
		bool skip_flag = false;
		if (curr_microop == LLVM_IR_PHI) {
			std::string operand_bbName = instName2bbNameMap.at(label);
			if (operand_bbName != prev_bblock) {
				skip_flag = true;
			}
		}

		if (skip_flag == false) {
			char unique_reg_id[256];
			sprintf(unique_reg_id, "%s-%s", curr_dynamic_function.c_str(), label);
			//Find the instruction that writes the register
			auto reg_it = register_last_written.find(unique_reg_id);
			if (reg_it != register_last_written.end())
			{
				edge_node_info tmp_edge;
				tmp_edge.sink_node = num_of_instructions;
				tmp_edge.par_id = param_tag;
				register_edge_table.insert(make_pair(reg_it->second, tmp_edge));
				num_of_reg_dep++;
				if (curr_microop == LLVM_IR_Call)
					last_call_source = reg_it->second;
			}
		}

  }
  if (curr_microop == LLVM_IR_Load || curr_microop == LLVM_IR_Store 
    || curr_microop == LLVM_IR_GetElementPtr || isDMAOp(curr_microop))
  {
    parameter_value_per_inst.push_back((long long int) value);
    parameter_size_per_inst.push_back(size);
    parameter_label_per_inst.push_back(label);

    //last parameter
    if (param_tag == 1 && curr_microop == LLVM_IR_Load)
    {
      long long int mem_address = parameter_value_per_inst.back();
      auto addr_it = address_last_written.find(mem_address);
      if (addr_it != address_last_written.end())
      {
        unsigned source_inst = addr_it->second;
        auto same_source_inst = memory_edge_table.equal_range(source_inst);
        bool edge_existed = 0;
        for (auto sink_it = same_source_inst.first; sink_it != same_source_inst.second; sink_it++)
        {
          if (sink_it->second.sink_node == num_of_instructions)
          {
            edge_existed = 1;
            break;
          }
        }
        if (!edge_existed)
        {
          edge_node_info tmp_edge;
          tmp_edge.sink_node = num_of_instructions;
          //tmp_edge.var_id = "";
          tmp_edge.par_id = -1;
          memory_edge_table.insert(make_pair(source_inst, tmp_edge));
          num_of_mem_dep++;
        }
      }
      long long int base_address = parameter_value_per_inst.back();
      std::string base_label = parameter_label_per_inst.back();
      gzprintf(getElementPtr_trace, "%d,%s,%lld\n", num_of_instructions, base_label.c_str(), base_address);
    }
    else if (param_tag == 2 && curr_microop == LLVM_IR_Store) //2nd of Store is the pointer while 1st is the value
    {
      long long int mem_address = parameter_value_per_inst[0];
      auto addr_it = address_last_written.find(mem_address);
      if (addr_it != address_last_written.end())
        addr_it->second = num_of_instructions;
      else
        address_last_written.insert(make_pair(mem_address, num_of_instructions));

      long long int base_address = parameter_value_per_inst[0];
      std::string base_label = parameter_label_per_inst[0];
      gzprintf(getElementPtr_trace, "%d,%s,%lld\n", num_of_instructions, base_label.c_str(), base_address);
    }
    else if (param_tag == 1 && curr_microop == LLVM_IR_Store)
    {
      long long int mem_address = parameter_value_per_inst[0];
      unsigned mem_size = parameter_size_per_inst.back();
      gzprintf(memory_trace, "%d,%lld,%u\n", num_of_instructions, mem_address, mem_size);
    }
    else if (param_tag == 1 && curr_microop == LLVM_IR_GetElementPtr)
    {
      long long int base_address = parameter_value_per_inst.back();
      std::string base_label = parameter_label_per_inst.back();
      gzprintf(getElementPtr_trace, "%d,%s,%lld\n", num_of_instructions, base_label.c_str(), base_address);
    }
  }
}

void DDDG::parse_result(std::string line)
{
  int size, is_reg;
  double value;
  char label[256];

  sscanf(line.c_str(), "%d,%lf,%d,%[^\n]\n", &size, &value, &is_reg, label);
  assert(is_reg);
  char unique_reg_id[256];
  sprintf(unique_reg_id, "%s-%s", curr_dynamic_function.c_str(), label);
  auto reg_it = register_last_written.find(unique_reg_id);
  if (reg_it != register_last_written.end())
    reg_it->second = num_of_instructions;
  else
    register_last_written[unique_reg_id] = num_of_instructions;

  if (curr_microop == LLVM_IR_Alloca)
    gzprintf(getElementPtr_trace, "%d,%s,%lld\n", num_of_instructions, label, (long long int)value);
  else if (curr_microop == LLVM_IR_Load) {
    long long int mem_address = parameter_value_per_inst.back();
    gzprintf(memory_trace, "%d,%lld,%u\n", num_of_instructions, mem_address, size);
  }
  else if (isDMAOp(curr_microop)) {
    long long int mem_address = parameter_value_per_inst[1];
    unsigned mem_size = parameter_value_per_inst[2];
    gzprintf(memory_trace, "%d,%lld,%u\n", num_of_instructions, mem_address, mem_size);
  }
}

void DDDG::parse_forward(std::string line)
{
  int size, is_reg;
  double value;
  char label[256];

  sscanf(line.c_str(), "%d,%lf,%d,%[^\n]\n", &size, &value, &is_reg, label);
  assert(is_reg);

  char unique_reg_id[256];
  assert(isCallOp(curr_microop) || isDMAOp(curr_microop));
  sprintf(unique_reg_id, "%s-%s", callee_dynamic_function.c_str(), label);

  auto reg_it = register_last_written.find(unique_reg_id);
  int tmp_written_inst = num_of_instructions;
  if (last_call_source != -1)
    tmp_written_inst = last_call_source;
  if (reg_it != register_last_written.end())
    reg_it->second = tmp_written_inst;
  else
    register_last_written[unique_reg_id] = tmp_written_inst;
}

bool DDDG::build_initial_dddg() {
  if (!fileExists(trace_name))
  {
		std::cout << "DEBUG-INFO: [DDDG-generation] ERROR: Input Trace Not Found" << std::endl;
		assert(false && "[DDDG-generation] ERROR: Input Trace Not Found!\n");
  }
	else
  {
    std::cout << "DEBUG-INFO: [DDDG-generation] Generating DDDG" << std::endl;
  }

	/// Get sub-trace interval "from" and "to"
	std::string loop_name = datapath->getTargetLoopName();
	unsigned loop_level = datapath->getTargetLoopLevel();
	unsigned loop_unroll_factor = datapath->getTargetLoopLevelUnrollFactor();
	line_from_to_Ty from_to_pair = getTraceLineFromTo(loop_name, loop_level, loop_unroll_factor);

	/// Generating DDDG
	gzFile tracefile;
  //tracefile = fopen(trace_name.c_str(), "r");
	std::string tracefile_name = trace_name;
	tracefile = gzopen(tracefile_name.c_str(), "r");
	if (tracefile == Z_NULL) {
		std::string err_str = "Error! gzfile " + tracefile_name + " can not open!";
		assert(false && err_str.c_str() );
	}

  std::string bench = datapath->getBenchName();
  std::string func_file_name, instid_file_name;
  std::string memory_trace_name, getElementPtr_trace_name;
  std::string resultVar_trace_name, line_num_file_name;
  std::string prevBasicBlock_trace_name;
	std::string curBasicBlock_trace_name;

	/*
	func_file_name = inputPath + bench + "_dynamic_funcid.gz";
	instid_file_name = inputPath + bench + "_instid.gz";
	line_num_file_name = inputPath + bench + "_linenum.gz";
	memory_trace_name = inputPath + bench + "_memaddr.gz";
	getElementPtr_trace_name = inputPath + bench + "_getElementPtr.gz";
	prevBasicBlock_trace_name = inputPath + bench + "_prevBasicBlock.gz";
	curBasicBlock_trace_name = inputPath + bench + "_curBasicBlock.gz";
	*/
	func_file_name = outputPath + bench + "_dynamic_funcid.gz";
	instid_file_name = outputPath + bench + "_instid.gz";
	line_num_file_name = outputPath + bench + "_linenum.gz";
	memory_trace_name = outputPath + bench + "_memaddr.gz";
	getElementPtr_trace_name = outputPath + bench + "_getElementPtr.gz";
	prevBasicBlock_trace_name = outputPath + bench + "_prevBasicBlock.gz";
	curBasicBlock_trace_name = outputPath + bench + "_curBasicBlock.gz";

  dynamic_func_file  = gzopen(func_file_name.c_str(), "w");
	if (dynamic_func_file == Z_NULL) {
		std::string err_str = "Error! gzfile " + func_file_name + " can not open!";
		assert(false && err_str.c_str());
	}

	instid_file = gzopen(instid_file_name.c_str(), "w");
	if (instid_file == Z_NULL) {
		std::string err_str = "Error! gzfile " + instid_file_name + " can not open!";
		assert(false && err_str.c_str());
	}

	line_num_file = gzopen(line_num_file_name.c_str(), "w");
	if (line_num_file == Z_NULL) {
		std::string err_str = "Error! gzfile " + line_num_file_name + " can not open!";
		assert(false && err_str.c_str());
	}

  memory_trace = gzopen(memory_trace_name.c_str(), "w");
	if (memory_trace == Z_NULL) {
		std::string err_str = "Error! gzfile " + memory_trace_name + " can not open!";
		assert(false && err_str.c_str());
	}

  getElementPtr_trace = gzopen(getElementPtr_trace_name.c_str(), "w");
	if (getElementPtr_trace == Z_NULL) {
		std::string err_str = "Error! gzfile " + getElementPtr_trace_name + " can not open!";
		assert(false && err_str.c_str());
	}

  prevBasicBlock_trace = gzopen(prevBasicBlock_trace_name.c_str(), "w");
	if (prevBasicBlock_trace == Z_NULL) {
		std::string err_str = "Error! gzfile " + prevBasicBlock_trace_name + " can not open!";
		assert(false && err_str.c_str());
	}

	curBasicBlock_trace = gzopen(curBasicBlock_trace_name.c_str(), "w");
	if (prevBasicBlock_trace == Z_NULL) {
		std::string err_str = "Error! gzfile " + curBasicBlock_trace_name + " can not open!";
		assert(false && err_str.c_str());
	}

	//extract_trace_file(tracefile);
	extract_trace_file(tracefile, from_to_pair.first, from_to_pair.second);

  gzclose(dynamic_func_file);
  gzclose(instid_file);
  gzclose(line_num_file);
  gzclose(memory_trace);
  gzclose(getElementPtr_trace);
  gzclose(prevBasicBlock_trace);
	gzclose(curBasicBlock_trace);

  output_dddg();

	std::cout << "DEBUG-INFO: [DDDG-generation] Loop name: " << datapath->getTargetLoopName() << std::endl;
	std::cout << "DEBUG-INFO: [DDDG-generation] Loop level: " << datapath->getTargetLoopLevel() << std::endl;
	std::cout << "DEBUG-INFO: [DDDG-generation] Unrolling factor at this loop level: " << datapath->getTargetLoopLevelUnrollFactor() << std::endl;
	std::cout << "DEBUG-INFO: [DDDG-generation] Num of Nodes: " << datapath->getNumOfNodes() << std::endl;
	std::cout << "DEBUG-INFO: [DDDG-generation] Num of Edges: " << datapath->getNumOfEdges() << std::endl;
	std::cout << "DEBUG-INFO: [DDDG-generation] Num of Reg Edges: " << num_of_register_dependency() << std::endl;
	std::cout << "DEBUG-INFO: [DDDG-generation] Num of MEM Edges: " << num_of_memory_dependency() << std::endl;

	// Write graph properties into a file
	std::string graphProperty_file_name = inputPath + bench + "_graph_property.csv";

	ofstream graphProperty_file(graphProperty_file_name);
	if (graphProperty_file.is_open()) {
		//graphProperty_file << "kernel name,node,edge,register edge,memory edge" << std::endl;

		graphProperty_file << bench << "," << datapath->getNumOfNodes() << "," << datapath->getNumOfEdges() << ",";
		graphProperty_file << num_of_register_dependency() << "," << num_of_memory_dependency() << std::endl;

		graphProperty_file.close();
	}
	else {
		assert(false && "DEBUG-INFO: [DDDG-generation] Error: Could not open graphProperty_file!\n");
	}

	std::cout << "DEBUG-INFO: [DDDG-generation] Finished" << std::endl;
  return 0;
}

line_from_to_Ty DDDG::getTraceLineFromTo(std::string loopName, unsigned loopLevel, unsigned unroll_factor) {
	/// Get the target instruction name inside header basic block of the target loop (last instruction in the
	/// header basic block of this loop)
	std::size_t pos = loopName.find("_loop");
	std::string function_name = loopName.substr(0, pos);
	lpNameLevelStrPairTy lpName_lpLevelPair = std::make_pair(loopName, std::to_string(loopLevel));
	lpNameLevelPair2headBBnameMapTy::iterator it_lpLevel = lpNameLevelPair2headBBnameMap.find(lpName_lpLevelPair);
	lpNameLevelPair2headBBnameMapTy::iterator ie_lpLevel = lpNameLevelPair2headBBnameMap.end();
	assert((it_lpLevel != ie_lpLevel) && "Error: Can not find (loop name, loop level) pair inside lpNameLevelPair2headBBnameMap!\n");
	std::string headerBBname = lpNameLevelPair2headBBnameMap.at(lpName_lpLevelPair);
	lpNameLevelPair2headBBnameMapTy::iterator it_lpLevel_exiting = lpNameLevelPair2exitingBBnameMap.find(lpName_lpLevelPair);
	lpNameLevelPair2headBBnameMapTy::iterator ie_lpLevel_exiting = lpNameLevelPair2exitingBBnameMap.end();
	assert((it_lpLevel_exiting != ie_lpLevel_exiting) && "Error: Can not find (loop name, loop level) pair inside lpNameLevelPair2exitingBBnameMap!\n");
	std::string exitingBBname = lpNameLevelPair2exitingBBnameMap.at(lpName_lpLevelPair);

	std::pair<std::string, std::string> headerBBFuncNamePair = std::make_pair(headerBBname, function_name);
	headerBBFuncNamePair2lastInstMapTy::iterator it_lastInst = headerBBFuncNamePair2lastInstMap.find(headerBBFuncNamePair);
	headerBBFuncNamePair2lastInstMapTy::iterator ie_lastInst = headerBBFuncNamePair2lastInstMap.end();
	assert( (it_lastInst!=ie_lastInst) && "Error: Can not find (headerBB name, function name) pair inside headerBBFuncNamePair2lastInstMap!\n" );
	std::string last_inst_in_headerBB = headerBBFuncNamePair2lastInstMap.at(headerBBFuncNamePair);

	std::pair<std::string, std::string> exitingBBFuncNamePair = std::make_pair(exitingBBname, function_name);
	headerBBFuncNamePair2lastInstMapTy::iterator it_lastInst_exiting = exitingBBFuncNamePair2lastInstMap.find(exitingBBFuncNamePair);
	headerBBFuncNamePair2lastInstMapTy::iterator ie_lastInst_exiting = exitingBBFuncNamePair2lastInstMap.end();
	assert((it_lastInst_exiting != ie_lastInst_exiting) && "Error: Can not find (headerBB name, function name) pair inside exitingBBFuncNamePair2lastInstMap!\n");
	std::string last_inst_in_exitingBB = exitingBBFuncNamePair2lastInstMap.at(exitingBBFuncNamePair);

	std::pair<std::string, std::string> FuncheaderBBNamePair = std::make_pair(function_name, headerBBname);
	funcBBNmPair2numInstInBBMapTy::iterator it_numInst = funcBBNmPair2numInstInBBMap.find(FuncheaderBBNamePair);
	funcBBNmPair2numInstInBBMapTy::iterator ie_numInst = funcBBNmPair2numInstInBBMap.end();
	assert( (it_numInst != ie_numInst) && "Error: Can not find (function name, headerBB name) pair inside funcBBNmPair2numInstInBBMap!\n" );
	unsigned numInstInHeaderBB = funcBBNmPair2numInstInBBMap.at(FuncheaderBBNamePair);

	/*
	loopName2levelUnrollVecMapTy::iterator it_lvUr = loopName2levelUnrollVecMap.begin();
	loopName2levelUnrollVecMapTy::iterator ie_lvUr = loopName2levelUnrollVecMap.end();
	
	for (; it_lvUr != ie_lvUr; ++it_lvUr) {
		std::string loop_name = it_lvUr->first;
		unsigned level_size = it_lvUr->second.size();
		std::vector<uint64_t> tmp(level_size, 0);
		loopName2levelLpBoundVecMap.insert(std::make_pair(loop_name, tmp));
	}*/

	headerBBlastInst2loopNameLevelPairMapTy headerBBlastInst2loopNameLevelPairMap;
	it_lpLevel = lpNameLevelPair2headBBnameMap.begin();
	ie_lpLevel = lpNameLevelPair2headBBnameMap.end();
	for (; it_lpLevel != ie_lpLevel; ++it_lpLevel) {
		std::string loop_name = it_lpLevel->first.first;
		std::string loop_level_str = it_lpLevel->first.second;
		unsigned loop_level = std::stoul(loop_level_str);
		std::size_t position = loop_name.find("_loop");
		std::string func_name = loopName.substr(0, position);
		std::string headerBBname = it_lpLevel->second;
		std::pair<std::string, std::string> headerBBfuncNamePair = std::make_pair(headerBBname, func_name);
		std::string headerBB_last_inst = headerBBFuncNamePair2lastInstMap[headerBBfuncNamePair];
		std::pair<std::string, unsigned> loopNameLevelPair = std::make_pair(loop_name, loop_level);
		headerBBlastInst2loopNameLevelPairMap.insert(std::make_pair(headerBB_last_inst, loopNameLevelPair));
	}


	//FILE *tracefile;
	gzFile tracefile;
	//tracefile = fopen(trace_name.c_str(), "r");
	std::string tracefile_name = trace_name;
	tracefile = gzopen(tracefile_name.c_str(), "r");
	if (tracefile == Z_NULL) {
		std::string err_str = "Error! gzfile " + tracefile_name + " can not open!";
		assert(false && err_str.c_str());
	}

	uint64_t from = 0;
	uint64_t to = 0;
	char buffer[256];

	uint64_t last_inst_header_counter = 0;
	uint64_t last_inst_exiting_counter = 0;

	std::string whole_loop_name = loopName + "_" + std::to_string(loopLevel);
	unsigned lp_bound = wholeloopName2loopBoundMap.at(whole_loop_name);
	unsigned skip_runtime_lp_bound = false;
	if (lp_bound > 0) {
		skip_runtime_lp_bound = true;
	}

	while (!gzeof(tracefile)) {
		if (gzgets(tracefile, buffer, sizeof(buffer)) == Z_NULL)
			continue;
		std::string wholeline(buffer);
		size_t pos_end_tag = wholeline.find(",");

		if (pos_end_tag == std::string::npos) {
			continue;
		}
		std::string tag = wholeline.substr(0, pos_end_tag);
		std::string line_left = wholeline.substr(pos_end_tag + 1);
		if (tag.compare("0") == 0){
			//parse_instruction_line(line_left);
			char curr_static_function[256];
			char instid[256], bblockid[256];
			int line_num;
			int microop;
			int count;
			sscanf(line_left.c_str(), "%d,%[^,],%[^,],%[^,],%d,%d\n", &line_num, curr_static_function, bblockid, instid, &microop, &count);
			std::string func_name(curr_static_function);
			std::string bb_name(bblockid);
			std::string inst_name(instid);
			if (inst_name == last_inst_in_headerBB) {
				last_inst_header_counter++;
				if (last_inst_header_counter == 1) {
					from = count - numInstInHeaderBB + 1;
				}
			}

			if (inst_name == last_inst_in_exitingBB) {
				last_inst_exiting_counter++;
				if (last_inst_exiting_counter == unroll_factor) {
					to = count;
					if (skip_runtime_lp_bound) {
						// No need to calculate loop bound at runtime, just exit the while loop
						break;
					}
				}
			}

			if (!skip_runtime_lp_bound) {
				headerBBlastInst2loopNameLevelPairMapTy::iterator it_headerBBlastInst = headerBBlastInst2loopNameLevelPairMap.find(inst_name);
				if (it_headerBBlastInst != headerBBlastInst2loopNameLevelPairMap.end()) {
					std::string loop_name = it_headerBBlastInst->second.first;
					unsigned loop_level = it_headerBBlastInst->second.second;
					std::string whole_lp_name = loop_name + "_" + std::to_string(loop_level);
					wholeloopName2loopBoundMap[whole_lp_name]++;
					//loopName2levelLpBoundVecMap[loop_name][loop_level - 1]++;
				}
			}
		}
		else {
			// Skip the rest lines
		}
	}

	gzclose(tracefile);

	/// Calculate loop bounds
	//loopName2levelLpBoundVecMapTy::iterator it_lpbound = loopName2levelLpBoundVecMap.begin();
	//loopName2levelLpBoundVecMapTy::iterator ie_lpbound = loopName2levelLpBoundVecMap.end();
	if (!skip_runtime_lp_bound) {
		/*
		wholeloopName2loopBoundMapTy::iterator it_bound = wholeloopName2loopBoundMap.begin();
		wholeloopName2loopBoundMapTy::iterator ie_bound = wholeloopName2loopBoundMap.end();
		for (; it_bound != ie_bound; ++it_bound) {
			std::string whole_lp_name = it_bound->first;
			std::size_t pos = whole_lp_name.find("-");
			std::string loop_name = whole_lp_name.substr(0, pos+1);
			std::string rest_name = whole_lp_name.substr(pos + 1);
			pos = rest_name.find("_");
			loop_name += rest_name.substr(0, pos);
			unsigned lp_level = (unsigned)std::stoi(rest_name.substr(pos + 1)) - 1;
			unsigned lp_bound = it_bound->second;
			loopName2levelLpBoundVecMap[loop_name][lp_level] = lp_bound;
		}*/

		loopName2levelUnrollVecMapTy::iterator it_lvUr = loopName2levelUnrollVecMap.begin();
		loopName2levelUnrollVecMapTy::iterator ie_lvUr = loopName2levelUnrollVecMap.end();
		for (; it_lvUr != ie_lvUr; ++it_lvUr) {
			std::string loop_name = it_lvUr->first;
			unsigned level_size = it_lvUr->second.size();
			assert((level_size >= 1) && "Error: loop level in this loop is less than 1!\n");
			std::vector<unsigned> lp_bounds(level_size, 0);
			for (unsigned i = 0; i < level_size; i++) {
				std::string whole_lp_name = loop_name + "_" + std::to_string(i+1);
				wholeloopName2loopBoundMapTy::iterator it_whole = wholeloopName2loopBoundMap.find(whole_lp_name);
				assert(it_whole != wholeloopName2loopBoundMap.end() && "Error: Can not find loop name in wholeloopName2loopBoundMap!\n");
				unsigned bound = wholeloopName2loopBoundMap[whole_lp_name];
				lp_bounds[i] = bound;
			}

			for (unsigned i = 1; i < lp_bounds.size(); i++) {
				std::string whole_lp_name = loop_name + "_" + std::to_string(i + 1);
				wholeloopName2loopBoundMapTy::iterator it_whole = wholeloopName2loopBoundMap.find(whole_lp_name);
				assert(it_whole != wholeloopName2loopBoundMap.end() && "Error: Can not find loop name in wholeloopName2loopBoundMap!\n");
				lp_bounds[i] = lp_bounds[i] / lp_bounds[i - 1];
				unsigned lpbound = lp_bounds[i];
				wholeloopName2loopBoundMap[whole_lp_name] = lpbound;
			}
		}
		/*
		for (; it_lpbound != ie_lpbound; ++it_lpbound) {
			unsigned size_level = it_lpbound->second.size();
			assert((size_level >= 1) && "Error: loop level in this loop is less than 1!\n");
			if (size_level != 1) {
				std::vector<uint64_t> lpbound = it_lpbound->second;
				for (unsigned i = 1; i < size_level; i++) {
					it_lpbound->second[i] = lpbound[i] / lpbound[i - 1];
				}
			}
		}*/
	}

	line_from_to_Ty from_to_pair = std::make_pair(from, to);
	return from_to_pair;
}

void DDDG::extract_trace_file(gzFile& trace_file) {
	char buffer[256];
	while (!gzeof(trace_file))
	{
		if (gzgets(trace_file, buffer, sizeof(buffer)) == Z_NULL)
			continue;
		std::string wholeline(buffer);
		size_t pos_end_tag = wholeline.find(",");

		if (pos_end_tag == std::string::npos) {
			continue;
		}
		std::string tag = wholeline.substr(0, pos_end_tag);
		std::string line_left = wholeline.substr(pos_end_tag + 1);
		if (tag.compare("0") == 0)
			parse_instruction_line(line_left);
		else if (tag.compare("r") == 0)
			parse_result(line_left);
		else if (tag.compare("f") == 0)
			parse_forward(line_left);
		else
			parse_parameter(line_left, atoi(tag.c_str()));
	}

	gzclose(trace_file);

	/*
	while(!feof(tracefile))
	{
	if (fgets(buffer, sizeof(buffer), tracefile) == NULL)
	continue;
	std::string wholeline(buffer);
	size_t pos_end_tag = wholeline.find(",");

	if(pos_end_tag == std::string::npos) {
	continue;
	}
	std::string tag = wholeline.substr(0,pos_end_tag);
	std::string line_left = wholeline.substr(pos_end_tag + 1);
	if (tag.compare("0") == 0)
	parse_instruction_line(line_left);
	else if (tag.compare("r")  == 0)
	parse_result(line_left);
	else if (tag.compare("f")  == 0)
	parse_forward(line_left);
	else
	parse_parameter(line_left, atoi(tag.c_str()));
	}

	fclose(tracefile);
	*/
}

void DDDG::extract_trace_file(gzFile& trace_file, uint64_t from, uint64_t to) {

	char buffer[256];
	uint64_t inst_count = 0;
	bool parse_inst_flag = false;
	while (!gzeof(trace_file))
	{
		if (gzgets(trace_file, buffer, sizeof(buffer)) == Z_NULL)
			continue;
		std::string wholeline(buffer);
		size_t pos_end_tag = wholeline.find(",");

		if (pos_end_tag == std::string::npos) {
			continue;
		}
		std::string tag = wholeline.substr(0, pos_end_tag);
		std::string line_left = wholeline.substr(pos_end_tag + 1);
		if (tag.compare("0") == 0){
			if ( (inst_count >= from) && (inst_count <= to) ) {
				parse_instruction_line(line_left);
				parse_inst_flag = true;
			}
			else {
				parse_inst_flag = false;
			}
			inst_count++;
		}

		if ( (tag.compare("0") != 0) && parse_inst_flag == true ) {
			if (tag.compare("r") == 0) {
				parse_result(line_left);
			}
			else if (tag.compare("f") == 0) {
				parse_forward(line_left);
			}
			else {
				parse_parameter(line_left, atoi(tag.c_str()));
			}
		}
		else {
			if (inst_count > to) {
				break;
			}
		}

	}

	gzclose(trace_file);

	/*
	while(!feof(tracefile))
	{
	if (fgets(buffer, sizeof(buffer), tracefile) == NULL)
	continue;
	std::string wholeline(buffer);
	size_t pos_end_tag = wholeline.find(",");

	if(pos_end_tag == std::string::npos) {
	continue;
	}
	std::string tag = wholeline.substr(0,pos_end_tag);
	std::string line_left = wholeline.substr(pos_end_tag + 1);
	if (tag.compare("0") == 0)
	parse_instruction_line(line_left);
	else if (tag.compare("r")  == 0)
	parse_result(line_left);
	else if (tag.compare("f")  == 0)
	parse_forward(line_left);
	else
	parse_parameter(line_left, atoi(tag.c_str()));
	}

	fclose(tracefile);
	*/
}
#endif
