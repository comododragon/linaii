#include "profile_h/auxiliary.h"

#include "llvm/IR/Verifier.h"

using namespace llvm;

const std::string functionNameMapperMDKindName = "lia.functionnamemapper";
const std::string loopNumberMDKindName = "lia.kernelloopnumber";
const std::string assignBasicBlockIDMDKindName = "lia.kernelbbid";
const std::string assignLoadStoreIDMDKindName = "lia.loadstoreid"; 
const std::string extractLoopInfoMDKindName = "lia.kernelloopinfo";

bool isFunctionOfInterest(std::string key, bool isMangled) {
	std::vector<std::string>::iterator it;
	if(isMangled) {
		std::map<std::string, std::string>::iterator found = mangledName2FunctionNameMap.find(key);
		it = std::find(args.kernelNames.begin(), args.kernelNames.end(), (mangledName2FunctionNameMap.end() == found)? key : found->second);
	}
	else {
		it = std::find(args.kernelNames.begin(), args.kernelNames.end(), key);
	}
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
	return x + 1;
}

uint64_t nextPowerOf2(uint64_t x) {
	x--;
	x |= (x >> 1);
	x |= (x >> 2);
	x |= (x >> 4);
	x |= (x >> 8);
	x |= (x >> 16);
	x |= (x >> 32);
	return x + 1;
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
	partitionCfgMap.insert(std::make_pair(baseAddr, elem));
}

void ConfigurationManager::appendToCompletePartitionCfg(std::string baseAddr, uint64_t size) {
	partitionCfgTy elem;

	elem.type = partitionCfgTy::PARTITION_TYPE_COMPLETE;
	elem.baseAddr = baseAddr;
	elem.size = size;
	elem.wordSize = 0;
	elem.pFactor = 0;

	completePartitionCfg.push_back(elem);
	completePartitionCfgMap.insert(std::make_pair(baseAddr, elem));
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

			tagPos = rest.find(",");
			if(std::string::npos == tagPos)
				break;

			std::string partitionType = rest.substr(0, tagPos);

			if(!partitionType.compare("complete"))
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
			std::map<std::string, std::string>::iterator mangledFound = functionName2MangledNameMap.find(funcName);
			std::string mangledFuncName = (functionName2MangledNameMap.end() == mangledFound)? funcName : mangledFound->second;
			std::string loopName = constructLoopName(mangledFuncName, loopNo);

			appendToPipeliningCfg(mangledFuncName, loopNo, loopLevel);

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
			std::map<std::string, std::string>::iterator mangledFound = functionName2MangledNameMap.find(funcName);
			std::string mangledFuncName = (functionName2MangledNameMap.end() == mangledFound)? funcName : mangledFound->second;
			std::string wholeLoopName = constructLoopName(mangledFuncName, loopNo, loopLevel);
			unrollWholeLoopNameStr.push_back(wholeLoopName);

			std::map<std::string, uint64_t>::iterator found = wholeLoopName2CompUnrollFactorMap.find(wholeLoopName);
			if(wholeLoopName2CompUnrollFactorMap.end() == found) {
				appendToUnrollingCfg(mangledFuncName, loopNo, loopLevel, lineNo, unrollFactor);
			}
			else {
				uint64_t staticBound = found->second;
				if(staticBound)
					appendToUnrollingCfg(mangledFuncName, loopNo, loopLevel, lineNo, staticBound);
				else
					appendToUnrollingCfg(mangledFuncName, loopNo, loopLevel, lineNo, unrollFactor);
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

void Pack::addDescriptor(std::string name, unsigned mergeMode, unsigned type) {
	structure.push_back(std::make_tuple(name, mergeMode, type));
}

template<> void Pack::addElement<uint64_t>(std::string name, uint64_t value) {
	std::unordered_map<std::string, std::vector<uint64_t>>::iterator found = unsignedContent.find(name);

	if(unsignedContent.end() == found) {
		std::vector<uint64_t> vec;
		vec.push_back(value);
		unsignedContent[name] = vec;
	}
	else {
		found->second.push_back(value);
	}
}

template<> void Pack::addElement<int64_t>(std::string name, int64_t value) {
	std::unordered_map<std::string, std::vector<int64_t>>::iterator found = signedContent.find(name);

	if(signedContent.end() == found) {
		std::vector<int64_t> vec;
		vec.push_back(value);
		signedContent[name] = vec;
	}
	else {
		found->second.push_back(value);
	}
}

template<> void Pack::addElement<float>(std::string name, float value) {
	std::unordered_map<std::string, std::vector<float>>::iterator found = floatContent.find(name);

	if(floatContent.end() == found) {
		std::vector<float> vec;
		vec.push_back(value);
		floatContent[name] = vec;
	}
	else {
		found->second.push_back(value);
	}
}

template<> void Pack::addElement<std::string>(std::string name, std::string value) {
	std::unordered_map<std::string, std::vector<std::string>>::iterator found = stringContent.find(name);

	if(stringContent.end() == found) {
		std::vector<std::string> vec;
		vec.push_back(value);
		stringContent[name] = vec;
	}
	else {
		found->second.push_back(value);
	}
}

std::vector<std::tuple<std::string, unsigned, unsigned>> Pack::getStructure() {
	return structure;
}

template<> std::vector<uint64_t> Pack::getElements<uint64_t>(std::string name) {
	return unsignedContent[name];
}

template<> std::vector<int64_t> Pack::getElements<int64_t>(std::string name) {
	return signedContent[name];
}

template<> std::vector<float> Pack::getElements<float>(std::string name) {
	return floatContent[name];
}

template<> std::vector<std::string> Pack::getElements<std::string>(std::string name) {
	return stringContent[name];
}

template<> std::string Pack::mergeElements<uint64_t>(std::string name) {
	uint64_t aggrElem;
	std::string aggrString;
	bool isEqual = true;
	bool firstElem = true;
	std::set<uint64_t> set;

	std::tuple<std::string, unsigned, unsigned> elem;
	bool found = false;
	for(auto &it : structure) {
		if(!(std::get<0>(it).compare(name))) {
			elem = it;
			found = true;
		}
	}
	assert(found && "Element was not found");

	switch(std::get<1>(elem)) {
		case MERGE_MAX:
			aggrElem = std::numeric_limits<uint64_t>::min();

			for(auto &it : unsignedContent[name]) {
				if(it > aggrElem)
					aggrElem = it;
			}

			return std::to_string(aggrElem);
		case MERGE_MIN:
			aggrElem = std::numeric_limits<uint64_t>::max();

			for(auto &it : unsignedContent[name]) {
				if(it < aggrElem)
					aggrElem = it;
			}

			return std::to_string(aggrElem);
		case MERGE_SUM:
			aggrElem = 0;

			for(auto &it : unsignedContent[name])
				aggrElem += it;

			return std::to_string(aggrElem);
		case MERGE_EQUAL:
			aggrElem = unsignedContent[name][0];

			for(auto &it : unsignedContent[name]) {
				if(aggrElem != it)
					isEqual = false;
			}

			return isEqual? "true" : "false";
		case MERGE_SET:
			for(auto &it : unsignedContent[name])
				set.insert(it);

			for(auto &it : set) {
				if(firstElem) {
					aggrString = std::to_string(it);
					firstElem = false;
				}
				else {
					aggrString += std::to_string(it);
				}
			}

			return aggrString;
		default:
			assert(false && "Cannot merge, aggregation type is MERGE_NONE");
	}
}

template<> std::string Pack::mergeElements<int64_t>(std::string name) {
	int64_t aggrElem;
	std::string aggrString;
	bool isEqual = true;
	bool firstElem = true;
	std::set<int64_t> set;

	std::tuple<std::string, unsigned, unsigned> elem;
	bool found = false;
	for(auto &it : structure) {
		if(!(std::get<0>(it).compare(name))) {
			elem = it;
			found = true;
		}
	}
	assert(found && "Element was not found");

	switch(std::get<1>(elem)) {
		case MERGE_MAX:
			aggrElem = std::numeric_limits<int64_t>::min();

			for(auto &it : signedContent[name]) {
				if(it > aggrElem)
					aggrElem = it;
			}

			return std::to_string(aggrElem);
		case MERGE_MIN:
			aggrElem = std::numeric_limits<int64_t>::max();

			for(auto &it : signedContent[name]) {
				if(it < aggrElem)
					aggrElem = it;
			}

			return std::to_string(aggrElem);
		case MERGE_SUM:
			aggrElem = 0;

			for(auto &it : signedContent[name])
				aggrElem += it;

			return std::to_string(aggrElem);
		case MERGE_EQUAL:
			aggrElem = signedContent[name][0];

			for(auto &it : signedContent[name]) {
				if(aggrElem != it)
					isEqual = false;
			}

			return isEqual? "true" : "false";
		case MERGE_SET:
			for(auto &it : signedContent[name])
				set.insert(it);

			for(auto &it : set) {
				if(firstElem) {
					aggrString = std::to_string(it);
					firstElem = false;
				}
				else {
					aggrString += std::to_string(it);
				}
			}

			return aggrString;
		default:
			assert(false && "Cannot merge, aggregation type is MERGE_NONE");
	}
}

template<> std::string Pack::mergeElements<float>(std::string name) {
	float aggrElem;
	std::string aggrString;
	bool isEqual = true;

	std::tuple<std::string, unsigned, unsigned> elem;
	bool found = false;
	for(auto &it : structure) {
		if(!(std::get<0>(it).compare(name))) {
			elem = it;
			found = true;
		}
	}
	assert(found && "Element was not found");

	switch(std::get<1>(elem)) {
		case MERGE_MAX:
			aggrElem = std::numeric_limits<float>::min();

			for(auto &it : floatContent[name]) {
				if(it > aggrElem)
					aggrElem = it;
			}

			return std::to_string(aggrElem);
		case MERGE_MIN:
			aggrElem = std::numeric_limits<float>::max();

			for(auto &it : floatContent[name]) {
				if(it < aggrElem)
					aggrElem = it;
			}

			return std::to_string(aggrElem);
		case MERGE_SUM:
			aggrElem = 0;

			for(auto &it : floatContent[name])
				aggrElem += it;

			return std::to_string(aggrElem);
		case MERGE_EQUAL:
			aggrElem = floatContent[name][0];

			for(auto &it : floatContent[name]) {
				if(fabs(aggrElem - it) > 0.0001)
					isEqual = false;
			}

			return isEqual? "true" : "false";
		case MERGE_SET:
			assert(false && "Cannot merge, invalid aggregation type for float (MERGE_SET)");
		default:
			assert(false && "Cannot merge, aggregation type is MERGE_NONE");
	}
}

template<> std::string Pack::mergeElements<std::string>(std::string name) {
	std::string aggrString;
	bool isEqual = true;
	bool firstElem = true;
	std::set<std::string> set;

	std::tuple<std::string, unsigned, unsigned> elem;
	bool found = false;
	for(auto &it : structure) {
		if(!(std::get<0>(it).compare(name))) {
			elem = it;
			found = true;
		}
	}
	assert(found && "Element was not found");

	switch(std::get<1>(elem)) {
		case MERGE_MAX:
			assert(false && "Cannot merge, invalid aggregation type for string (MERGE_MAX)");
		case MERGE_MIN:
			assert(false && "Cannot merge, invalid aggregation type for string (MERGE_MIN)");
		case MERGE_SUM:
			assert(false && "Cannot merge, invalid aggregation type for string (MERGE_SUM");
		case MERGE_EQUAL:
			aggrString = stringContent[name][0];

			for(auto &it : stringContent[name]) {
				if(aggrString.compare(it))
					isEqual = false;
			}

			return isEqual? "true" : "false";
		case MERGE_SET:
			for(auto &it : stringContent[name])
				set.insert(it);

			for(auto &it : set) {
				if(firstElem) {
					aggrString = it;
					firstElem = false;
				}
				else {
					aggrString += it;
				}
			}

			return aggrString;
		default:
			assert(false && "Cannot merge, merge type is MERGE_NONE");
	}
}

bool Pack::hasSameStructure(Pack &P) {
	std::vector<std::tuple<std::string, unsigned, unsigned>> otherStructure = P.getStructure();

	if(structure.size() != otherStructure.size())
		return false;

	for(unsigned i = 0; i < structure.size(); i++) {
		if(std::get<0>(structure[i]).compare(std::get<0>(otherStructure[i])))
			return false;
		if(std::get<1>(structure[i]) != std::get<1>(otherStructure[i]))
			return false;
		if(std::get<2>(structure[i]) != std::get<2>(otherStructure[i]))
			return false;
	}

	return true;
}

void Pack::merge(Pack &P) {
	if(!(structure.size())) {
		for(auto &it : P.getStructure())
			structure.push_back(it);
	}

	if(!(P.getStructure().size()))
		return;

	assert(hasSameStructure(P) && "Attempt to merge a pack with different structure");

	std::vector<uint64_t> otherUnsignedStuff;
	std::vector<int64_t> otherSignedStuff;
	std::vector<float> otherFloatStuff;
	std::vector<std::string> otherStringStuff;

	for(auto &it : structure) {
		switch(std::get<2>(it)) {
			case TYPE_UNSIGNED:
				otherUnsignedStuff = P.getElements<uint64_t>(std::get<0>(it));
				for(auto &it2 : otherUnsignedStuff)
					addElement<uint64_t>(std::get<0>(it), it2);
				break;
			case TYPE_SIGNED:
				otherSignedStuff = P.getElements<int64_t>(std::get<0>(it));
				for(auto &it2 : otherSignedStuff)
					addElement<int64_t>(std::get<0>(it), it2);
				break;
			case TYPE_FLOAT:
				otherFloatStuff = P.getElements<float>(std::get<0>(it));
				for(auto &it2 : otherFloatStuff)
					addElement<float>(std::get<0>(it), it2);
				break;
			case TYPE_STRING:
				otherStringStuff = P.getElements<std::string>(std::get<0>(it));
				for(auto &it2 : otherStringStuff)
					addElement<std::string>(std::get<0>(it), it2);
				break;
			default:
				assert(false && "Invalid type of element");
				break;
		}
	}
}

void Pack::clear() {
	std::vector<std::tuple<std::string, unsigned, unsigned>>().swap(structure);
	// TODO: I don't know if this does not leak because of the inner vectors
	std::unordered_map<std::string, std::vector<uint64_t>>().swap(unsignedContent);
	std::unordered_map<std::string, std::vector<int64_t>>().swap(signedContent);
	std::unordered_map<std::string, std::vector<float>>().swap(floatContent);
	std::unordered_map<std::string, std::vector<std::string>>().swap(stringContent);
}
