#ifndef __CONTEXTMANAGER_H__
#define __CONTEXTMANAGER_H__

#include "profile_h/auxiliary.h"
#include "profile_h/DDDGBuilder.h"

#define FILE_CONTEXT_MANAGER "context.dat"
#define FILE_CONTEXT_MANAGER_MAGIC_STRING "!Bc"

class BaseDatapath;
struct ddrInfoTy;
struct outBurstInfoTy;

class ContextManager {
	enum {
		TYPE_EOF = 0,
		TYPE_PROGRESSIVE_TRACE_INFO = 1,
		TYPE_LOOP_BOUND_INFO = 2,
		TYPE_PARSED_TRACE_CONTAINER = 3,
		TYPE_DDDG = 4,
		TYPE_OUTBURSTS_INFO = 5,
		TYPE_GLOBAL_DDR_MAP = 6,
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
	bool seekToIdentified(int type, std::string ID, unsigned optID2 = 0);
	void realign();
	template<typename T> size_t writeElement(std::stringstream &ss, T &elem);
	template<typename E> size_t writeElement(std::stringstream &ss, std::vector<E> &elem);
	template<typename E> size_t writeElement(std::stringstream &ss, std::set<E> &elem);
	template<typename K, typename E> size_t writeElement(std::stringstream &ss, std::map<K, E> &elem);
	template<typename K, typename E> size_t writeElement(std::stringstream &ss, std::unordered_map<K, E> &elem);
	template<typename K, typename E> size_t writeElement(std::stringstream &ss, std::unordered_map<K, std::vector<E>> &elem);
	template<typename K, typename E, typename F> size_t writeElement(std::stringstream &ss, std::unordered_map<K, std::pair<E, F>> &elem);
	template<typename K, typename E> size_t writeElement(std::stringstream &ss, std::unordered_multimap<K, E> &elem);
	template<typename T> void readElement(std::fstream &fs, T &elem);
	template<typename E> void readElement(std::fstream &fs, std::vector<E> &elem);
	template<typename E> void readElement(std::fstream &fs, std::set<E> &elem);
	template<typename K, typename E> void readElement(std::fstream &fs, std::map<K, E> &elem);
	template<typename K, typename E> void readElement(std::fstream &fs, std::unordered_map<K, E> &elem);
	template<typename K, typename E> void readElement(std::fstream &fs, std::unordered_map<K, std::vector<E>> &elem);
	template<typename T> void skipElement(std::fstream &fs);
	void commit(char elemType, std::stringstream &ss, size_t totalFieldSize, std::string optID = "", unsigned optID2 = 0);
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
	void saveParsedTraceContainer(std::string wholeLoopName, ParsedTraceContainer &PC);
	void getParsedTraceContainer(std::string wholeLoopName, ParsedTraceContainer *PC);
	void saveDDDG(std::string wholeLoopName, unsigned datapathType, DDDGBuilder &builder, std::vector<int> &microops);
	void getDDDG(std::string wholeLoopName, unsigned datapathType, BaseDatapath *datapath);
	void saveOutBurstsInfo(std::string wholeLoopName, unsigned datapathType,
		std::unordered_map<std::string, outBurstInfoTy> &loadOutBurstsFound, std::unordered_map<std::string, outBurstInfoTy> &storeOutBurstsFound);
	void getOutBurstsInfo(std::string wholeLoopName, unsigned datapathType,
		std::unordered_map<std::string, outBurstInfoTy> *loadOutBurstsFound, std::unordered_map<std::string, outBurstInfoTy> *storeOutBurstsFound);
	void saveGlobalDDRMap(std::unordered_map<std::string, std::vector<ddrInfoTy>> &globalDDRMap);
	void getGlobalDDRMap(std::unordered_map<std::string, std::vector<ddrInfoTy>> *globalDDRMap);

#ifdef DBG_PRINT_ALL
	void printDatabase();
#endif
};

#endif
