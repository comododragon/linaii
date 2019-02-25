#include "profile_h/auxiliary.h"

#include "llvm/IR/Verifier.h"

using namespace llvm;

const std::string loopNumberMDKindName = "lia.kernelloopnumber";
const std::string assignBasicBlockIDMDKindName = "lia.kernelbbid";
const std::string assignLoadStoreIDMDKindName = "lia.loadstoreid"; 
const std::string extractLoopInfoMDKindName = "lia.kernelloopinfo";

bool isFunctionOfInterest(std::string key) {
	std::vector<std::string>::iterator it;
	it = std::find(args.kernelNames.begin(), args.kernelNames.end(), key);
	return (it != args.kernelNames.end());
}

bool verifyModuleAndPrintErrors(Module &M) {
	std::string errorStr;
	raw_string_ostream OS(errorStr);
	if(verifyModule(M, &OS)) {
		errs() << OS.str() << "\n";
		return false;
	}

	return true;
}

std::string constructLoopName(std::string funcName, unsigned loopNo, unsigned depth) {
#ifdef LEGACY_SEPARATOR
	std::string loopName = funcName + "_loop-" + std::to_string(loopNo);
#else
	std::string loopName = funcName + GLOBAL_SEPARATOR + "loop" + GLOBAL_SEPARATOR + std::to_string(loopNo);
#endif

	if(((unsigned) -1) == depth)
		return loopName;

	return appendDepthToLoopName(loopName, depth);
}

std::string appendDepthToLoopName(std::string loopName, unsigned depth) {
#ifdef LEGACY_SEPARATOR
	return loopName + "_" + std::to_string(depth);
#else
	return loopName + GLOBAL_SEPARATOR + std::to_string(depth);
#endif
}

std::tuple<std::string, unsigned> parseLoopName(std::string loopName) {
#ifdef LEGACY_SEPARATOR
	const std::string mainLoopTag = "_loop-";
	const size_t mainLoopTagSize = 6;
#else
	const std::string mainLoopTag = GLOBAL_SEPARATOR "loop" GLOBAL_SEPARATOR;
	const size_t mainLoopTagSize = 6;
#endif

	size_t tagPos = loopName.find(mainLoopTag);
	std::string funcName = loopName.substr(0, tagPos);

	std::string rest = loopName.substr(tagPos + mainLoopTagSize);
	unsigned loopNo = std::stoi(rest);

	return std::make_tuple(funcName, loopNo);
}

std::tuple<std::string, unsigned, unsigned> parseWholeLoopName(std::string wholeLoopName) {
#ifdef LEGACY_SEPARATOR
	const std::string mainLoopTag = "_loop-";
	const std::string depthTag = "_";
	const size_t mainLoopTagSize = 6;
	const size_t depthTagSize = 1;
#else
	const std::string mainLoopTag = GLOBAL_SEPARATOR "loop" GLOBAL_SEPARATOR;
	const std::string depthTag = GLOBAL_SEPARATOR;
	const size_t mainLoopTagSize = 6;
	const size_t depthTagSize = 1;
#endif

	size_t tagPos = wholeLoopName.find(mainLoopTag);
	std::string funcName = wholeLoopName.substr(0, tagPos);

	std::string rest = wholeLoopName.substr(tagPos + mainLoopTagSize);
	tagPos = rest.find(depthTag);
	unsigned loopNo = std::stoi(rest.substr(0, tagPos));
	unsigned loopLevel = std::stoi(rest.substr(tagPos + depthTagSize));

	return std::make_tuple(funcName, loopNo, loopLevel);
}

unsigned nextPowerOf2(unsigned x) {
	x--;
	x |= (x >> 1);
	x |= (x >> 2);
	x |= (x >> 4);
	x |= (x >> 8);
	x |= (x >> 16);
	return x++;
}

ConfigurationManager::ConfigurationManager(std::string kernelName) : kernelName(kernelName) { }

void ConfigurationManager::appendToPipeliningCfg(std::string funcName, unsigned loopNo, unsigned loopLevel) {
	pipeliningCfgTy elem;

	elem.funcName = funcName;
	elem.loopNo = loopNo;
	elem.loopLevel = loopLevel;

	pipeliningCfg.push_back(elem);
}

void ConfigurationManager::appendToUnrollingCfg(std::string funcName, unsigned loopNo, unsigned loopLevel, int lineNo, uint64_t unrollFactor) {
	unrollingCfgTy elem;

	elem.funcName = funcName;
	elem.loopNo = loopNo;
	elem.loopLevel = loopLevel;
	elem.lineNo = lineNo;
	elem.unrollFactor = unrollFactor;

	unrollingCfg.push_back(elem);
}

void ConfigurationManager::appendToPartitionCfg(unsigned type, std::string baseAddr, uint64_t size, size_t wordSize, uint64_t pFactor) {
	partitionCfgTy elem;

	elem.type = type;
	elem.baseAddr = baseAddr;
	elem.size = size;
	elem.wordSize = wordSize;
	elem.pFactor = pFactor;

	partitionCfg.push_back(elem);
	partitionCfgMap.insert(std::make_pair(baseAddr, &(partitionCfg.back())));
}

void ConfigurationManager::appendToCompletePartitionCfg(std::string baseAddr, uint64_t size) {
	partitionCfgTy elem;

	elem.type = partitionCfgTy::PARTITION_TYPE_COMPLETE;
	elem.baseAddr = baseAddr;
	elem.size = size;
	elem.wordSize = 0;
	elem.pFactor = 0;

	completePartitionCfg.push_back(elem);
	completePartitionCfgMap.insert(std::make_pair(baseAddr, &(completePartitionCfg.back())));
}

void ConfigurationManager::appendToArrayInfoCfg(std::string arrayName, uint64_t totalSize, size_t wordSize) {
	arrayInfoCfgTy elem;

	elem.totalSize = totalSize;
	elem.wordSize = wordSize;

	arrayInfoCfgMap.insert(std::make_pair(arrayName, elem));
}

void ConfigurationManager::clear() {
	pipeliningCfg.clear();
	unrollingCfg.clear();
	partitionCfg.clear();
	partitionCfgMap.clear();
	completePartitionCfg.clear();
	completePartitionCfgMap.clear();
	arrayInfoCfgMap.clear();
}

void ConfigurationManager::parseAndPopulate(std::vector<std::string> &pipelineLoopLevelVec) {
	std::ifstream configFile;

	configFile.open(args.configFileName);
	assert(configFile.is_open() && "Error opening configuration file");

	std::string line;
	std::vector<std::string> pipeliningCfgStr;
	std::vector<std::string> unrollingCfgStr;
	std::vector<std::string> partitionCfgStr;
	std::vector<std::string> completePartitionCfgStr;
	std::vector<std::string> arrayInfoCfgStr;

	pipelineLoopLevelVec.clear();

	while(!configFile.eof()) {
		// TODO: is it necessary to clean it?
		// line.clear();
		std::getline(configFile, line);

		if(!line.size())
			break;
		if('#' == line[0])
			continue;

		size_t tagPos = line.find(",");
		if(std::string::npos == tagPos)
			break;

		std::string type = line.substr(0, tagPos);

		if(!type.compare("pipeline")) {
			pipeliningCfgStr.push_back(line);
		}
		else if(!type.compare("unrolling")) {
			unrollingCfgStr.push_back(line);
		}
		else if(!type.compare("array")) {
			arrayInfoCfgStr.push_back(line);
		}
		else if(!type.compare("partition")) {
			std::string rest = line.substr(tagPos + 1);

			tagPos = line.find(",");
			if(std::string::npos == tagPos)
				break;

			std::string partitionType = rest.substr(0, tagPos);

			if(!type.compare("complete"))
				completePartitionCfgStr.push_back(line);
			else
				partitionCfgStr.push_back(line);
		}
	}

	configFile.close();

	std::map<std::string, uint64_t> wholeLoopName2CompUnrollFactorMap;

	if(pipeliningCfgStr.size()) {
		for(std::string i : pipeliningCfgStr) {
			char buff[BUFF_STR_SZ];
			unsigned loopNo, loopLevel;
			sscanf(i.c_str(), "%*[^,],%[^,],%u,%u\n", buff, &loopNo, &loopLevel);

			std::string funcName(buff);
			std::string loopName = constructLoopName(funcName, loopNo);

			appendToPipeliningCfg(funcName, loopNo, loopLevel);

			LpName2numLevelMapTy::iterator found = LpName2numLevelMap.find(loopName);
			assert(found != LpName2numLevelMap.end() && "Cannot find loop name provided in configuration file");
			unsigned numLevel = found->second;

			std::string wholeLoopName = appendDepthToLoopName(loopName, loopLevel);
			pipelineLoopLevelVec.push_back(wholeLoopName);

			assert(loopLevel <= numLevel && "Loop level is larger than number of levels");

			// TODO: This is assuming that loop pipelining is already performed at the innermost level. Is this correct?
			if(loopLevel == numLevel)
				continue;

			for(unsigned j = loopLevel + 1; j < numLevel + 1; j++) {
				std::string wholeLoopName2 = appendDepthToLoopName(loopName, j);
				wholeloopName2loopBoundMapTy::iterator found2 = wholeloopName2loopBoundMap.find(wholeLoopName2);

				assert(found2 != wholeloopName2loopBoundMap.end() && "Cannot find loop name provided in configuration file");

				uint64_t loopBound = found2->second;

#if 0
				// TODO: This is a silent error/warning. Is this correct (i.e. nothing should be performed apart from informing the user)?
				if(!loopBound)
					VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Variable loop bound found for \"" << wholeLoopName2 << "\", pipelining not supported in current version\n");
#else
				// XXX: Changed to an error instead of warning. By detecting a variable loop bound when pipelining and putting a 0
				// for unroll factor, DDDGBuilder is not able to construct the interval used for DDDG construction, leading to a 0-sized graph,
				// which in original code leads to errors regarding reading the .gz files such as dynamic_funcid.gz
				assert(loopBound && "Pipeline requested on a loop containing nested loops with variable bounds. Not supported in current version");
#endif

				wholeLoopName2CompUnrollFactorMap.insert(std::make_pair(wholeLoopName2, loopBound));
			}	
		}
	}

	if(unrollingCfgStr.size()) {
		std::vector<std::string> unrollWholeLoopNameStr;

		// TODO: IS lineNo REALLY NECESSARY?
		for(std::string i : unrollingCfgStr) {
			char buff[BUFF_STR_SZ];
			unsigned loopNo, loopLevel;
			int lineNo;
			uint64_t unrollFactor;
			sscanf(i.c_str(), "%*[^,],%[^,],%u,%u,%d,%lu\n", buff, &loopNo, &loopLevel, &lineNo, &unrollFactor);

			std::string funcName(buff);
			std::string wholeLoopName = constructLoopName(funcName, loopNo, loopLevel);
			unrollWholeLoopNameStr.push_back(wholeLoopName);

			std::map<std::string, uint64_t>::iterator found = wholeLoopName2CompUnrollFactorMap.find(wholeLoopName);
			if(wholeLoopName2CompUnrollFactorMap.end() == found) {
				appendToUnrollingCfg(funcName, loopNo, loopLevel, lineNo, unrollFactor);
			}
			else {
				uint64_t staticBound = found->second;
				if(staticBound)
					appendToUnrollingCfg(funcName, loopNo, loopLevel, lineNo, staticBound);
				else
					appendToUnrollingCfg(funcName, loopNo, loopLevel, lineNo, unrollFactor);
			}
		}

		// If loop pipelining in a loop that have nested non-unrolled loops, these loops must be fully unrolled. Therefore such
		// configurations are automatically added
		for(auto &it : wholeLoopName2CompUnrollFactorMap) {
			std::string wholeLoopName = it.first;
			uint64_t loopBound = it.second;

			std::vector<std::string>::iterator found = std::find(unrollWholeLoopNameStr.begin(), unrollWholeLoopNameStr.end(), wholeLoopName);
			if(unrollWholeLoopNameStr.end() == found) {
				std::tuple<std::string, unsigned, unsigned> parsed = parseWholeLoopName(wholeLoopName);
				appendToUnrollingCfg(std::get<0>(parsed), std::get<1>(parsed), std::get<2>(parsed), -1, loopBound);
			}
		}
	}

	if(arrayInfoCfgStr.size()) {
		for(std::string i : arrayInfoCfgStr) {
			char buff[BUFF_STR_SZ];
			uint64_t totalSize;
			size_t wordSize;
			sscanf(i.c_str(), "%*[^,],%[^,],%lu,%zu\n", buff, &totalSize, &wordSize);

			std::string arrayName(buff);
			appendToArrayInfoCfg(arrayName, totalSize, wordSize);
		}
	}
	else {
		assert(false && "Please provide array information for this kernel");
	}

	if(partitionCfgStr.size()) {
		for(std::string i : partitionCfgStr) {
			char buff[BUFF_STR_SZ];
			char buff2[BUFF_STR_SZ];
			uint64_t size;
			size_t wordSize;
			uint64_t pFactor;
			sscanf(i.c_str(), "%*[^,],%[^,],%[^,],%lu,%zu,%lu\n", buff, buff2, &size, &wordSize, &pFactor);

			std::string typeStr(buff);
			unsigned type = (typeStr.compare("cyclic"))? partitionCfgTy::PARTITION_TYPE_BLOCK : partitionCfgTy::PARTITION_TYPE_CYCLIC;
			std::string baseAddr(buff2);
			appendToPartitionCfg(type, baseAddr, size, wordSize, pFactor);
		}
	}

	if(completePartitionCfgStr.size()) {
		for(std::string i : completePartitionCfgStr) {
			char buff[BUFF_STR_SZ];
			uint64_t size;
			sscanf(i.c_str(), "%*[^,],%*[^,],%[^,],%lu\n", buff, &size);

			std::string baseAddr(buff);
			appendToCompletePartitionCfg(baseAddr, size);
		}
	}

	// TODO: Y dis?
	if(!pipeliningCfgStr.size()) {
		for(partitionCfgTy i : partitionCfg) {
			if(i.pFactor > 1) {
				args.fILL = true;
				break;
			}
		}
	}
}

void ConfigurationManager::parseToFiles() {
	std::string pipeliningFileName = args.outWorkDir + kernelName + "_pipelining.cfg";
	std::string unrollingFileName = args.outWorkDir + kernelName + "_unrolling.cfg";
	std::string arrayInfoFileName = args.outWorkDir + kernelName + "_arrayinfo.cfg";
	std::string partitionFileName = args.outWorkDir + kernelName + "_partition.cfg";
	std::string completePartitionFileName = args.outWorkDir + kernelName + "_completepartition.cfg";
	std::ofstream outFile;

	outFile.open(pipeliningFileName);
	for(auto &it : pipeliningCfg)
		outFile << "pipeline," << it.funcName << "," << std::to_string(it.loopNo) << "," << std::to_string(it.loopLevel) << "\n";
	outFile.close();

	outFile.open(unrollingFileName);
	for(auto &it : unrollingCfg) {
		outFile << "unrolling," << it.funcName << "," <<
			std::to_string(it.loopNo) << "," << std::to_string(it.loopLevel) << "," <<
			std::to_string(it.lineNo) << "," << std::to_string(it.unrollFactor) << "\n";
	}
	outFile.close();

	outFile.open(partitionFileName);
	for(auto &it : partitionCfg) {
		std::string type = (partitionCfgTy::PARTITION_TYPE_BLOCK == it.type)? "block" : "cyclic";
		outFile << "partition," << type << "," << it.baseAddr << "," << std::to_string(it.size) << "," << std::to_string(it.wordSize) << "," << std::to_string(it.pFactor) << "\n";
	}
	outFile.close();

	outFile.open(completePartitionFileName);
	for(auto &it : completePartitionCfg)
		outFile << "partition,complete," << it.baseAddr << "," << std::to_string(it.size) << "\n";
	outFile.close();

	outFile.open(arrayInfoFileName);
	for(auto &it : arrayInfoCfgMap)
		outFile << "array," << it.first << "," << std::to_string(it.second.totalSize) << "," << std::to_string(it.second.wordSize) << "\n";
	outFile.close();
}

LimitedQueue::LimitedQueue(size_t size) : size(size) { }

void LimitedQueue::push(unsigned elem) {
	history.push(elem);

	if(history.size() > size)
		history.pop();
}

unsigned LimitedQueue::front() {
	return (history.size() < size)? 0 : history.front();
}

unsigned LimitedQueue::back() {
	return history.back();
}

size_t LimitedQueue::getSize() {
	return size;
}
