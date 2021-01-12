#include "profile_h/auxiliary.h"

#include "llvm/IR/Verifier.h"

#include "profile_h/opcodes.h"

using namespace llvm;

#ifdef DBG_FILE
std::ofstream debugFile;
#endif

const std::string functionNameMapperMDKindName = "lia.functionnamemapper";
const std::string loopNumberMDKindName = "lia.kernelloopnumber";
const std::string assignBasicBlockIDMDKindName = "lia.kernelbbid";
const std::string assignLoadStoreIDMDKindName = "lia.loadstoreid"; 
const std::string extractLoopInfoMDKindName = "lia.kernelloopinfo";

bool isFunctionOfInterest(std::string key, bool isMangled) {
	std::vector<std::string>::iterator it = std::find(args.kernelNames.begin(), args.kernelNames.end(), isMangled? demangleFunctionName(key) : key);
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

std::string mangleFunctionName(std::string functionName) {
	std::map<std::string, std::string>::iterator mangledFound = functionName2MangledNameMap.find(functionName);
	return (functionName2MangledNameMap.end() == mangledFound)? functionName : mangledFound->second;
}

std::string demangleFunctionName(std::string mangledName) {
	std::map<std::string, std::string>::iterator functionFound = mangledName2FunctionNameMap.find(mangledName);
	return (mangledName2FunctionNameMap.end() == functionFound)? mangledName : functionFound->second;
}

std::string mangleArrayName(std::string arrayName) {
	std::map<std::string, std::string>::iterator mangledFound = arrayName2MangledNameMap.find(arrayName);
	return (arrayName2MangledNameMap.end() == mangledFound)? arrayName : mangledFound->second;
}

std::string demangleArrayName(std::string mangledName) {
	std::map<std::string, std::string>::iterator arrayFound = mangledName2ArrayNameMap.find(mangledName);
	return (mangledName2ArrayNameMap.end() == arrayFound)? mangledName : arrayFound->second;
}

std::string generateInstID(unsigned opcode, std::vector<std::string> instIDList) {
	static uint64_t idCtr = 0;

	// XXX: I don't think this is a performance bottleneck, but if it is, then we should re-think this logic to avoid name collision
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

uint64_t logNextPowerOf2(uint64_t x) {
	unsigned onePos = 0;
	bool foundOne = false;
	bool foundMoreOne = false;

	for(unsigned i = 63; i + 1; i--) {
		if((x >> i) & 0x1) {
			if(foundOne) {
				foundMoreOne = true;
			}
			else {
				onePos = i;
				foundOne = true;
			}
		}
	}

	return foundMoreOne? onePos + 1 : onePos;
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

void ConfigurationManager::appendToArrayInfoCfg(std::string arrayName, uint64_t totalSize, size_t wordSize, unsigned type, unsigned scope) {
	arrayInfoCfgTy elem;

	elem.totalSize = totalSize;
	elem.wordSize = wordSize;
	elem.type = type;
	elem.scope = scope;

	arrayInfoCfgMap.insert(std::make_pair(arrayName, elem));
}

template <>
void ConfigurationManager::appendToGlobalCfg<std::string>(unsigned name, std::string value) {
	assert((globalCfgTy::GLOBAL_TYPE_STRING == globalCfgTy::globalCfgTypeMap.at(name)) && "Wrong type (string) for provided global parameter");

	globalCfgTy elem;
	elem.asString = value;
	globalCfgMap.insert(std::make_pair(name, elem));
}

template <>
void ConfigurationManager::appendToGlobalCfg<int>(unsigned name, int value) {
	assert((globalCfgTy::GLOBAL_TYPE_INT == globalCfgTy::globalCfgTypeMap.at(name)) && "Wrong type (int) for provided global parameter");

	globalCfgTy elem;
	elem.asInt = value;
	globalCfgMap.insert(std::make_pair(name, elem));
}

template <>
void ConfigurationManager::appendToGlobalCfg<float>(unsigned name, float value) {
	assert((globalCfgTy::GLOBAL_TYPE_FLOAT == globalCfgTy::globalCfgTypeMap.at(name)) && "Wrong type (float) for provided global parameter");

	globalCfgTy elem;
	elem.asFloat = value;
	globalCfgMap.insert(std::make_pair(name, elem));
}

template <>
void ConfigurationManager::appendToGlobalCfg<bool>(unsigned name, bool value) {
	assert((globalCfgTy::GLOBAL_TYPE_BOOL == globalCfgTy::globalCfgTypeMap.at(name)) && "Wrong type (bool) for provided global parameter");

	globalCfgTy elem;
	elem.asBool = value;
	globalCfgMap.insert(std::make_pair(name, elem));
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
	std::vector<std::string> globalCfgStr;

	pipelineLoopLevelVec.clear();

	while(!configFile.eof()) {
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
		else if(!type.compare("global")) {
			globalCfgStr.push_back(line);
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
			std::string mangledFuncName = mangleFunctionName(funcName);
			std::string loopName = constructLoopName(mangledFuncName, loopNo);

			appendToPipeliningCfg(mangledFuncName, loopNo, loopLevel);

			LpName2numLevelMapTy::iterator found = LpName2numLevelMap.find(loopName);
			assert(found != LpName2numLevelMap.end() && "Cannot find loop name provided in configuration file");
			unsigned numLevel = found->second;

			std::string wholeLoopName = appendDepthToLoopName(loopName, loopLevel);
			pipelineLoopLevelVec.push_back(wholeLoopName);

			assert(loopLevel <= numLevel && "Loop level is larger than number of levels");

			if(loopLevel == numLevel)
				continue;

			for(unsigned j = loopLevel + 1; j < numLevel + 1; j++) {
				std::string wholeLoopName2 = appendDepthToLoopName(loopName, j);
				wholeloopName2loopBoundMapTy::iterator found2 = wholeloopName2loopBoundMap.find(wholeLoopName2);

				assert(found2 != wholeloopName2loopBoundMap.end() && "Cannot find loop name provided in configuration file");

				uint64_t loopBound = found2->second;

				// XXX: Changed to an error instead of warning. By detecting a variable loop bound when pipelining and putting a 0
				// for unroll factor, DDDGBuilder is not able to construct the interval used for DDDG construction, leading to a 0-sized graph,
				// which in original code leads to errors regarding reading the .gz files such as dynamic_funcid.gz
				assert(loopBound && "Pipeline requested on a loop containing nested loops with variable bounds. Not supported in current version");

				wholeLoopName2CompUnrollFactorMap.insert(std::make_pair(wholeLoopName2, loopBound));
			}	
		}
	}

	std::vector<std::string> unrollWholeLoopNameStr;

	if(unrollingCfgStr.size()) {
		for(std::string i : unrollingCfgStr) {
			char buff[BUFF_STR_SZ];
			unsigned loopNo, loopLevel;
			int lineNo;
			uint64_t unrollFactor;
			sscanf(i.c_str(), "%*[^,],%[^,],%u,%u,%d,%lu\n", buff, &loopNo, &loopLevel, &lineNo, &unrollFactor);

			std::string funcName(buff);
			std::string mangledFuncName = mangleFunctionName(funcName);
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

	if(arrayInfoCfgStr.size()) {
		for(std::string i : arrayInfoCfgStr) {
			char buff[BUFF_STR_SZ];
			uint64_t totalSize;
			size_t wordSize;
			char buff2[BUFF_STR_SZ];
			char buff3[BUFF_STR_SZ];

			int retVal = sscanf(i.c_str(), "%*[^,],%[^,],%lu,%zu,%[^,],%[^,]\n", buff, &totalSize, &wordSize, buff2, buff3);

			std::string arrayName(buff);
			unsigned type = arrayInfoCfgTy::ARRAY_TYPE_ONCHIP;
			unsigned scope = arrayInfoCfgTy::ARRAY_SCOPE_ARG;
			if(retVal > 3) {
				std::string typeStr(buff2);

				if(!(args.fNoMMA) && typeStr.compare("onchip"))
					type = arrayInfoCfgTy::ARRAY_TYPE_OFFCHIP;
			}
			if(retVal > 4) {
				assert(arrayInfoCfgTy::ARRAY_TYPE_ONCHIP == type && "Scope argument should not be present when array is offchip");

				std::string scopeStr(buff3);

				if("rovar" == scopeStr)
					scope = arrayInfoCfgTy::ARRAY_SCOPE_ROVAR;
				else if("rwvar" == scopeStr)
					scope = arrayInfoCfgTy::ARRAY_SCOPE_RWVAR;
				else if("nocount" == scopeStr)
					scope = arrayInfoCfgTy::ARRAY_SCOPE_NOCOUNT;
			}
			appendToArrayInfoCfg(mangleArrayName(arrayName), totalSize, wordSize, type, scope);
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
			appendToPartitionCfg(type, mangleArrayName(baseAddr), size, wordSize, pFactor);
		}
	}

	if(completePartitionCfgStr.size()) {
		for(std::string i : completePartitionCfgStr) {
			char buff[BUFF_STR_SZ];
			uint64_t size;
			sscanf(i.c_str(), "%*[^,],%*[^,],%[^,],%lu\n", buff, &size);

			std::string baseAddr(buff);
			appendToCompletePartitionCfg(mangleArrayName(baseAddr), size);
		}
	}

	if(globalCfgStr.size()) {
		for(std::string i : globalCfgStr) {
			char buff[BUFF_STR_SZ];
			char buff2[BUFF_STR_SZ];
			sscanf(i.c_str(), "%*[^,],%[^,],%[^,]\n", buff, buff2);

			std::string cfgName(buff);
			std::unordered_map<std::string, unsigned>::const_iterator found = globalCfgTy::globalCfgRenamerMap.find(cfgName);
			assert(found != globalCfgTy::globalCfgRenamerMap.end() && "Invalid global parameter supplied");
			unsigned cfgRename = found->second;

			if(globalCfgTy::GLOBAL_TYPE_STRING == globalCfgTy::globalCfgTypeMap.at(cfgRename)) {
				std::string value(buff2);
				appendToGlobalCfg<std::string>(cfgRename, value);
			}
			else if(globalCfgTy::GLOBAL_TYPE_INT == globalCfgTy::globalCfgTypeMap.at(cfgRename)) {
				int value;
				sscanf(buff2, "%d", &value);
				appendToGlobalCfg<int>(cfgRename, value);
			}
			else if(globalCfgTy::GLOBAL_TYPE_FLOAT == globalCfgTy::globalCfgTypeMap.at(cfgRename)) {
				float value;
				sscanf(buff2, "%f", &value);
				appendToGlobalCfg<float>(cfgRename, value);
			}
			else if(globalCfgTy::GLOBAL_TYPE_BOOL == globalCfgTy::globalCfgTypeMap.at(cfgRename)) {
				int value;
				sscanf(buff2, "%d", &value);
				appendToGlobalCfg<bool>(cfgRename, (bool) value);
			}
			else {
				assert(false && "Global parameter has invalid type in its definition");
			}
		}
	}

	// Activate load latency increase if pipelining and partitioning was enabled
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
	std::string globalFileName = args.outWorkDir + kernelName + "_global.cfg";
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
	for(auto &it : arrayInfoCfgMap) {
		std::string type = (arrayInfoCfgTy::ARRAY_TYPE_ONCHIP == it.second.type)? "onchip" : "offchip";
		std::string scope;
		if(arrayInfoCfgTy::ARRAY_SCOPE_ROVAR == it.second.scope)
			scope = "rovar";
		else if(arrayInfoCfgTy::ARRAY_SCOPE_RWVAR == it.second.scope)
			scope = "rwvar";
		else if(arrayInfoCfgTy::ARRAY_SCOPE_NOCOUNT == it.second.scope)
			scope = "nocount";
		else
			scope = "arg";

		outFile << "array," << it.first << "," << std::to_string(it.second.totalSize) << "," << std::to_string(it.second.wordSize) << "," << type << "," << scope << "\n";
	}
	outFile.close();

	outFile.open(globalFileName);
	for(auto &it : globalCfgMap) {
		unsigned inName = it.first;
		std::string humanReadableName = "unknown";
		for(auto &it2 : globalCfgTy::globalCfgRenamerMap) {
			if(it2.second == inName)
				humanReadableName = it2.first;
		}
		assert(humanReadableName != "unknown" && "Unknown global parameter found");

		if(globalCfgTy::GLOBAL_TYPE_STRING == globalCfgTy::globalCfgTypeMap.at(inName))
			outFile << "global," << humanReadableName << "," << it.second.asString << "\n";
		else if(globalCfgTy::GLOBAL_TYPE_INT == globalCfgTy::globalCfgTypeMap.at(inName))
			outFile << "global," << humanReadableName << "," << std::to_string(it.second.asInt) << "\n";
		else if(globalCfgTy::GLOBAL_TYPE_FLOAT == globalCfgTy::globalCfgTypeMap.at(inName))
			outFile << "global," << humanReadableName << "," << std::to_string(it.second.asFloat) << "\n";
		else if(globalCfgTy::GLOBAL_TYPE_BOOL == globalCfgTy::globalCfgTypeMap.at(inName))
			outFile << "global," << humanReadableName << "," << std::to_string(it.second.asBool) << "\n";
		else
			assert(false && "Global parameter with unknown type found");
	}
	outFile.close();
}

template <>
std::string ConfigurationManager::getGlobalCfg<std::string>(unsigned name) {
	assert((globalCfgTy::GLOBAL_TYPE_STRING == globalCfgTy::globalCfgTypeMap.at(name)) && "Requested global parameter is not of type string");

	globalCfgMapTy::const_iterator found = globalCfgMap.find(name);
	return (found != globalCfgMap.end())? found->second.asString : "";
}

template <>
int ConfigurationManager::getGlobalCfg<int>(unsigned name) {
	assert((globalCfgTy::GLOBAL_TYPE_INT == globalCfgTy::globalCfgTypeMap.at(name)) && "Requested global parameter is not of type int");

	globalCfgMapTy::const_iterator found = globalCfgMap.find(name);
	return (found != globalCfgMap.end())? found->second.asInt : 0;
}

template <>
float ConfigurationManager::getGlobalCfg<float>(unsigned name) {
	assert((globalCfgTy::GLOBAL_TYPE_FLOAT == globalCfgTy::globalCfgTypeMap.at(name)) && "Requested global parameter is not of type float");

	globalCfgMapTy::const_iterator found = globalCfgMap.find(name);
	return (found != globalCfgMap.end())? found->second.asFloat : 0.0;
}

template <>
bool ConfigurationManager::getGlobalCfg<bool>(unsigned name) {
	assert((globalCfgTy::GLOBAL_TYPE_BOOL == globalCfgTy::globalCfgTypeMap.at(name)) && "Requested global parameter is not of type bool");

	globalCfgMapTy::const_iterator found = globalCfgMap.find(name);
	return (found != globalCfgMap.end())? found->second.asBool : false;
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

template<> void Pack::addElement<Pack::resourceNodeTy>(std::string name, resourceNodeTy value) {
	std::unordered_map<std::string, std::vector<resourceNodeTy>>::iterator found = resourceTreeContent.find(name);

	if(resourceTreeContent.end() == found) {
		std::vector<resourceNodeTy> vec;
		vec.push_back(value);
		resourceTreeContent[name] = vec;
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

template<> std::vector<Pack::resourceNodeTy> Pack::getElements<Pack::resourceNodeTy>(std::string name) {
	return resourceTreeContent[name];
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
		case MERGE_MULSUM:
			aggrElem = 0;

			for(unsigned i = 0; i < unsignedContent[name].size(); i += 2)
				aggrElem += unsignedContent[name][i] * unsignedContent[name][i + 1];

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
		case MERGE_RESOURCETREEMAX:
			assert(false && "Cannot merge, invalid aggregation type for unsigned (MERGE_RESOURCETREEMAX)");
		case MERGE_RESOURCELISTMAX:
			assert(false && "Cannot merge, invalid aggregation type for unsigned (MERGE_RESOURCELISTMAX)");
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
		case MERGE_MULSUM:
			aggrElem = 0;

			for(unsigned i = 0; i < signedContent[name].size(); i += 2)
				aggrElem += signedContent[name][i] * signedContent[name][i + 1];

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
		case MERGE_RESOURCETREEMAX:
			assert(false && "Cannot merge, invalid aggregation type for integer (MERGE_RESOURCETREEMAX)");
		case MERGE_RESOURCELISTMAX:
			assert(false && "Cannot merge, invalid aggregation type for unsigned (MERGE_RESOURCELISTMAX)");
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
		case MERGE_MULSUM:
			aggrElem = 0;

			for(unsigned i = 0; i < floatContent[name].size(); i += 2)
				aggrElem += floatContent[name][i] * floatContent[name][i + 1];

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
		case MERGE_RESOURCETREEMAX:
			assert(false && "Cannot merge, invalid aggregation type for float (MERGE_RESOURCETREEMAX)");
		case MERGE_RESOURCELISTMAX:
			assert(false && "Cannot merge, invalid aggregation type for unsigned (MERGE_RESOURCELISTMAX)");
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
			assert(false && "Cannot merge, invalid aggregation type for string (MERGE_SUM)");
		case MERGE_MULSUM:
			assert(false && "Cannot merge, invalid aggregation type for string (MERGE_MULSUM)");
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
		case MERGE_RESOURCETREEMAX:
			assert(false && "Cannot merge, invalid aggregation type for string (MERGE_RESOURCETREEMAX)");
		case MERGE_RESOURCELISTMAX:
			assert(false && "Cannot merge, invalid aggregation type for unsigned (MERGE_RESOURCELISTMAX)");
		default:
			assert(false && "Cannot merge, merge type is MERGE_NONE");
	}
}

template<> std::string Pack::mergeElements<Pack::resourceNodeTy>(std::string name) {
	unsigned aggrElemFU;
	unsigned aggrElemDSP;
	unsigned aggrElemFF;
	unsigned aggrElemLUT;
	std::vector<resourceNodeTy>::iterator it;
	std::vector<resourceNodeTy>::iterator itEnd;
	std::string stringFU;
	std::string stringDSP;
	std::string stringFF;
	std::string stringLUT;

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
			assert(false && "Cannot merge, invalid aggregation type for resource tree (MERGE_MAX)");
		case MERGE_MIN:
			assert(false && "Cannot merge, invalid aggregation type for resource tree (MERGE_MIN)");
		case MERGE_SUM:
			assert(false && "Cannot merge, invalid aggregation type for resource tree (MERGE_SUM)");
		case MERGE_MULSUM:
			assert(false && "Cannot merge, invalid aggregation type for resource tree (MERGE_MULSUM)");
		case MERGE_EQUAL:
			assert(false && "Cannot merge, invalid aggregation type for resource tree (MERGE_EQUAL)");
		case MERGE_SET:
			assert(false && "Cannot merge, invalid aggregation type for resource tree (MERGE_SET)");
		/* Resource Tree Max: Sum the values for one instance of each DDDG in the code */
		/* Used for integer FUs */
		case MERGE_RESOURCETREEMAX:
			aggrElemFU = 0;
			aggrElemDSP = 0;
			aggrElemFF = 0;
			aggrElemLUT = 0;
			it = resourceTreeContent[name].begin();
			itEnd = resourceTreeContent[name].end();

			// If this resource tree has only one node (i.e. mergeElements was called by BaseDatapath for a single DDDG)
			// we simply return the resources of the single node.
			if(1 == resourceTreeContent[name].size()) {
				aggrElemFU = it->fus;
				aggrElemDSP = it->dsps;
				aggrElemFF = it->ffs;
				aggrElemLUT = it->luts;
			}
			else {
				while(it != itEnd) {
					if(DatapathType::NORMAL_LOOP == it->datapathType) {
						aggrElemFU += it->fus;
						aggrElemDSP += it->dsps;
						aggrElemFF += it->ffs;
						aggrElemLUT += it->luts;
						it++;
					}
					else if(DatapathType::NON_PERFECT_BEFORE == it->datapathType) {
						unsigned beforeLoopLevel = it->loopLevel;
						unsigned beforeFU = it->fus;
						unsigned beforeDSP = it->dsps;
						unsigned beforeFF = it->ffs;
						unsigned beforeLUT = it->luts;

						// We expect the next value to be a NON_PERFECT_AFTER for the same level
						it++;

						// If next node is already in another loop level or we reached the end, nothing to do here apart from adding up
						if(itEnd == it || it->loopLevel != beforeLoopLevel) {
							aggrElemFU += beforeFU;
							aggrElemDSP += beforeDSP;
							aggrElemFF += beforeFF;
							aggrElemLUT += beforeLUT;
						}
						else {
							if(DatapathType::NON_PERFECT_AFTER == it->datapathType) {
								unsigned afterLoopLevel = it->loopLevel;
								unsigned afterFU = it->fus;
								unsigned afterDSP = it->dsps;
								unsigned afterFF = it->ffs;
								unsigned afterLUT = it->luts;

								// We expect the next value to be a NON_PERFECT_BETWEEN for the same level
								it++;

								// If next node is already in another loop level or we reached the end, nothing to do here apart from adding up
								if(itEnd == it || it->loopLevel != afterLoopLevel) {
									aggrElemFU += beforeFU + afterFU;
									aggrElemDSP += beforeDSP + afterDSP;
									aggrElemFF += beforeFF + afterFF;
									aggrElemLUT += beforeLUT + afterLUT;
								}
								else {
									// The only thing that can come after an AFTER for the same loop level is a BETWEEN
									assert(DatapathType::NON_PERFECT_BETWEEN == it->datapathType && "Cannot merge, resource tree has invalid format (missing nodes)");

									// Check if between consumes more than BEFORE and AFTER together. If positive, use this resource
									// count. Otherwise use BEFORE + AFTER
									if((beforeFU + afterFU) > it->fus) {
										aggrElemFU += beforeFU + afterFU;
										aggrElemDSP += beforeDSP + afterDSP;
										aggrElemFF += beforeFF + afterFF;
										aggrElemLUT += beforeLUT + afterLUT;
										it++;
									}
									else {
										aggrElemFU += it->fus;
										aggrElemDSP += it->dsps;
										aggrElemFF += it->ffs;
										aggrElemLUT += it->luts;
										it++;
									}
								}
							}
							else if(DatapathType::NON_PERFECT_BETWEEN == it->datapathType) {
								// There is no AFTER after BEFORE (lol), this means that BEFORE == BETWEEN, nothing to do here apart from adding up
								aggrElemFU += beforeFU;
								aggrElemDSP += beforeDSP;
								aggrElemFF += beforeFF;
								aggrElemLUT += beforeLUT;
								it++;
							}
						}
					}
					else if(DatapathType::NON_PERFECT_AFTER == it->datapathType) {
						unsigned afterLoopLevel = it->loopLevel;
						unsigned afterFU = it->fus;
						unsigned afterDSP = it->dsps;
						unsigned afterFF = it->ffs;
						unsigned afterLUT = it->luts;

						// We expect the next value to be a NON_PERFECT_BETWEEN for the same level
						it++;

						// If next node is already in another loop level or we reached the end, nothing to do here apart from adding up
						if(itEnd == it || it->loopLevel != afterLoopLevel) {
							aggrElemFU += afterFU;
							aggrElemDSP += afterDSP;
							aggrElemFF += afterFF;
							aggrElemLUT += afterLUT;
						}
						else {
							// The only thing that can come after an AFTER for the same loop level is a BETWEEN
							assert(DatapathType::NON_PERFECT_BETWEEN == it->datapathType && "Cannot merge, resource tree has invalid format (missing nodes)");

							// Since there was no BEFORE, this means that AFTER  == BETWEEN, nothing to do here apart from adding up
							aggrElemFU += afterFU;
							aggrElemDSP += afterDSP;
							aggrElemFF += afterFF;
							aggrElemLUT += afterLUT;
							it++;
						}
					}
					else {
						assert(false && "Cannot merge, resource tree has invalid format (missing nodes or out of order)");
					}
				}
			}

			stringFU = std::to_string(aggrElemFU);
			stringDSP = std::to_string(aggrElemDSP);
			stringFF = std::to_string(aggrElemFF);
			stringLUT = std::to_string(aggrElemLUT);

			// Since we will pack these values on a single stream, we assume 10 characters for each number
			// If any exceeds, abort
			assert(stringFU.size() <= 10 && stringDSP.size() <= 10 && stringFF.size() <= 10 && stringLUT.size() <= 10 && "Cannot merge, aggregated values exceeds 10 characters");

			stringFU.insert(0, 10 - stringFU.size(), '0');
			stringDSP.insert(0, 10 - stringDSP.size(), '0');
			stringFF.insert(0, 10 - stringFF.size(), '0');
			stringLUT.insert(0, 10 - stringLUT.size(), '0');

			// Pack'em all!
			return stringFU + stringDSP + stringFF + stringLUT;
		/* Resource List Max: Find the maximum value considering one instance of each DDDG in the code */
		/* Used for float FUs */
		case MERGE_RESOURCELISTMAX:
			aggrElemFU = 0;
			aggrElemDSP = 0;
			aggrElemFF = 0;
			aggrElemLUT = 0;

			for(auto &it : resourceTreeContent[name]) {
				if(it.fus > aggrElemFU) {
					aggrElemFU = it.fus;
					aggrElemDSP = it.dsps;
					aggrElemFF = it.ffs;
					aggrElemLUT = it.luts;
				}
			}
			stringFU = std::to_string(aggrElemFU);
			stringDSP = std::to_string(aggrElemDSP);
			stringFF = std::to_string(aggrElemFF);
			stringLUT = std::to_string(aggrElemLUT);

			// Since we will pack these values on a single stream, we assume 10 characters for each number
			// If any exceeds, abort
			assert(stringFU.size() <= 10 && stringDSP.size() <= 10 && stringFF.size() <= 10 && stringLUT.size() <= 10 && "Cannot merge, aggregated values exceeds 10 characters");

			stringFU.insert(0, 10 - stringFU.size(), '0');
			stringDSP.insert(0, 10 - stringDSP.size(), '0');
			stringFF.insert(0, 10 - stringFF.size(), '0');
			stringLUT.insert(0, 10 - stringLUT.size(), '0');

			// Pack'em all!
			return stringFU + stringDSP + stringFF + stringLUT;
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
	std::vector<resourceNodeTy> otherResourceTreeStuff;

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
			case TYPE_RESOURCENET:
				otherResourceTreeStuff = P.getElements<resourceNodeTy>(std::get<0>(it));
				for(auto &it2 : otherResourceTreeStuff)
					addElement<resourceNodeTy>(std::get<0>(it), it2);
				break;
			default:
				assert(false && "Invalid type of element");
				break;
		}
	}
}

void Pack::clear() {
	std::vector<std::tuple<std::string, unsigned, unsigned>>().swap(structure);
	// XXX: Double-check to see if there is no memory leak due to inner vectors
	std::unordered_map<std::string, std::vector<uint64_t>>().swap(unsignedContent);
	std::unordered_map<std::string, std::vector<int64_t>>().swap(signedContent);
	std::unordered_map<std::string, std::vector<float>>().swap(floatContent);
	std::unordered_map<std::string, std::vector<std::string>>().swap(stringContent);
	std::unordered_map<std::string, std::vector<resourceNodeTy>>().swap(resourceTreeContent);
}
