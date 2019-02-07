#include "profile_h/auxiliary.h"

#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

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

std::string constructLoopName(std::string funcName, int loopNo, int depth) {
	std::string loopName = funcName + "_loop" + std::to_string(loopNo);

	if(-1 == depth)
		return loopName;

	return appendDepthToLoopName(loopName, depth);
}

std::string appendDepthToLoopName(std::string loopName, int depth) {
	return loopName + "_" + std::to_string(depth);
}

std::tuple<std::string, int, int> parseLoopName(std::string loopName) {
	const std::string mainLoopTag = "_loop";
	const std::string depthTag = "_";
	const size_t mainLoopTagSize = 5;
	const size_t depthTagSize = 1;

	size_t tagPos = loopName.find(mainLoopTag);
	std::string funcName = loopName.substr(0, tagPos);

	std::string rest = loopName.substr(tagPos + mainLoopTagSize);
	tagPos = rest.find(depthTag);
	int loopNo = std::stoi(rest.substr(0, tagPos));
	int loopLevel = std::stoi(rest.substr(tagPos + depthTagSize));

	return std::make_tuple(funcName, loopNo, loopLevel);
}

ConfigurationManager::ConfigurationManager(std::string kernelName) : kernelName(kernelName) { }

void ConfigurationManager::appendToPipeliningCfg(std::string funcName, int loopNo, int loopLevel) {
	pipeliningCfgTy elem;

	elem.funcName = funcName;
	elem.loopNo = loopNo;
	elem.loopLevel = loopLevel;

	pipeliningCfg.push_back(elem);
}

void ConfigurationManager::appendToUnrollingCfg(std::string funcName, int loopNo, int loopLevel, int lineNo, int unrollFactor) {
	unrollingCfgTy elem;

	elem.funcName = funcName;
	elem.loopNo = loopNo;
	elem.loopLevel = loopLevel;
	elem.lineNo = lineNo;
	elem.unrollFactor = unrollFactor;

	unrollingCfg.push_back(elem);
}

void ConfigurationManager::appendToPartitionCfg(int pFactor) {
	partitionCfgTy elem;

	elem.pFactor = pFactor;

	partitionCfg.push_back(elem);
}

void ConfigurationManager::appendToCompletePartitionCfg(int pFactor) {
	partitionCfgTy elem;

	elem.pFactor = pFactor;

	completePartitionCfg.push_back(elem);
}

void ConfigurationManager::appendToArrayInfoCfg(std::string line) {
	arrayInfoCfgTy elem;

	elem.line = line;

	arrayInfoCfg.push_back(elem);
}

void ConfigurationManager::clear() {
	pipeliningCfg.clear();
	unrollingCfg.clear();
	partitionCfg.clear();
	completePartitionCfg.clear();
	arrayInfoCfg.clear();
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

	std::map<std::string, unsigned> wholeLoopName2CompUnrollFactorMap;

	if(pipeliningCfgStr.size()) {
		for(std::string i : pipeliningCfgStr) {
			char buff[BUFF_STR_SZ];
			int loopNo, loopLevel;
			sscanf(i.c_str(), "%*[^,],%[^,],%d,%d\n", buff, &loopNo, &loopLevel);

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

			for(unsigned int j = loopLevel + 1; j < numLevel + 1; j++) {
				std::string wholeLoopName2 = appendDepthToLoopName(loopName, j);
				wholeloopName2loopBoundMapTy::iterator found2 = wholeloopName2loopBoundMap.find(wholeLoopName2);
				assert(found2 != wholeloopName2loopBoundMap.end() && "Cannot find loop name provided in configuration file");
				unsigned loopBound = found2->second;

				// TODO: This is a silent error/warning. Is this correct (i.e. nothing should be performed apart from informing the user)?
				if(!loopBound)
					VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Variable loop bound found for \"" << wholeLoopName2 << "\", pipelining not supported in current version\n");

				wholeLoopName2CompUnrollFactorMap.insert(std::make_pair(wholeLoopName2, loopBound));
			}	
		}
	}

	if(unrollingCfgStr.size()) {
		std::vector<std::string> unrollWholeLoopNameStr;

		// TODO: IS lineNo REALLY NECESSARY?
		for(std::string i : unrollingCfgStr) {
			char buff[BUFF_STR_SZ];
			int loopNo, loopLevel, lineNo, unrollFactor;
			sscanf(i.c_str(), "%*[^,],%[^,],%d,%d,%d,%d\n", buff, &loopNo, &loopLevel, &lineNo, &unrollFactor);

			std::string funcName(buff);
			std::string wholeLoopName = constructLoopName(funcName, loopNo, loopLevel);
			unrollWholeLoopNameStr.push_back(wholeLoopName);

			std::map<std::string, unsigned>::iterator found = wholeLoopName2CompUnrollFactorMap.find(wholeLoopName);
			if(wholeLoopName2CompUnrollFactorMap.end() == found) {
				appendToUnrollingCfg(funcName, loopNo, loopLevel, lineNo, unrollFactor);
			}
			else {
				unsigned staticBound = found->second;
				if(staticBound)
					appendToUnrollingCfg(funcName, loopNo, loopLevel, lineNo, staticBound);
				else
					appendToUnrollingCfg(funcName, loopNo, loopLevel, lineNo, unrollFactor);
			}
		}

		// If loop pipelining in a loop that have nested non-unrolled loops, these loops must be fully unrolled. Therefore such
		// configurations are automatically added
		for(std::pair<std::string, unsigned> it : wholeLoopName2CompUnrollFactorMap) {
			std::string wholeLoopName = it.first;
			unsigned loopBound = it.second;

			std::vector<std::string>::iterator found = std::find(unrollWholeLoopNameStr.begin(), unrollWholeLoopNameStr.end(), wholeLoopName);
			if(unrollWholeLoopNameStr.end() == found) {
				std::tuple<std::string, int, int> parsed = parseLoopName(wholeLoopName);
				appendToUnrollingCfg(std::get<0>(parsed), std::get<1>(parsed), std::get<2>(parsed), -1, loopBound);
			}
		}
	}

	// TODO: consertar os elementos!
	if(arrayInfoCfgStr.size()) {
		for(std::string i : arrayInfoCfgStr)
			appendToArrayInfoCfg(i);
	}
	else {
		assert(false && "Please provide array information for this kernel");
	}

	// TODO: consertar os elementos!
	if(partitionCfgStr.size()) {
		for(std::string i : partitionCfgStr) {
			int pFactor;
			sscanf(i.c_str(), "%*[^,],%*[^,],%*[^,],%*d,%*d,%d\n", &pFactor);

			appendToPartitionCfg(pFactor);
		}
	}

	// TODO: consertar os elementos!
	if(completePartitionCfgStr.size()) {
		for(std::string i : completePartitionCfgStr) {
			int pFactor;
			sscanf(i.c_str(), "%*[^,],%*[^,],%*[^,],%*d,%*d,%d\n", &pFactor);

			appendToCompletePartitionCfg(pFactor);
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
}
