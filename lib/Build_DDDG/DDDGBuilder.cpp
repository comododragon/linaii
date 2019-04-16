#include "profile_h/DDDGBuilder.h"

#include "profile_h/BaseDatapath.h"

#ifdef USE_FUTURE
FutureCache::FutureCache(unsigned unrollFactor) : unrollFactor(unrollFactor), computed(false) { }

void FutureCache::saveInterval(intervalTy interval) {
	assert(!computed && "Attempt to save interval on a computed future cache");
	this->interval = interval;
	computed = true;
}
#endif

ParsedTraceContainer::ParsedTraceContainer(std::string kernelName) {
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

	compressed = args.compressed;
	keepAliveRead = false;
	keepAliveWrite = false;
	locked = false;

	funcList.clear();
	instIDList.clear();
	lineNoList.clear();
	memoryTraceList.clear();
	getElementPtrList.clear();
	prevBasicBlockList.clear();
	currBasicBlockList.clear();
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

void ParsedTraceContainer::openAndClearAllFiles() {
	assert(!locked && "This container is locked, no modification permitted");
	assert(!keepAliveRead && "This container is open for read, no modification permitted");

	closeAllFiles();

	if(compressed) {
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

		keepAliveWrite = true;
	}
}

void ParsedTraceContainer::openAllFilesForRead() {
	assert(!keepAliveWrite && "This container is open for write, no reading permitted");

	closeAllFiles();

	if(compressed) {
		funcFile = gzopen(funcFileName.c_str(), "r");
		assert(funcFile != Z_NULL && "Could not open dynamic funcID file for read");
		instIDFile = gzopen(instIDFileName.c_str(), "r");
		assert(instIDFile != Z_NULL && "Could not open instID file for read");
		lineNoFile = gzopen(lineNoFileName.c_str(), "r");
		assert(lineNoFile != Z_NULL && "Could not open line num file for read");
		memoryTraceFile = gzopen(memoryTraceFileName.c_str(), "r");
		assert(memoryTraceFile != Z_NULL && "Could not open memory trace file for read");
		getElementPtrFile = gzopen(getElementPtrFileName.c_str(), "r");
		assert(getElementPtrFile != Z_NULL && "Could not open getelementptr file for read");
		prevBasicBlockFile = gzopen(prevBasicBlockFileName.c_str(), "r");
		assert(prevBasicBlockFile != Z_NULL && "Could not open prev BB file for read");
		currBasicBlockFile = gzopen(currBasicBlockFileName.c_str(), "r");
		assert(currBasicBlockFile != Z_NULL && "Could not open curr BB file for read");

		keepAliveRead = true;
	}
}

void ParsedTraceContainer::closeAllFiles() {
	if(compressed) {
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
		funcFile = Z_NULL;
		instIDFile = Z_NULL;
		lineNoFile = Z_NULL;
		memoryTraceFile = Z_NULL;
		getElementPtrFile = Z_NULL;
		prevBasicBlockFile = Z_NULL;
		currBasicBlockFile = Z_NULL;

		keepAliveRead = false;
		keepAliveWrite = false;
	}
}

void ParsedTraceContainer::lock() {
	locked = true;
}

void ParsedTraceContainer::appendToFuncList(std::string elem) {
	assert(!locked && "This container is locked, no modification permitted");
	assert(!keepAliveRead && "This container is open for read, no modification permitted");

	if(compressed) {
		if(!funcFile) {
			funcFile = gzopen(funcFileName.c_str(), "a");
			assert(funcFile != Z_NULL && "Could not open dynamic funcID file for write");
		}

		gzprintf(funcFile, "%s\n", elem.c_str());

		if(!keepAliveWrite) {
			gzclose(funcFile);
			funcFile = Z_NULL;
		}
	}
	else {
		funcList.push_back(elem);
	}
}

void ParsedTraceContainer::appendToInstIDList(std::string elem) {
	assert(!locked && "This container is locked, no modification permitted");
	assert(!keepAliveRead && "This container is open for read, no modification permitted");

	if(compressed) {
		if(!instIDFile) {
			instIDFile = gzopen(instIDFileName.c_str(), "a");
			assert(instIDFile != Z_NULL && "Could not open dynamic instID file for write");
		}

		gzprintf(instIDFile, "%s\n", elem.c_str());

		if(!keepAliveWrite) {
			gzclose(instIDFile);
			instIDFile = Z_NULL;
		}
	}
	else {
		instIDList.push_back(elem);
	}
}

void ParsedTraceContainer::appendToLineNoList(int elem) {
	assert(!locked && "This container is locked, no modification permitted");
	assert(!keepAliveRead && "This container is open for read, no modification permitted");

	if(compressed) {
		if(!lineNoFile) {
			lineNoFile = gzopen(lineNoFileName.c_str(), "a");
			assert(lineNoFile != Z_NULL && "Could not open line num file for write");
		}

		gzprintf(lineNoFile, "%d\n", elem);

		if(!keepAliveWrite) {
			gzclose(lineNoFile);
			lineNoFile = Z_NULL;
		}
	}
	else {
		lineNoList.push_back(elem);
	}
}

void ParsedTraceContainer::appendToMemoryTraceList(int key, int64_t elem, unsigned elem2) {
	assert(!locked && "This container is locked, no modification permitted");
	assert(!keepAliveRead && "This container is open for read, no modification permitted");

	if(compressed) {
		if(!memoryTraceFile) {
			memoryTraceFile = gzopen(memoryTraceFileName.c_str(), "a");
			assert(memoryTraceFile != Z_NULL && "Could not open memory trace file for write");
		}

		gzprintf(memoryTraceFile, "%d,%ld,%u\n", key, elem, elem2);

		if(!keepAliveWrite) {
			gzclose(memoryTraceFile);
			memoryTraceFile = Z_NULL;
		}
	}
	else {
		memoryTraceList.insert(std::make_pair(key, std::make_pair(elem, elem2)));
	}
}

void ParsedTraceContainer::appendToGetElementPtrList(int key, std::string elem, int64_t elem2) {
	assert(!locked && "This container is locked, no modification permitted");
	assert(!keepAliveRead && "This container is open for read, no modification permitted");

	// XXX: Substitute the elem (which can be arrayidxXX) for the array name (e.g. A, B, etc)
	// In the original code, this is performed when the getElementPtrList is requested, not
	// when the element is inserted. However, getElementPtrList is only used at initBaseAddress(),
	// where the array name is used instead of the arrayidxXX. Therefore, I think there is no
	// problem to add the arrayidxXX-to-arrayName conversion here instead of in getGetElementPtr()
	getElementPtrName2arrayNameMapTy::iterator found = getElementPtrName2arrayNameMap.find(elem);
	std::string arrayName = (found != getElementPtrName2arrayNameMap.end())? found->second : elem;

	if(compressed) {
		if(!getElementPtrFile) {
			getElementPtrFile = gzopen(getElementPtrFileName.c_str(), "a");
			assert(getElementPtrFile != Z_NULL && "Could not open getelementptr file for write");
		}

		gzprintf(getElementPtrFile, "%d,%s,%ld\n", key, arrayName.c_str(), elem2);

		if(!keepAliveWrite) {
			gzclose(getElementPtrFile);
			getElementPtrFile = Z_NULL;
		}
	}
	else {
		getElementPtrList.insert(std::make_pair(key, std::make_pair(arrayName, elem2)));
	}
}

void ParsedTraceContainer::appendToPrevBBList(std::string elem) {
	assert(!locked && "This container is locked, no modification permitted");
	assert(!keepAliveRead && "This container is open for read, no modification permitted");

	if(compressed) {
		if(!prevBasicBlockFile) {
			prevBasicBlockFile = gzopen(prevBasicBlockFileName.c_str(), "a");
			assert(prevBasicBlockFile != Z_NULL && "Could not open prev BB file for write");
		}

		gzprintf(prevBasicBlockFile, "%s\n", elem.c_str());

		if(!keepAliveWrite) {
			gzclose(prevBasicBlockFile);
			prevBasicBlockFile = Z_NULL;
		}
	}
	else {
		prevBasicBlockList.push_back(elem);
	}
}

void ParsedTraceContainer::appendToCurrBBList(std::string elem) {
	assert(!locked && "This container is locked, no modification permitted");
	assert(!keepAliveRead && "This container is open for read, no modification permitted");

	if(compressed) {
		if(!currBasicBlockFile) {
			currBasicBlockFile = gzopen(currBasicBlockFileName.c_str(), "a");
			assert(currBasicBlockFile != Z_NULL && "Could not open curr BB file for write");
		}

		gzprintf(currBasicBlockFile, "%s\n", elem.c_str());

		if(!keepAliveWrite) {
			gzclose(currBasicBlockFile);
			currBasicBlockFile = Z_NULL;
		}
	}
	else {
		currBasicBlockList.push_back(elem);
	}
}

const std::vector<std::string> &ParsedTraceContainer::getFuncList() {
	if(compressed) {
		assert(!keepAliveWrite && "This container is open for write, no reading permitted");

		if(!funcFile) {
			funcFile = gzopen(funcFileName.c_str(), "r");
			assert(funcFile != Z_NULL && "Could not open dynamic funcID file for read");
		}

		funcList.clear();

		char buffer[BUFF_STR_SZ];
		while(!gzeof(funcFile)) {
			if(Z_NULL == gzgets(funcFile, buffer, sizeof(buffer)))
				continue;

			std::string line(buffer);
			line.pop_back();
			funcList.push_back(line);
		}

		if(!keepAliveRead) {
			gzclose(funcFile);
			funcFile = Z_NULL;
		}
	}

	return funcList;
}

const std::vector<std::string> &ParsedTraceContainer::getInstIDList() {
	if(compressed) {
		assert(!keepAliveWrite && "This container is open for write, no reading permitted");

		if(!instIDFile) {
			instIDFile = gzopen(instIDFileName.c_str(), "r");
			assert(instIDFile != Z_NULL && "Could not open instID file for read");
		}

		instIDList.clear();

		char buffer[BUFF_STR_SZ];
		while(!gzeof(instIDFile)) {
			if(Z_NULL == gzgets(instIDFile, buffer, sizeof(buffer)))
				continue;

			std::string line(buffer);
			line.pop_back();
			instIDList.push_back(line);
		}

		if(!keepAliveRead) {
			gzclose(instIDFile);
			instIDFile = Z_NULL;
		}
	}

	return instIDList;
}

const std::vector<int> &ParsedTraceContainer::getLineNoList() {
	if(compressed) {
		assert(!keepAliveWrite && "This container is open for write, no reading permitted");

		if(!lineNoFile) {
			lineNoFile = gzopen(lineNoFileName.c_str(), "r");
			assert(lineNoFile != Z_NULL && "Could not open line num file for read");
		}

		lineNoList.clear();

		char buffer[BUFF_STR_SZ];
		while(!gzeof(lineNoFile)) {
			if(Z_NULL == gzgets(lineNoFile, buffer, sizeof(buffer)))
				continue;

			int elem;
			sscanf(buffer, "%d\n", &elem);
			lineNoList.push_back(elem);
		}

		if(!keepAliveRead) {
			gzclose(lineNoFile);
			lineNoFile = Z_NULL;
		}
	}

	return lineNoList;
}

const std::unordered_map<int, std::pair<int64_t, unsigned>> &ParsedTraceContainer::getMemoryTraceList() {
	if(compressed) {
		assert(!keepAliveWrite && "This container is open for write, no reading permitted");

		if(!memoryTraceFile) {
			memoryTraceFile = gzopen(memoryTraceFileName.c_str(), "r");
			assert(memoryTraceFile != Z_NULL && "Could not open memory trace file for read");
		}

		memoryTraceList.clear();

		char buffer[BUFF_STR_SZ];
		while(!gzeof(memoryTraceFile)) {
			if(Z_NULL == gzgets(memoryTraceFile, buffer, sizeof(buffer)))
				continue;

			int elem;
			int64_t elem2;
			unsigned elem3;
			sscanf(buffer, "%d,%ld,%u\n", &elem, &elem2, &elem3);
			memoryTraceList.insert(std::make_pair(elem, std::make_pair(elem2, elem3)));
		}

		if(!keepAliveRead) {
			gzclose(memoryTraceFile);
			memoryTraceFile = Z_NULL;
		}
	}

	return memoryTraceList;
}

const std::unordered_map<int, std::pair<std::string, int64_t>> &ParsedTraceContainer::getGetElementPtrList() {
	if(compressed) {
		assert(!keepAliveWrite && "This container is open for write, no reading permitted");

		if(!getElementPtrFile) {
			getElementPtrFile = gzopen(getElementPtrFileName.c_str(), "r");
			assert(getElementPtrFile != Z_NULL && "Could not open getelementptr file for read");
		}

		getElementPtrList.clear();

		char buffer[BUFF_STR_SZ];
		while(!gzeof(getElementPtrFile)) {
			if(Z_NULL == gzgets(getElementPtrFile, buffer, sizeof(buffer)))
				continue;

			int elem;
			char elem2[BUFF_STR_SZ];
			int64_t elem3;
			sscanf(buffer, "%d,%[^,],%ld\n", &elem, elem2, &elem3);
			getElementPtrList.insert(std::make_pair(elem, std::make_pair(std::string(elem2), elem3)));
		}

		if(!keepAliveRead) {
			gzclose(getElementPtrFile);
			getElementPtrFile = Z_NULL;
		}
	}

	return getElementPtrList;
}

const std::vector<std::string> &ParsedTraceContainer::getPrevBBList() {
	if(compressed) {
		assert(!keepAliveWrite && "This container is open for write, no reading permitted");

		if(!prevBasicBlockFile) {
			prevBasicBlockFile = gzopen(prevBasicBlockFileName.c_str(), "r");
			assert(prevBasicBlockFile != Z_NULL && "Could not open prev BB file for read");
		}

		prevBasicBlockList.clear();

		char buffer[BUFF_STR_SZ];
		while(!gzeof(prevBasicBlockFile)) {
			if(Z_NULL == gzgets(prevBasicBlockFile, buffer, sizeof(buffer)))
				continue;

			std::string line(buffer);
			line.pop_back();
			prevBasicBlockList.push_back(line);
		}

		if(!keepAliveRead) {
			gzclose(prevBasicBlockFile);
			prevBasicBlockFile = Z_NULL;
		}
	}

	return prevBasicBlockList;
}

const std::vector<std::string> &ParsedTraceContainer::getCurrBBList() {
	if(compressed) {
		assert(!keepAliveWrite && "This container is open for write, no reading permitted");

		if(!currBasicBlockFile) {
			currBasicBlockFile = gzopen(currBasicBlockFileName.c_str(), "r");
			assert(currBasicBlockFile != Z_NULL && "Could not open curr BB file for read");
		}

		currBasicBlockList.clear();

		char buffer[BUFF_STR_SZ];
		while(!gzeof(currBasicBlockFile)) {
			if(Z_NULL == gzgets(currBasicBlockFile, buffer, sizeof(buffer)))
				continue;

			std::string line(buffer);
			line.pop_back();
			currBasicBlockList.push_back(line);
		}

		if(!keepAliveRead) {
			gzclose(currBasicBlockFile);
			currBasicBlockFile = Z_NULL;
		}
	}

	return currBasicBlockList;
}

#ifdef USE_FUTURE
DDDGBuilder::DDDGBuilder(BaseDatapath *datapath, ParsedTraceContainer &PC, FutureCache *future) : datapath(datapath), PC(PC), future(future) {
#else
DDDGBuilder::DDDGBuilder(BaseDatapath *datapath, ParsedTraceContainer &PC) : datapath(datapath), PC(PC) {
#endif
	numOfInstructions = -1;
	lastParameter = true;
	prevBB = "-1";
	numOfRegDeps = 0;
	numOfMemDeps = 0;
}

void DDDGBuilder::buildInitialDDDG() {
	std::string traceFileName = args.workDir + FILE_DYNAMIC_TRACE;
	gzFile traceFile;

	traceFile = gzopen(traceFileName.c_str(), "r");
	assert(traceFile != Z_NULL && "Could not open trace input file");

	VERBOSE_PRINT(errs() << "\t\tStarted build of initial DDDG\n");

	//lineFromToTy fromToPair = getTraceLineFromTo(traceFile);
	//parseTraceFile(traceFile, fromToPair);
#ifdef USE_FUTURE
	intervalTy interval;
	// If interval was computed on a previous DynamicDatapath construction, use that
	if(future && future->isComputed())
		interval = future->getInterval();
	else
		interval = getTraceLineFromTo(traceFile);

	VERBOSE_PRINT(errs() << "\t\tUsing cached interval of trace to analyse\n");
#else
	intervalTy interval = getTraceLineFromTo(traceFile);
#endif

	VERBOSE_PRINT(errs() << "\t\tSkipping " << std::to_string(std::get<0>(interval)) << " bytes from trace\n");
	VERBOSE_PRINT(errs() << "\t\tEnd of interval: " << std::to_string(std::get<1>(interval)) << "\n");

	parseTraceFile(traceFile, interval);

	writeDDDG();

	VERBOSE_PRINT(errs() << "\t\tNumber of nodes: " << std::to_string(datapath->getNumNodes()) << "\n");
	VERBOSE_PRINT(errs() << "\t\tNumber of edges: " << std::to_string(datapath->getNumEdges()) << "\n");
	VERBOSE_PRINT(errs() << "\t\tNumber of register dependencies: " << std::to_string(getNumOfRegisterDependencies()) << "\n");
	VERBOSE_PRINT(errs() << "\t\tNumber of memory dependencies: " << std::to_string(getNumOfMemoryDependencies()) << "\n");
	VERBOSE_PRINT(errs() << "\t\tDDDG build finished\n");

	// TODO: write graph property csv?
}

unsigned DDDGBuilder::getNumOfRegisterDependencies() {
	return numOfRegDeps;
}

unsigned DDDGBuilder::getNumOfMemoryDependencies() {
	return numOfMemDeps;
}

intervalTy DDDGBuilder::getTraceLineFromTo(gzFile &traceFile) {
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
#ifdef USE_FUTURE
	bool actualComputed = false;
	// XXX: I AM ASSUMING THAT THIS WILL ONLY BE POSSIBLY FALSE ON FIRST DYNAMICDATAPATH CONSTRUCTION EVER
	// XXX: This method iterates from the beginning of file. If loopBound == 0, there will be no break in the
	// XXX: line reading loop, i.e. ALL LOOP BOUNDS WILL BE COMPUTED, therefore if skipRuntimeLoopBound == false,
	// XXX: it won't be in the future again.
	// XXX: Since future cache skips this getLineFromTo() when generated, wholeloopName2loopBoundMap is not updated!
	// XXX: But if wholeloopName2loopBoundMap is updated only once, this won't be a problem
#endif
	bool skipRuntimeLoopBound = (loopBound > 0);

#ifdef USE_FUTURE
	// Handle future generation logic
	bool computeFuture = future;
	bool futureComputed = !computeFuture;
	uint64_t futureUnrollFactor = computeFuture? future->getUnrollFactor() : 0;
	if(computeFuture)
		assert(!(future->isComputed()) && "Future cache computation was asked but its values are already computed");
#endif

#ifdef PROGRESSIVE_TRACE_CURSOR
	uint64_t instCount = progressiveTraceInstCount;
#else
	uint64_t instCount = 0;
#endif
#ifdef USE_FUTURE
	uint64_t byteFrom, to = 0, futureTo = 0;
#else
	uint64_t byteFrom, to = 0;
#endif
	// This queue saves exactly the last (numInstInHeaderBB - 1) byte offsets for the beggining of each line
	LimitedQueue lineByteOffset(numInstInHeaderBB - 1);
	bool firstTraverseHeader = true;
	uint64_t lastInstExitingCounter = 0;
	char buffer[BUFF_STR_SZ];

	// Iterate through dynamic trace
#ifdef PROGRESSIVE_TRACE_CURSOR
	if(args.progressive)
		VERBOSE_PRINT(errs() << "\t\tUsing progressive trace cursor, skipping " << std::to_string(progressiveTraceCursor) << " bytes from trace\n");
	gzseek(traceFile, progressiveTraceCursor, SEEK_SET);
#else
	gzrewind(traceFile);
#endif
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
#if 0
			if(firstTraverseHeader && !instName.compare(lastInstHeaderBB)) {
				from = count - numInstInHeaderBB + 1;
				firstTraverseHeader = false;
			}
#endif
#if 1
			if(firstTraverseHeader) {
				instCount++;

				if(!instName.compare(lastInstHeaderBB)) {
					// Save in byteFrom the amount of bytes between beginning of trace of file and first instruction
					// of first loop iteration (the front() of this queue has the line byte offset for the header)
					byteFrom = lineByteOffset.front();
					instCount -= numInstInHeaderBB;
					firstTraverseHeader = false;

#ifdef PROGRESSIVE_TRACE_CURSOR
					if(args.progressive) {
						progressiveTraceCursor = byteFrom;
						progressiveTraceInstCount = instCount;
					}
#endif
				}
				else {
					// Save this line byte offset
					lineByteOffset.push(gztell(traceFile) - line.size());
				}
			}
#endif

			// Mark the last line of the last iteration of this loop
			if(!instName.compare(lastInstExitingBB)) {
				lastInstExitingCounter++;

#ifdef USE_FUTURE
				// Compute actual "to" value
				if(!actualComputed && unrollFactor == lastInstExitingCounter) {
					to = count;
					actualComputed = true;
				}

				// Compute future "to" value (only if asked)
				if(computeFuture && !futureComputed && (futureUnrollFactor == lastInstExitingCounter)) {
					futureTo = count;
					futureComputed = true;
				}

				// If we computed all "to" values and no runtime loop bound must be calculated, we can stop now
				if(actualComputed && futureComputed && skipRuntimeLoopBound)
					break;
#else
				if(unrollFactor == lastInstExitingCounter) {
					to = count;

					// If we don't need to calculate runtime loop bound, we can stop now
					if(skipRuntimeLoopBound)
						break;
				}
#endif
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
		VERBOSE_PRINT(errs() << "\t\tThere are loops with unknown static bounds, using trace to determine their bounds\n");

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

#ifdef USE_FUTURE
	// If future was asked to be computed, save interval
	if(computeFuture && futureComputed)
		future->saveInterval(std::make_tuple(byteFrom, futureTo, instCount));
#endif

	return std::make_tuple(byteFrom, to, instCount);
}

void DDDGBuilder::parseTraceFile(gzFile &traceFile, intervalTy interval) {
	PC.openAndClearAllFiles();

#if 0
// XXX: New simpler logic
	uint64_t from = fromToPair.first, to = fromToPair.second;
	uint64_t instCount = 0;
#endif
#if 0
// XXX: Old logic that seems buggish
	uint64_t from = fromToPair.first, to = fromToPair.second;
	uint64_t instCount = 0;
	bool parseInst = false;
#endif
#if 1
// XXX: New logic based on old logic that speeds up file reading
	uint64_t from = std::get<0>(interval), to = std::get<1>(interval);
	uint64_t instCount = std::get<2>(interval);
	bool parseInst = false;
#endif
	char buffer[BUFF_STR_SZ];

	// Iterate through dynamic trace, but only process the specified interval
#if 0
// XXX: New simpler logic and old logic
	gzrewind(traceFile);
#endif
#if 1
// XXX: New logic based on old logic that speeds up file reading
	gzseek(traceFile, from, SEEK_SET);
#endif
	while(!gzeof(traceFile)) {
		if(Z_NULL == gzgets(traceFile, buffer, sizeof(buffer)))
			continue;

		std::string line(buffer);
		size_t tagPos = line.find(",");

		if(std::string::npos == tagPos)
			continue;

		std::string tag = line.substr(0, tagPos);
		rest = line.substr(tagPos + 1);

#if 0
// XXX: New simpler logic
		if(!tag.compare("0"))
			instCount++;

		if(instCount > from && instCount <= to) {
			if(!tag.compare("0"))
				parseInstructionLine();
			else if(!tag.compare("r"))
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
#endif
#if 0
// XXX: Old logic that seems buggish
		if(!tag.compare("0")) {
			if(instCount >= from && instCount <= to) {
				parseInstructionLine();
				parseInst = true;
			}
			else {
				parseInst = false;
			}
			instCount++;
		}

		if(tag.compare("0") && parseInst) {
			if(!tag.compare("r"))
				parseResult();
			else if(!tag.compare("f"))
				parseForward();
			else
				parseParameter(std::atoi(tag.c_str()));
		}
		else if(instCount > to) {
			break;
		}
#endif
#if 1
// XXX: New logic based on old logic that speeds up file reading
		if(!tag.compare("0")) {
			if(instCount <= to) {
				parseInstructionLine();
				parseInst = true;
			}
			else {
				parseInst = false;
			}
			instCount++;
		}

		if(tag.compare("0") && parseInst) {
			if(!tag.compare("r"))
				parseResult();
			else if(!tag.compare("f"))
				parseForward();
			else
				parseParameter(std::atoi(tag.c_str()));
		}
		else if(instCount > to) {
			break;
		}
#endif
	}

	PC.closeAllFiles();
	PC.lock();
}

void DDDGBuilder::parseInstructionLine() {
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
#ifdef LEGACY_SEPARATOR
				currDynamicFunction = currStaticFunction + "-0";
#else
				currDynamicFunction = currStaticFunction + GLOBAL_SEPARATOR "0";
#endif
				activeMethod.push(std::make_pair(currStaticFunction, 0));
			}
			// Update (increment) counter for this function
			else {
				found->second++;
#ifdef LEGACY_SEPARATOR
				currDynamicFunction = currStaticFunction + "-" + std::to_string(found->second);
#else
				currDynamicFunction = currStaticFunction + GLOBAL_SEPARATOR + std::to_string(found->second);
#endif
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
#ifdef LEGACY_SEPARATOR
				currDynamicFunction = currStaticFunction + "-" + std::to_string(found->second);
#else
				currDynamicFunction = currStaticFunction + GLOBAL_SEPARATOR + std::to_string(found->second);
#endif
				activeMethod.push(std::make_pair(currStaticFunction, found->second));
			}
			// Nothing changed, just change the current dynamic function
			else {
#ifdef LEGACY_SEPARATOR
				currDynamicFunction = prevStaticFunction + "-" + std::to_string(prevCount);
#else
				currDynamicFunction = prevStaticFunction + GLOBAL_SEPARATOR + std::to_string(prevCount);
#endif
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
#ifdef LEGACY_SEPARATOR
			currDynamicFunction = currStaticFunction + "-0";
#else
			currDynamicFunction = currStaticFunction + GLOBAL_SEPARATOR "0";
#endif
			activeMethod.push(std::make_pair(currStaticFunction, 0));
			functionCounter.insert(std::make_pair(currStaticFunction, 0));
		}
		// Update (increment) counter for this function
		else {
			found->second++;
#ifdef LEGACY_SEPARATOR
			currDynamicFunction = currStaticFunction + "-" + std::to_string(found->second);
#else
			currDynamicFunction = currStaticFunction + GLOBAL_SEPARATOR + std::to_string(found->second);
#endif
			activeMethod.push(std::make_pair(currStaticFunction, found->second));
		}
	}

	// If this is a PHI instruction and last instruction was a branch, update BB pointers
	if(isPhiOp(microop) && LLVM_IR_Br == prevMicroop)
		prevBB = currBB;
	currBB = bbID;

	// Store collected info to compressed files or memory lists
	PC.appendToFuncList(currDynamicFunction);
	PC.appendToInstIDList(currInstID);
	PC.appendToLineNoList(lineNo);
	PC.appendToPrevBBList(prevBB);
	PC.appendToCurrBBList(currBB);

	// Reset variables for the following lines
	numOfInstructions++;
	lastParameter = true;
	parameterValuePerInst.clear();
	parameterSizePerInst.clear();
	parameterLabelPerInst.clear();
}

void DDDGBuilder::parseResult() {
	int size;
	double value;
	int isReg;
	char buffer[BUFF_STR_SZ];
	sscanf(rest.c_str(), "%d,%lf,%d,%[^\n]\n", &size, &value, &isReg, buffer);
	std::string label(buffer);

	assert(isReg && "Result trace line must be a register");

#ifdef LEGACY_SEPARATOR
	std::string uniqueRegID = currDynamicFunction + "-" + label;
#else
	std::string uniqueRegID = currDynamicFunction + GLOBAL_SEPARATOR + label;
#endif

	// Store the instruction where this register was written
	s2uMap::iterator found = registerLastWritten.find(uniqueRegID);
	if(found != registerLastWritten.end())
		found->second = numOfInstructions;
	else
		registerLastWritten.insert(std::make_pair(uniqueRegID, numOfInstructions));

	// Register an allocation request
	if(LLVM_IR_Alloca == currMicroop) {
		PC.appendToGetElementPtrList(numOfInstructions, label, (int64_t) value);
	}
	// Register a load
	else if(isLoadOp(currMicroop)) {
		int64_t addr = parameterValuePerInst.back();
		PC.appendToMemoryTraceList(numOfInstructions, addr, size);
	}
	// Register a DMA request
	else if(isDMAOp(currMicroop)) {
		int64_t addr = parameterValuePerInst[1];
		unsigned memSize = parameterValuePerInst[2];
		PC.appendToMemoryTraceList(numOfInstructions, addr, memSize);
	}
}

void DDDGBuilder::parseForward() {
	int size, isReg;
	double value;
	char buffer[BUFF_STR_SZ];
	sscanf(rest.c_str(), "%d,%lf,%d,%[^\n]\n", &size, &value, &isReg, buffer);
	std::string label(buffer);

	assert(isReg && "Forward trace line must be a register");
	assert(isCallOp(currMicroop) && "Invalid forward line found in trace with no attached DMA/call instruction");

#ifdef LEGACY_SEPARATOR
	std::string uniqueRegID = calleeDynamicFunction + "-" + label;
#else
	std::string uniqueRegID = calleeDynamicFunction + GLOBAL_SEPARATOR + label;
#endif

	int tmpWrittenInst = (lastCallSource != -1)? lastCallSource : numOfInstructions;

	s2uMap::iterator found = registerLastWritten.find(uniqueRegID);
	if(found != registerLastWritten.end())
		found->second = tmpWrittenInst;
	else
		registerLastWritten.insert(std::make_pair(uniqueRegID, tmpWrittenInst));
}

void DDDGBuilder::parseParameter(int param) {
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
#ifdef LEGACY_SEPARATOR
		if(found != functionCounter.end())
			calleeDynamicFunction = calleeFunction + "-" + std::to_string(found->second + 1);
		else
			calleeDynamicFunction = calleeFunction + "-0";
#else
		if(found != functionCounter.end())
			calleeDynamicFunction = calleeFunction + GLOBAL_SEPARATOR + std::to_string(found->second + 1);
		else
			calleeDynamicFunction = calleeFunction + GLOBAL_SEPARATOR "0";
#endif
	}

	// Note that the last parameter is listed first in the trace, hence this non-intuitive logic
	lastParameter = false;
	lastCallSource = -1;

	// If this is a register, we must check about dependency
	if(isReg) {
		// If this is a PHI node and previous analysed BB is the same as the PHI operand, no need to check for dependency
		bool processDep = true;
		if(isPhiOp(currMicroop)) {
			std::string operandBB = instName2bbNameMap.at(label);
			if(operandBB != prevBB)
				processDep = false;
		}

		// Process register dependency
		if(processDep) {
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
					if((unsigned) numOfInstructions == sink->second.sink) {
						exists = true;
						break;
					}
				}

				// Update, register a new memory dependency
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
			PC.appendToGetElementPtrList(numOfInstructions, baseLabel, addr);
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

			PC.appendToGetElementPtrList(numOfInstructions, baseLabel, addr);
		}
		// First parameter of store is the value
		else if(1 == param && isStoreOp(currMicroop)) {
			int64_t addr = parameterValuePerInst[0];
			unsigned size = parameterSizePerInst.back();
			PC.appendToMemoryTraceList(numOfInstructions, addr, size);
		}
		else if(1 == param && LLVM_IR_GetElementPtr == currMicroop) {
			int64_t addr = parameterValuePerInst.back();
			std::string label = parameterLabelPerInst.back();
			PC.appendToGetElementPtrList(numOfInstructions, label, addr);
		}
	}
}

void DDDGBuilder::writeDDDG() {
	for(auto &it : registerEdgeTable)
		datapath->insertDDDGEdge(it.first, it.second.sink, it.second.paramID);

	for(auto &it : memoryEdgeTable)
		datapath->insertDDDGEdge(it.first, it.second.sink, it.second.paramID);
}
