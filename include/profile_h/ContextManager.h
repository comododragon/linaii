#ifndef __CONTEXTMANAGER_H__
#define __CONTEXTMANAGER_H__

// If using GCC, these pragmas will stop GCC from outputting the annoying misleading indentation warning for this include
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <boost/functional/hash.hpp>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "profile_h/auxiliary.h"
#include "profile_h/DDDGBuilder.h"

#define FILE_CONTEXT_MANAGER "context.dat"
#define FILE_CONTEXT_MANAGER_MAGIC_STRING "!Bc"

class BaseDatapath;
struct ddrInfoTy;
struct globalOutBurstsInfoTy;
struct packInfoTy;
struct outBurstInfoTy;

class ContextManager {
	enum {
		TYPE_EOF = 0,
		TYPE_PROGRESSIVE_TRACE_INFO = 1,
		TYPE_LOOP_BOUND_INFO = 2,
		TYPE_PARSED_TRACE_CONTAINER = 3,
		TYPE_DDDG = 4,
		TYPE_GLOBAL_OUTBURSTS_INFO = 5,
		TYPE_GLOBAL_DDR_MAP = 6,
		TYPE_GLOBAL_PACK_INFO = 7,
	};

	struct cfd_t {
		int length;

		cfd_t(int length) : length(length) { }
	};
	// XXX: You can find the definitions at lib/Build_DDDG/ContextManager.cpp
	static const std::unordered_map<int, cfd_t> typeMap;

	std::string fileName;
	std::fstream contextFile;
	bool readOnly;

	bool seekTo(int type);
	bool seekToIdentified(int type, std::string ID, uint64_t optID2 = 0);
	void realign();
	template<typename T> size_t writeElement(std::stringstream &ss, T &elem);
	template<typename E> size_t writeElement(std::stringstream &ss, std::vector<E> &elem);
	template<typename E> size_t writeElement(std::stringstream &ss, std::set<E> &elem);
	template<typename K, typename E> size_t writeElement(std::stringstream &ss, std::map<K, E> &elem);
	template<typename K, typename E> size_t writeElement(std::stringstream &ss, std::unordered_map<K, E> &elem);
	template<typename K, typename E> size_t writeElement(std::stringstream &ss, std::unordered_map<K, std::vector<E>> &elem);
	template<typename K, typename L, typename E> size_t writeElement(std::stringstream &ss, std::unordered_map<std::pair<K, L>, std::vector<E>, boost::hash<std::pair<K, L>>> &elem);
	template<typename K, typename E, typename F> size_t writeElement(std::stringstream &ss, std::unordered_map<K, std::pair<E, F>> &elem);
	template<typename K, typename E> size_t writeElement(std::stringstream &ss, std::unordered_multimap<K, E> &elem);
	template<typename T> void readElement(std::fstream &fs, T &elem);
	template<typename E> void readElement(std::fstream &fs, std::vector<E> &elem);
	template<typename E> void readElement(std::fstream &fs, std::set<E> &elem);
	template<typename K, typename E> void readElement(std::fstream &fs, std::map<K, E> &elem);
	template<typename K, typename E> void readElement(std::fstream &fs, std::unordered_map<K, E> &elem);
	template<typename K, typename E> void readElement(std::fstream &fs, std::unordered_map<K, std::vector<E>> &elem);
	template<typename K, typename L, typename E> void readElement(std::fstream &fs, std::unordered_map<std::pair<K, L>, std::vector<E>, boost::hash<std::pair<K, L>>> &elem);
	template<typename K, typename E, typename F> void readElement(std::fstream &fs, std::unordered_map<K, std::pair<E, F>> &elem);
	template<typename T> void skipElement(std::fstream &fs);
	void commit(char elemType, std::stringstream &ss, size_t totalFieldSize, std::string optID = "", uint64_t optID2 = 0);
	void commit(char elemType, std::stringstream &ss);

public:
	ContextManager();
	~ContextManager();

	void openForWrite();
	void openForRead();
	void close();
	bool isOpen();

	void saveProgressiveTraceInfo(long int &cursor, uint64_t &instCount);
	void getProgressiveTraceInfo(long int *cursor, uint64_t *instCount);
	void saveLoopBoundInfo(wholeloopName2loopBoundMapTy &wholeloopName2loopBoundMap);
	void getLoopBoundInfo(wholeloopName2loopBoundMapTy *wholeloopName2loopBoundMap);
	void saveParsedTraceContainer(std::string wholeLoopName, unsigned datapathType, unsigned unrollFactor, ParsedTraceContainer &PC);
	void getParsedTraceContainer(std::string wholeLoopName, unsigned datapathType, unsigned unrollFactor, ParsedTraceContainer *PC);
	void saveDDDG(std::string wholeLoopName, unsigned datapathType, unsigned unrollFactor, DDDGBuilder &builder, std::vector<int> &microops);
	void getDDDG(std::string wholeLoopName, unsigned datapathType, unsigned unrollFactor, BaseDatapath *datapath);
	void saveGlobalOutBurstsInfo(std::unordered_map<std::string, std::vector<globalOutBurstsInfoTy>> &globalOutBurstsInfo);
	void getGlobalOutBurstsInfo(std::unordered_map<std::string, std::vector<globalOutBurstsInfoTy>> *globalOutBurstsInfo);
	void saveGlobalDDRMap(std::unordered_map<std::string, std::vector<ddrInfoTy>> &globalDDRMap);
	void getGlobalDDRMap(std::unordered_map<std::string, std::vector<ddrInfoTy>> *globalDDRMap);
	void saveGlobalPackInfo(std::unordered_map<arrayPackSzPairTy, std::vector<packInfoTy>, boost::hash<arrayPackSzPairTy>> &globalPackInfo);
	void getGlobalPackInfo(std::unordered_map<arrayPackSzPairTy, std::vector<packInfoTy>, boost::hash<arrayPackSzPairTy>> *globalPackInfo);

#ifdef DBG_PRINT_ALL
	void printDatabase();
#endif
};

#endif
