#ifndef __DDDGBUILDER_H__
#define __DDDGBUILDER_H__

#include <fstream>
#include <map>
#include <set>
#include <stack>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <unordered_map>
#include <zlib.h>

#include "profile_h/auxiliary.h"
//#include "profile_h/file_func.h"
#include "profile_h/opcodes.h"

typedef std::unordered_map<std::string, std::string> instName2bbNameMapTy;
extern instName2bbNameMapTy instName2bbNameMap;

typedef std::map<std::pair<std::string, std::string>, std::string> headerBBFuncNamePair2lastInstMapTy;
extern headerBBFuncNamePair2lastInstMapTy headerBBFuncNamePair2lastInstMap;
extern headerBBFuncNamePair2lastInstMapTy exitingBBFuncNamePair2lastInstMap;

typedef std::pair<std::string, std::string> lpNameLevelStrPairTy;
typedef std::map<lpNameLevelStrPairTy, std::string> lpNameLevelPair2headBBnameMapTy;
extern lpNameLevelPair2headBBnameMapTy lpNameLevelPair2headBBnameMap;
extern lpNameLevelPair2headBBnameMapTy lpNameLevelPair2exitingBBnameMap;

typedef std::map<std::string, std::vector<unsigned> > loopName2levelUnrollVecMapTy;
extern loopName2levelUnrollVecMapTy loopName2levelUnrollVecMap;

typedef std::map<std::pair<std::string, std::string>, unsigned> funcBBNmPair2numInstInBBMapTy;
extern funcBBNmPair2numInstInBBMapTy funcBBNmPair2numInstInBBMap;

typedef std::map<std::string, bool> wholeloopName2perfectOrNotMapTy;
extern wholeloopName2perfectOrNotMapTy wholeloopName2perfectOrNotMap;

typedef std::pair<uint64_t, uint64_t> lineFromToTy;

typedef std::map<std::string, std::pair<std::string, unsigned> > headerBBlastInst2loopNameLevelPairMapTy;

typedef std::unordered_map<std::string, unsigned> s2uMap;

struct edgeNodeInfo {
	unsigned sink;
	int paramID;
};

typedef std::unordered_multimap<unsigned, edgeNodeInfo> u2eMMap;

typedef std::unordered_map<int64_t, unsigned> i642uMap;

#if 0
struct edge_node_info{
  unsigned sink_node;
  int par_id;
};

//data structure used to tract dependency
typedef unordered_map<std::string, unsigned int> string_to_uint;
typedef unordered_map<long long int, unsigned int> uint_to_uint;
typedef unordered_multimap<unsigned int, edge_node_info> multi_uint_to_node_info;
/// (Function Name, Basic Block Name) --> number of instructions in a BB Map
//typedef std::map<std::string, std::vector<uint64_t> > loopName2levelLpBoundVecMapTy;

//extern loopName2levelLpBoundVecMapTy loopName2levelLpBoundVecMap;

namespace llvm {
	//funcBBNmPair2numInstInBBMapTy funcBBNmPair2numInstInBBMap;
}

typedef std::pair<uint64_t, uint64_t> line_from_to_Ty;
#endif

class BaseDatapath;

class ParsedTraceContainer {
	BaseDatapath *datapath;

	std::string funcFileName;
	std::string instIDFileName;
	std::string lineNoFileName;
	std::string memoryTraceFileName;
	std::string getElementPtrFileName;
	std::string prevBasicBlockFileName;
	std::string currBasicBlockFileName;

	gzFile funcFile;
	gzFile instIDFile;
	gzFile lineNoFile;
	gzFile memoryTraceFile;
	gzFile getElementPtrFile;
	gzFile prevBasicBlockFile;
	gzFile currBasicBlockFile;

	std::vector<std::string> funcList;
	std::vector<std::string> instIDList;
	std::vector<int> lineNoList;
	std::vector<std::tuple<int, int64_t, unsigned>> memoryTraceList;
	std::unordered_map<int, std::pair<std::string, int64_t>> getElementPtrList;
	std::vector<std::string> prevBasicBlockList;
	std::vector<std::string> currBasicBlockList;

	bool compressed;
	bool keepAliveRead;
	bool keepAliveWrite;
	bool locked;

public:
	ParsedTraceContainer(std::string kernelName);
	~ParsedTraceContainer();

	void openAndClearAllFiles();
	void openAllFilesForRead();
	void closeAllFiles();
	void lock();

	void appendToFuncList(std::string elem);
	void appendToInstIDList(std::string elem);
	void appendToLineNoList(int elem);
	void appendToMemoryTraceList(int elem, int64_t elem2, unsigned elem3);
	void appendToGetElementPtrList(int key, std::string elem, int64_t elem2);
	void appendToPrevBBList(std::string elem);
	void appendToCurrBBList(std::string elem);

	const std::vector<std::string> &getFuncList();
	const std::vector<std::string> &getInstIDList();
	const std::vector<int> &getLineNoList();
	const std::vector<std::tuple<int, int64_t, unsigned>> &getMemoryTraceList();
	const std::unordered_map<int, std::pair<std::string, int64_t>> &getGetElementPtrList();
	const std::vector<std::string> &getPrevBBList();
	const std::vector<std::string> &getCurrBBList();
};

class DDDGBuilder {
	BaseDatapath *datapath;
	ParsedTraceContainer &PC;

	std::string rest;
	uint8_t prevMicroop, currMicroop;
	std::string currInstID;
	std::string currDynamicFunction;
	std::string calleeFunction;
	std::stack<std::pair<std::string, int>> activeMethod;
	s2uMap functionCounter;
	std::string prevBB, currBB;
	int numOfInstructions;
	bool lastParameter;
	std::vector<int64_t> parameterValuePerInst;
	std::vector<unsigned> parameterSizePerInst;
	std::vector<std::string> parameterLabelPerInst;
	s2uMap registerLastWritten;
	std::string calleeDynamicFunction;
	int lastCallSource;
	u2eMMap registerEdgeTable;
	u2eMMap memoryEdgeTable;
	unsigned numOfRegDeps, numOfMemDeps;
	i642uMap addressLastWritten;

	lineFromToTy getTraceLineFromTo(gzFile &traceFile);
	void parseTraceFile(gzFile &traceFile, lineFromToTy fromToPair);
	void parseInstructionLine();
	void parseResult();
	void parseForward();
	void parseParameter(int param);

	void writeDDDG();

public:
	DDDGBuilder(BaseDatapath *datapath, ParsedTraceContainer &PC);

	void buildInitialDDDG();

	unsigned getNumEdges();
	unsigned getNumNodes();
	unsigned getNumOfRegisterDependencies();
	unsigned getNumOfMemoryDependencies();
};

#endif
