#include "profile_h/ContextManager.h"

#include <sstream>

#include "profile_h/BaseDatapath.h"

const std::unordered_map<int, ContextManager::cfd_t> ContextManager::typeMap = {
	{ContextManager::TYPE_PROGRESSIVE_TRACE_INFO, cfd_t(sizeof(long int) + sizeof(uint64_t))},
	{ContextManager::TYPE_LOOP_BOUND_INFO, cfd_t(-1)},
	{ContextManager::TYPE_PARSED_TRACE_CONTAINER, cfd_t(-1)},
	{ContextManager::TYPE_DDDG, cfd_t(-1)},
	{ContextManager::TYPE_OUTBURSTS_INFO, cfd_t(-1)},
	{ContextManager::TYPE_GLOBAL_DDR_MAP, cfd_t(-1)},
#ifdef CROSS_DDDG_PACKING
	{ContextManager::TYPE_REMAINING_BUDGET_INFO, cfd_t(-1)},
#endif
};

// TODO
// TODO Maybe create reverseSeek to speedup reading/
// TODO

// Seek to the desired type. If return is true, it means that the field was found.
// In this case, the cursor will be positioned at the first data if the field length is fixed
// Otherwise, the cursor will be positioned at the field length
// Return will be false if element was not found
// If the file is corrupt and the desired field is located right before EOF, this function
// will return true and will be pointing to EOF, so careful!
bool ContextManager::seekTo(int type) {
	bool wentAround = false;
	int origCurPos = contextFile.tellg();

	while(!wentAround || contextFile.tellg() < origCurPos) {
		char readType;
		contextFile.read((char *) &readType, 1);

		if(contextFile.eof()) {
			assert(!(contextFile.gcount()) && "Context file is incomplete or corrupt");
			// This is needed otherwise seekg() won't work (I ******* HATE C++!!!!!!!!!!!!!!!!!!!!!!!!)
			contextFile.clear();
			contextFile.seekg(3);
			wentAround = true;
		}
		else {
			if(readType == (char) type) {
				return true;
			}
			else {
				int typeLength = typeMap.at(type).length;

				// Variable length
				if(-1 == typeLength) {
					size_t size;
					contextFile.read((char *) &size, sizeof(size_t));

					assert(!(contextFile.eof()) && "Context file is incomplete or corrupt");

					// Skip this field entirely
					contextFile.seekg(size, std::ios::cur);
				}
				else {
					contextFile.seekg(typeLength, std::ios::cur);
				}
			}
		}
	}

	return false;
}

// Seek to the desired type and identified by ID. If return is true, it means that the field was found.
// Differently of seekTo() that might point either to the content or to the field length, this function
// will always point to the first content, if any.
bool ContextManager::seekToIdentified(int type, std::string ID, unsigned ID2) {
	std::string readID = "";
	unsigned readID2 = 0;
	std::string readIDFirst = "none";
	unsigned readID2First;
	bool foundTheRightOne = false;

	while(!foundTheRightOne) {
		if(seekTo(type)) {
			size_t totalFieldSize;
			contextFile.read((char *) &totalFieldSize, sizeof(size_t));

			size_t stringSize;
			contextFile.read((char *) &stringSize, sizeof(size_t));

			assert(stringSize < BUFF_STR_SZ && "A read of more than BUFF_STR_SZ bytes was requested from context file and it was blocked. If you believe that this was not caused by a corrupt context file, increase BUFF_STR_SZ");

			char buff[BUFF_STR_SZ];
			contextFile.read(buff, stringSize);
			buff[stringSize] = '\0';
			readID.assign(buff);

			contextFile.read((char *) &readID2, sizeof(unsigned));

			if(ID == readID && ID2 == readID2)
				foundTheRightOne = true;
			else
				contextFile.seekg(totalFieldSize - sizeof(size_t) - stringSize - sizeof(unsigned), std::ios::cur);

			if("none" == readIDFirst) {
				readIDFirst.assign(readID);
				readID2First = readID2;
			}
			else if(readIDFirst == readID && readID2First == readID2) {
				break;
			}
		}
		else {
			break;
		}
	}

	return foundTheRightOne;
}


template<typename T> size_t ContextManager::writeElement(std::stringstream &ss, T &elem) {
	ss.write((char *) &elem, sizeof(T));

	return sizeof(T);
}

template<> size_t ContextManager::writeElement<std::string>(std::stringstream &ss, std::string &elem) {
	size_t writtenSize = 0;

	// Write the string length
	size_t length = elem.length();
	ss.write((char *) &length, sizeof(size_t));
	writtenSize += sizeof(size_t);

	// Write the string itself
	ss.write(elem.c_str(), length);
	writtenSize += length;

	return writtenSize;
}

template<> size_t ContextManager::writeElement<edgeNodeInfo>(std::stringstream &ss, edgeNodeInfo &elem) {
	size_t writtenSize = 0;

	writtenSize += writeElement<unsigned>(ss, elem.sink);
	writtenSize += writeElement<int>(ss, elem.paramID);

	return writtenSize;
}

template<> size_t ContextManager::writeElement<ddrInfoTy>(std::stringstream &ss, ddrInfoTy &elem) {
	size_t writtenSize = 0;

	writtenSize += writeElement<unsigned>(ss, elem.loopLevel);
	writtenSize += writeElement<unsigned>(ss, elem.datapathType);
	writtenSize += writeElement<std::string>(ss, elem.arraysLoaded);
	writtenSize += writeElement<std::string>(ss, elem.arraysStored);

	return writtenSize;
}

template<> size_t ContextManager::writeElement<outBurstInfoTy>(std::stringstream &ss, outBurstInfoTy &elem) {
	size_t writtenSize = 0;

	writtenSize += writeElement<bool>(ss, elem.canOutBurst);
	writtenSize += writeElement<bool>(ss, elem.isRegistered);
	writtenSize += writeElement<uint64_t>(ss, elem.baseAddress);
	writtenSize += writeElement<uint64_t>(ss, elem.offset);
#ifdef VAR_WSIZE
	writtenSize += writeElement<uint64_t>(ss, elem.wordSize);
#endif

	return writtenSize;
}

#ifdef CROSS_DDDG_PACKING
template<> size_t ContextManager::writeElement<remainingBudgetTy>(std::stringstream &ss, remainingBudgetTy &elem) {
	size_t writtenSize = 0;

	writtenSize += writeElement<bool>(ss, elem.isValid);
	writtenSize += writeElement<unsigned>(ss, elem.remainingBudget);
	writtenSize += writeElement<uint64_t>(ss, elem.baseAddress);
	writtenSize += writeElement<uint64_t>(ss, elem.offset);

	return writtenSize;
}
#endif

template<typename E> size_t ContextManager::writeElement(std::stringstream &ss, std::vector<E> &elem) {
	size_t writtenSize = 0;
	size_t elementSize = elem.size();

	writtenSize += writeElement<size_t>(ss, elementSize);
	for(auto &it : elem)
		writtenSize += writeElement<E>(ss, it);

	return writtenSize;
}

template<typename E> size_t ContextManager::writeElement(std::stringstream &ss, std::set<E> &elem) {
	size_t writtenSize = 0;
	size_t elementSize = elem.size();

	writtenSize += writeElement<size_t>(ss, elementSize);
	for(auto &it : elem)
		writtenSize += writeElement<E>(ss, const_cast<E &>(it));

	return writtenSize;
}

template<typename K, typename E> size_t ContextManager::writeElement(std::stringstream &ss, std::map<K, E> &elem) {
	size_t writtenSize = 0;
	size_t elementSize = elem.size();

	writtenSize += writeElement<size_t>(ss, elementSize);
	for(auto &it : elem) {
		writtenSize += writeElement<K>(ss, const_cast<K &>(it.first));
		writtenSize += writeElement<E>(ss, it.second);
	}

	return writtenSize;
}

template<typename K, typename E> size_t ContextManager::writeElement(std::stringstream &ss, std::unordered_map<K, E> &elem) {
	size_t writtenSize = 0;
	size_t elementSize = elem.size();

	writtenSize += writeElement<size_t>(ss, elementSize);
	for(auto &it : elem) {
		writtenSize += writeElement<K>(ss, const_cast<K &>(it.first));
		writtenSize += writeElement<E>(ss, it.second);
	}

	return writtenSize;
}

template<typename K, typename E> size_t ContextManager::writeElement(std::stringstream &ss, std::unordered_map<K, std::vector<E>> &elem) {
	size_t writtenSize = 0;
	size_t elementSize = elem.size();

	writtenSize += writeElement<size_t>(ss, elementSize);
	for(auto &it : elem) {
		writtenSize += writeElement<K>(ss, const_cast<K &>(it.first));
		writtenSize += writeElement<E>(ss, it.second);
	}

	return writtenSize;
}

template<typename K, typename E, typename F> size_t ContextManager::writeElement(std::stringstream &ss, std::unordered_map<K, std::pair<E, F>> &elem) {
	size_t writtenSize = 0;
	size_t elementSize = elem.size();

	writtenSize += writeElement<size_t>(ss, elementSize);
	for(auto &it : elem) {
		writtenSize += writeElement<K>(ss, const_cast<K &>(it.first));
		writtenSize += writeElement<E>(ss, it.second.first);
		writtenSize += writeElement<F>(ss, it.second.second);
	}

	return writtenSize;
}

template<typename K, typename E> size_t ContextManager::writeElement(std::stringstream &ss, std::unordered_multimap<K, E> &elem) {
	size_t writtenSize = 0;
	size_t elementSize = elem.size();

	writtenSize += writeElement<size_t>(ss, elementSize);
	for(auto &it : elem) {
		writtenSize += writeElement<K>(ss, const_cast<K &>(it.first));
		writtenSize += writeElement<E>(ss, it.second);
	}

	return writtenSize;
}

template<> size_t ContextManager::writeElement<ParsedTraceContainer>(std::stringstream &ss, ParsedTraceContainer &elem) {
	size_t writtenSize = 0;

// TODO TODO TODO
#if 1//DBG_PRINT_ALL
	DBG_DUMP("Dump of ParsedTraceContainer\n");
	DBG_DUMP("-- funcList:\n");
	for(auto const &x : elem.getFuncList())
		DBG_DUMP("---- " << x << "\n");
	DBG_DUMP("-- instIDList:\n");
	for(auto const &x : elem.getInstIDList())
		DBG_DUMP("---- " << x << "\n");
	DBG_DUMP("-- lineNoList:\n");
	for(auto const &x : elem.getLineNoList())
		DBG_DUMP("---- " << x << "\n");
	DBG_DUMP("-- memoryTraceList:\n");
	for(auto const &x : elem.getMemoryTraceList())
		DBG_DUMP("---- " << x.first << ": <" << x.second.first << ", " << x.second.second << ">\n");
	DBG_DUMP("-- getElementPtrList:\n");
	for(auto const &x : elem.getGetElementPtrList())
		DBG_DUMP("---- " << x.first << ": <" << x.second.first << ", " << x.second.second << ">\n");
	DBG_DUMP("-- prevBBList:\n");
	for(auto const &x : elem.getPrevBBList())
		DBG_DUMP("---- " << x << "\n");
	DBG_DUMP("-- currBBList:\n");
	for(auto const &x : elem.getCurrBBList())
		DBG_DUMP("---- " << x << "\n");
#endif

	writtenSize += writeElement<std::string>(ss, const_cast<std::vector<std::string> &>(elem.getFuncList()));
	writtenSize += writeElement<std::string>(ss, const_cast<std::vector<std::string> &>(elem.getInstIDList()));
	writtenSize += writeElement<int>(ss, const_cast<std::vector<int> &>(elem.getLineNoList()));
	writtenSize += writeElement<int, int64_t, unsigned>(ss, const_cast<std::unordered_map<int, std::pair<int64_t, unsigned>> &>(elem.getMemoryTraceList()));
	writtenSize += writeElement<int, std::string, int64_t>(ss, const_cast<std::unordered_map<int, std::pair<std::string, int64_t>> &>(elem.getGetElementPtrList()));
	writtenSize += writeElement<std::string>(ss, const_cast<std::vector<std::string> &>(elem.getPrevBBList()));
	writtenSize += writeElement<std::string>(ss, const_cast<std::vector<std::string> &>(elem.getCurrBBList()));

	return writtenSize;
}

template<typename T> void ContextManager::readElement(std::fstream &fs, T &elem) {
	fs.read((char *) &elem, sizeof(T));
}

template<> void ContextManager::readElement<std::string>(std::fstream &fs, std::string &elem) {
	size_t stringSize;
	readElement<size_t>(fs, stringSize);

	assert(stringSize < BUFF_STR_SZ && "A read of more than BUFF_STR_SZ bytes was requested from context file and it was blocked. If you believe that this was not caused by a corrupt context file, increase BUFF_STR_SZ");

	char buff[BUFF_STR_SZ];
	fs.read(buff, stringSize);
	buff[stringSize] = '\0';
	elem.assign(buff);
}

template<> void ContextManager::readElement<edgeNodeInfo>(std::fstream &fs, edgeNodeInfo &elem) {
	readElement<unsigned>(fs, elem.sink);
	readElement<int>(fs, elem.paramID);
}

template<> void ContextManager::readElement<ddrInfoTy>(std::fstream &fs, ddrInfoTy &elem) {
	readElement<unsigned>(fs, elem.loopLevel);
	readElement<unsigned>(fs, elem.datapathType);
	readElement<std::string>(fs, elem.arraysLoaded);
	readElement<std::string>(fs, elem.arraysStored);
}

template<> void ContextManager::readElement<outBurstInfoTy>(std::fstream &fs, outBurstInfoTy &elem) {
	readElement<bool>(fs, elem.canOutBurst);
	readElement<bool>(fs, elem.isRegistered);
	readElement<uint64_t>(fs, elem.baseAddress);
	readElement<uint64_t>(fs, elem.offset);
#ifdef VAR_WSIZE
	readElement<uint64_t>(fs, elem.wordSize);
#endif
}

#ifdef CROSS_DDDG_PACKING
template<> void ContextManager::readElement<remainingBudgetTy>(std::fstream &fs, remainingBudgetTy &elem) {
	readElement<bool>(fs, elem.isValid);
	readElement<unsigned>(fs, elem.remainingBudget);
	readElement<uint64_t>(fs, elem.baseAddress);
	readElement<uint64_t>(fs, elem.offset);
}
#endif

template<typename E> void ContextManager::readElement(std::fstream &fs, std::vector<E> &elem) {
	size_t elemSize;
	readElement<size_t>(fs, elemSize);

	elem.clear();
	for(size_t i = 0; i < elemSize; i++) {
		E ee;
		readElement<E>(fs, ee);

		elem.push_back(ee);
	}
}

template<typename E> void ContextManager::readElement(std::fstream &fs, std::set<E> &elem) {
	size_t setSize;
	readElement<size_t>(fs, setSize);

	elem.clear();
	for(size_t i = 0; i < setSize; i++) {
		E ee;
		readElement<E>(fs, ee);

		elem.insert(ee);
	}
}

template<typename K, typename E> void ContextManager::readElement(std::fstream &fs, std::map<K, E> &elem) {
	size_t mapSize;
	readElement<size_t>(fs, mapSize);

	elem.clear();
	for(size_t i = 0; i < mapSize; i++) {
		K kk;
		readElement<K>(fs, kk);
		E ee;
		readElement<E>(fs, ee);

		elem.insert(std::make_pair(kk, ee));
	}
}

template<typename K, typename E> void ContextManager::readElement(std::fstream &fs, std::unordered_map<K, E> &elem) {
	size_t mapSize;
	readElement<size_t>(fs, mapSize);

	elem.clear();
	for(size_t i = 0; i < mapSize; i++) {
		K kk;
		readElement<K>(fs, kk);
		E ee;
		readElement<E>(fs, ee);

		elem.insert(std::make_pair(kk, ee));
	}
}

template<typename K, typename E> void ContextManager::readElement(std::fstream &fs, std::unordered_map<K, std::vector<E>> &elem) {
	size_t mapSize;
	readElement<size_t>(fs, mapSize);

	elem.clear();
	for(size_t i = 0; i < mapSize; i++) {
		K kk;
		readElement<K>(fs, kk);
		std::vector<E> ee;
		readElement<E>(fs, ee);

		elem.insert(std::make_pair(kk, ee));
	}
}

template<> void ContextManager::readElement<ParsedTraceContainer>(std::fstream &fs, ParsedTraceContainer &elem) {
	elem = ParsedTraceContainer(elem.getKernelName());

	elem.openAndClearAllFiles();

	size_t funcListSize;
	readElement<size_t>(fs, funcListSize);
	for(size_t i = 0; i < funcListSize; i++) {
		std::string ee;
		readElement<std::string>(fs, ee);

		elem.appendToFuncList(ee);
	}

	size_t instIDListSize;
	readElement<size_t>(fs, instIDListSize);
	for(size_t i = 0; i < instIDListSize; i++) {
		std::string ee;
		readElement<std::string>(fs, ee);

		elem.appendToInstIDList(ee);
	}

	size_t lineNoListSize;
	readElement<size_t>(fs, lineNoListSize);
	for(size_t i = 0; i < lineNoListSize; i++) {
		int ee;
		readElement<int>(fs, ee);

		elem.appendToLineNoList(ee);
	}

	size_t memoryTraceListSize;
	readElement<size_t>(fs, memoryTraceListSize);
	for(size_t i = 0; i < memoryTraceListSize; i++) {
		int kk;
		readElement<int>(fs, kk);
		int64_t ee;
		readElement<int64_t>(fs, ee);
		unsigned ff;
		readElement<unsigned>(fs, ff);

		elem.appendToMemoryTraceList(kk, ee, ff);
	}

	size_t getElementPtrListSize;
	readElement<size_t>(fs, getElementPtrListSize);
	for(size_t i = 0; i < getElementPtrListSize; i++) {
		int kk;
		readElement<int>(fs, kk);
		std::string ee;
		readElement<std::string>(fs, ee);
		int64_t ff;
		readElement<int64_t>(fs, ff);

		elem.appendToGetElementPtrList(kk, ee, ff);
	}

	size_t prevBBListSize;
	readElement<size_t>(fs, prevBBListSize);
	for(size_t i = 0; i < prevBBListSize; i++) {
		std::string ee;
		readElement<std::string>(fs, ee);

		elem.appendToPrevBBList(ee);
	}

	size_t currBBListSize;
	readElement<size_t>(fs, currBBListSize);
	for(size_t i = 0; i < currBBListSize; i++) {
		std::string ee;
		readElement<std::string>(fs, ee);

		elem.appendToCurrBBList(ee);
	}

// TODO TODO TODO
#if 1//DBG_PRINT_ALL
	DBG_DUMP("Dump of ParsedTraceContainer\n");
	DBG_DUMP("-- funcList:\n");
	for(auto const &x : elem.getFuncList())
		DBG_DUMP("---- " << x << "\n");
	DBG_DUMP("-- instIDList:\n");
	for(auto const &x : elem.getInstIDList())
		DBG_DUMP("---- " << x << "\n");
	DBG_DUMP("-- lineNoList:\n");
	for(auto const &x : elem.getLineNoList())
		DBG_DUMP("---- " << x << "\n");
	DBG_DUMP("-- memoryTraceList:\n");
	for(auto const &x : elem.getMemoryTraceList())
		DBG_DUMP("---- " << x.first << ": <" << x.second.first << ", " << x.second.second << ">\n");
	DBG_DUMP("-- getElementPtrList:\n");
	for(auto const &x : elem.getGetElementPtrList())
		DBG_DUMP("---- " << x.first << ": <" << x.second.first << ", " << x.second.second << ">\n");
	DBG_DUMP("-- prevBBList:\n");
	for(auto const &x : elem.getPrevBBList())
		DBG_DUMP("---- " << x << "\n");
	DBG_DUMP("-- currBBList:\n");
	for(auto const &x : elem.getCurrBBList())
		DBG_DUMP("---- " << x << "\n");
#endif
}

template<> void ContextManager::readElement<BaseDatapath>(std::fstream &fs, BaseDatapath &elem) {
// TODO TODO TODO
#if 1//DBG_PRINT_ALL
	DBG_DUMP("Dump of DDDG:\n");
#endif

	elem.setForDDDGImport();

// TODO TODO TODO
#if 1//DBG_PRINT_ALL
	DBG_DUMP("-- edgeTables.first:\n");
#endif
	size_t edgeListFirstSize;
	readElement<size_t>(fs, edgeListFirstSize);
	for(size_t i = 0; i < edgeListFirstSize; i++) {
		unsigned kk;
		readElement<unsigned>(fs, kk);
		edgeNodeInfo ee;
		readElement<edgeNodeInfo>(fs, ee);

// TODO TODO TODO
#if 1//DBG_PRINT_ALL
		DBG_DUMP("---- " << kk << ": <" << ee.sink << ", " << ee.paramID << ">\n");
#endif
		elem.insertDDDGEdge(kk, ee.sink, ee.paramID);
	}

// TODO TODO TODO
#if 1//DBG_PRINT_ALL
	DBG_DUMP("-- edgeTables.second:\n");
#endif
	size_t edgeListSecondSize;
	readElement<size_t>(fs, edgeListSecondSize);
	for(size_t i = 0; i < edgeListSecondSize; i++) {
		unsigned kk;
		readElement<unsigned>(fs, kk);
		edgeNodeInfo ee;
		readElement<edgeNodeInfo>(fs, ee);

// TODO TODO TODO
#if 1//DBG_PRINT_ALL
		DBG_DUMP("---- " << kk << ": <" << ee.sink << ", " << ee.paramID << ">\n");
#endif
		elem.insertDDDGEdge(kk, ee.sink, ee.paramID);
	}

// TODO TODO TODO
#if 1//DBG_PRINT_ALL
	DBG_DUMP("-- microops:\n");
#endif
	size_t microopsSize;
	readElement<size_t>(fs, microopsSize);
	for(size_t i = 0; i < microopsSize; i++) {
		int ee;
		readElement<int>(fs, ee);

// TODO TODO TODO
#if 1//DBG_PRINT_ALL
		DBG_DUMP("---- " << ee << "\n");
#endif
		elem.insertMicroop(ee);
	}
}

template<typename T> void ContextManager::skipElement(std::fstream &fs) {
	fs.seekg(sizeof(T), std::ios::cur);
}

void ContextManager::commit(char elemType, std::stringstream &ss, size_t totalFieldSize, std::string optID, unsigned optID2) {
	std::string toWrite(ss.str());
	size_t toWriteLen = toWrite.length();

	// Commit to file
	contextFile.write(&elemType, 1);
	// If optID is non-empty, we add it to the field. We also add optID2 even if it's unused
	if(optID != "") {
		size_t optIDLength = optID.length();
		totalFieldSize += sizeof(size_t) + optIDLength + sizeof(unsigned);
		contextFile.write((char *) &totalFieldSize, sizeof(size_t));
		contextFile.write((char *) &optIDLength, sizeof(size_t));
		contextFile.write(optID.c_str(), optIDLength);

		// Add optID2
		contextFile.write((char *) &optID2, sizeof(unsigned));
	}
	else {
		contextFile.write((char *) &totalFieldSize, sizeof(size_t));
	}
	contextFile.write(toWrite.c_str(), toWriteLen);
}

void ContextManager::commit(char elemType, std::stringstream &ss) {
	std::string toWrite(ss.str());
	size_t toWriteLen = toWrite.length();

	// Commit to file
	contextFile.write(&elemType, 1);
	contextFile.write(toWrite.c_str(), toWriteLen);
}

ContextManager::ContextManager() : fileName(args.outWorkDir + FILE_CONTEXT_MANAGER) {
	readOnly = false;
}

ContextManager::~ContextManager() {
	close();
}

void ContextManager::openForWrite() {
	close();
	contextFile.open(fileName, std::ios::out | std::ios::binary);

	assert(contextFile.is_open() && "Could not open context file");

	contextFile.write(FILE_CONTEXT_MANAGER_MAGIC_STRING, std::string(FILE_CONTEXT_MANAGER_MAGIC_STRING).size());

	readOnly = false;
}

void ContextManager::openForRead() {
	close();
	contextFile.open(fileName, std::ios::in | std::ios::binary);

	assert(contextFile.is_open() && "Could not open context file");

	char magicBits[4];
	contextFile.read(magicBits, std::string(FILE_CONTEXT_MANAGER_MAGIC_STRING).size());
	magicBits[3] = '\0';
	assert(std::string(magicBits) == FILE_CONTEXT_MANAGER_MAGIC_STRING && "Invalid or corrupt context file found");

	readOnly = true;
}

void ContextManager::close() {
	if(isOpen())
		contextFile.close();
}

bool ContextManager::isOpen() {
	return contextFile.is_open();
}

void ContextManager::saveProgressiveTraceInfo(long int &cursor, uint64_t &instCount) {
	assert(!readOnly && "Attempt to save progressive trace info on a read-only context manager");

	std::stringstream ss;
	writeElement<long int>(ss, cursor);
	writeElement<uint64_t>(ss, instCount);
	commit(ContextManager::TYPE_PROGRESSIVE_TRACE_INFO, ss);
}

void ContextManager::getProgressiveTraceInfo(long int *cursor, uint64_t *instCount) {
	assert(readOnly && "Attempt to read progressive trace info from a write-only context manager");
	assert(seekTo(ContextManager::TYPE_PROGRESSIVE_TRACE_INFO) && "Progressive trace info not found at the context manager");

	readElement<long int>(contextFile, *cursor);
	readElement<uint64_t>(contextFile, *instCount);

	assert(!(contextFile.eof()) && "Context file is incomplete or corrupt");
}

void ContextManager::saveLoopBoundInfo(wholeloopName2loopBoundMapTy &wholeloopName2loopBoundMap) {
	assert(!readOnly && "Attempt to save loop bound info on a read-only context manager");

// TODO TODO TODO
#if 1//DBG_PRINT_ALL
	DBG_DUMP("Dump of wholeloopName2loopBoundMap:\n");
	for(auto const &x : wholeloopName2loopBoundMap)
		DBG_DUMP("-- " << x.first << ": " << x.second << "\n");
#endif

	size_t totalFieldSize = 0;
	std::stringstream ss;
	totalFieldSize += writeElement<std::string, uint64_t>(ss, wholeloopName2loopBoundMap);
	commit(ContextManager::TYPE_LOOP_BOUND_INFO, ss, totalFieldSize);
}

void ContextManager::getLoopBoundInfo(wholeloopName2loopBoundMapTy *wholeloopName2loopBoundMap) {
	assert(readOnly && "Attempt to read loop bound info from a write-only context manager");
	assert(seekTo(ContextManager::TYPE_LOOP_BOUND_INFO) && "Loop bound info not found at the context manager");

	skipElement<size_t>(contextFile);
	readElement<std::string, uint64_t>(contextFile, *wholeloopName2loopBoundMap);

// TODO TODO TODO
#if 1//DBG_PRINT_ALL
	DBG_DUMP("Dump of wholeloopName2loopBoundMap:\n");
	for(auto const &x : *wholeloopName2loopBoundMap)
		DBG_DUMP("-- " << x.first << ": " << x.second << "\n");
#endif

	assert(!(contextFile.eof()) && "Context file is incomplete or corrupt");
}

void ContextManager::saveParsedTraceContainer(std::string wholeLoopName, ParsedTraceContainer &PC) {
	assert(!readOnly && "Attempt to save parsed trace container on a read-only context manager");

	size_t totalFieldSize = 0;
	std::stringstream ss;
	totalFieldSize += writeElement<ParsedTraceContainer>(ss, PC);
	commit(ContextManager::TYPE_PARSED_TRACE_CONTAINER, ss, totalFieldSize, wholeLoopName);
}

void ContextManager::getParsedTraceContainer(std::string wholeLoopName, ParsedTraceContainer *PC) {
	assert(readOnly && "Attempt to read parsed trace container from a write-only context manager");
	assert(seekToIdentified(ContextManager::TYPE_PARSED_TRACE_CONTAINER, wholeLoopName) && "Requested progressive trace container not found at the context manager");

	readElement<ParsedTraceContainer>(contextFile, *PC);

	assert(!(contextFile.eof()) && "Context file is incomplete or corrupt");
}

void ContextManager::saveDDDG(std::string wholeLoopName, unsigned datapathType, DDDGBuilder &builder, std::vector<int> &microops) {
	assert(!readOnly && "Attempt to save DDDG on a read-only context manager");

// TODO TODO TODO
#if 1//DBG_PRINT_ALL
	DBG_DUMP("Dump of DDDG:\n");
	DBG_DUMP("-- edgeTables.first:\n");
	for(auto const &x : builder.getEdgeTables().first)
		DBG_DUMP("---- " << x.first << ": <" << x.second.sink << ", " << x.second.paramID << ">\n");
	DBG_DUMP("-- edgeTables.second:\n");
	for(auto const &x : builder.getEdgeTables().second)
		DBG_DUMP("---- " << x.first << ": <" << x.second.sink << ", " << x.second.paramID << ">\n");
	DBG_DUMP("-- microops:\n");
	for(auto const &x : microops)
		DBG_DUMP("---- " << x << "\n");
#endif

	size_t totalFieldSize = 0;
	std::stringstream ss;
	std::pair<const u2eMMap, const u2eMMap> edgeTables = builder.getEdgeTables();
	totalFieldSize += writeElement<unsigned, edgeNodeInfo>(ss, const_cast<u2eMMap &>(edgeTables.first));
	totalFieldSize += writeElement<unsigned, edgeNodeInfo>(ss, const_cast<u2eMMap &>(edgeTables.second));
	totalFieldSize += writeElement<int>(ss, microops);
	commit(ContextManager::TYPE_DDDG, ss, totalFieldSize, wholeLoopName, datapathType);
}

void ContextManager::getDDDG(std::string wholeLoopName, unsigned datapathType, BaseDatapath *datapath) {
	assert(readOnly && "Attempt to read DDDG from a write-only context manager");
	assert(seekToIdentified(ContextManager::TYPE_DDDG, wholeLoopName, datapathType) && "Requested DDDG not found at the context manager");

	readElement<BaseDatapath>(contextFile, *datapath);

	assert(!(contextFile.eof()) && "Context file is incomplete or corrupt");
}

void ContextManager::saveOutBurstsInfo(std::string wholeLoopName, unsigned datapathType,
	std::unordered_map<std::string, outBurstInfoTy> &loadOutBurstsFound, std::unordered_map<std::string, outBurstInfoTy> &storeOutBurstsFound) {
// TODO TODO TODO
#if 1//DBG_PRINT_ALL
	DBG_DUMP("Dump of loadOutBurstsFound:\n");
#ifdef VAR_WSIZE
	for(auto const &x : loadOutBurstsFound)
		DBG_DUMP("-- " << x.first << ": <" << x.second.canOutBurst << ", " << x.second.isRegistered << ", " << x.second.baseAddress << ", " << x.second.offset << ", " << x.second.wordSize << ">\n");
#else
	for(auto const &x : loadOutBurstsFound)
		DBG_DUMP("-- " << x.first << ": <" << x.second.canOutBurst << ", " << x.second.isRegistered << ", " << x.second.baseAddress << ", " << x.second.offset << ">\n");
#endif
	DBG_DUMP("Dump of storeOutBurstsFound:\n");
#ifdef VAR_WSIZE
	for(auto const &x : storeOutBurstsFound)
		DBG_DUMP("-- " << x.first << ": <" << x.second.canOutBurst << ", " << x.second.isRegistered << ", " << x.second.baseAddress << ", " << x.second.offset << ", " << x.second.wordSize << ">\n");
#else
	for(auto const &x : storeOutBurstsFound)
		DBG_DUMP("-- " << x.first << ": <" << x.second.canOutBurst << ", " << x.second.isRegistered << ", " << x.second.baseAddress << ", " << x.second.offset << ">\n");
#endif
#endif

	assert(!readOnly && "Attempt to save out-bursts info on a read-only context manager");

	size_t totalFieldSize = 0;
	std::stringstream ss;
	totalFieldSize += writeElement<std::string, outBurstInfoTy>(ss, loadOutBurstsFound);
	totalFieldSize += writeElement<std::string, outBurstInfoTy>(ss, storeOutBurstsFound);
	commit(ContextManager::TYPE_OUTBURSTS_INFO, ss, totalFieldSize, wholeLoopName, datapathType);
}

void ContextManager::getOutBurstsInfo(std::string wholeLoopName, unsigned datapathType,
	std::unordered_map<std::string, outBurstInfoTy> *loadOutBurstsFound, std::unordered_map<std::string, outBurstInfoTy> *storeOutBurstsFound) {
	assert(readOnly && "Attempt to read out-bursts info from a write-only context manager");
	assert(seekToIdentified(ContextManager::TYPE_OUTBURSTS_INFO, wholeLoopName, datapathType) && "Requested out-bursts info not found at the context manager");

	readElement<std::string, outBurstInfoTy>(contextFile, *loadOutBurstsFound);
	readElement<std::string, outBurstInfoTy>(contextFile, *storeOutBurstsFound);

// TODO TODO TODO
#if 1//DBG_PRINT_ALL
	DBG_DUMP("Dump of loadOutBurstsFound:\n");
#ifdef VAR_WSIZE
	for(auto const &x : *loadOutBurstsFound)
		DBG_DUMP("-- " << x.first << ": <" << x.second.canOutBurst << ", " << x.second.isRegistered << ", " << x.second.baseAddress << ", " << x.second.offset << ", " << x.second.wordSize << ">\n");
#else
	for(auto const &x : *loadOutBurstsFound)
		DBG_DUMP("-- " << x.first << ": <" << x.second.canOutBurst << ", " << x.second.isRegistered << ", " << x.second.baseAddress << ", " << x.second.offset << ">\n");
#endif
	DBG_DUMP("Dump of storeOutBurstsFound:\n");
#ifdef VAR_WSIZE
	for(auto const &x : *storeOutBurstsFound)
		DBG_DUMP("-- " << x.first << ": <" << x.second.canOutBurst << ", " << x.second.isRegistered << ", " << x.second.baseAddress << ", " << x.second.offset << ", " << x.second.wordSize << ">\n");
#else
	for(auto const &x : *storeOutBurstsFound)
		DBG_DUMP("-- " << x.first << ": <" << x.second.canOutBurst << ", " << x.second.isRegistered << ", " << x.second.baseAddress << ", " << x.second.offset << ">\n");
#endif
#endif

	assert(!(contextFile.eof()) && "Context file is incomplete or corrupt");
}

void ContextManager::saveGlobalDDRMap(std::unordered_map<std::string, std::vector<ddrInfoTy>> &globalDDRMap) {
	assert(!readOnly && "Attempt to save global DDR map on a read-only context manager");

// TODO TODO TODO
#if 1//DBG_PRINT_ALL
	DBG_DUMP("Dump of globalDDRMap:\n");
	for(auto const &x : globalDDRMap) {
		DBG_DUMP("-- " << x.first << "\n");
		for(auto const &y : x.second) {
			DBG_DUMP("---- " << y.loopLevel << " " << y.datapathType << "\n");
			DBG_DUMP("---- arraysLoaded:\n");
			for(auto const &z: y.arraysLoaded)
				DBG_DUMP("------ " << z << "\n");
			DBG_DUMP("---- arraysStored:\n");
			for(auto const &z: y.arraysStored)
				DBG_DUMP("------ " << z << "\n");
		}
	}
#endif

	size_t totalFieldSize = 0;
	std::stringstream ss;
	totalFieldSize += writeElement<std::string, ddrInfoTy>(ss, globalDDRMap);
	commit(ContextManager::TYPE_GLOBAL_DDR_MAP, ss, totalFieldSize);
}

void ContextManager::getGlobalDDRMap(std::unordered_map<std::string, std::vector<ddrInfoTy>> *globalDDRMap) {
	assert(readOnly && "Attempt to read global DDR map from a write-only context manager");
	assert(seekTo(ContextManager::TYPE_GLOBAL_DDR_MAP) && "Requested global DDR map not found at the context manager");

	skipElement<size_t>(contextFile);
	readElement<std::string, ddrInfoTy>(contextFile, *globalDDRMap);

// TODO TODO TODO
#if 1//DBG_PRINT_ALL
	DBG_DUMP("Dump of globalDDRMap:\n");
	for(auto const &x : *globalDDRMap) {
		DBG_DUMP("-- " << x.first << "\n");
		for(auto const &y : x.second) {
			DBG_DUMP("---- " << y.loopLevel << " " << y.datapathType << "\n");
			DBG_DUMP("---- arraysLoaded:\n");
			for(auto const &z: y.arraysLoaded)
				DBG_DUMP("------ " << z << "\n");
			DBG_DUMP("---- arraysStored:\n");
			for(auto const &z: y.arraysStored)
				DBG_DUMP("------ " << z << "\n");
		}
	}
#endif

	assert(!(contextFile.eof()) && "Context file is incomplete or corrupt");
}

#ifdef CROSS_DDDG_PACKING
void ContextManager::saveRemainingBudgetInfo(std::string wholeLoopName, unsigned datapathType,
	std::unordered_map<std::string, remainingBudgetTy> &loadRemainingBudget, std::unordered_map<std::string, remainingBudgetTy> &storeRemainingBudget) {
// TODO TODO TODO
#if 1//DBG_PRINT_ALL
	DBG_DUMP("Dump of loadRemainingBudget:\n");
	for(auto const &x : loadRemainingBudget)
		DBG_DUMP("-- " << x.first << ": <" << x.second.isValid << ", " << x.second.remainingBudget << ", " << x.second.baseAddress << ", " << x.second.offset << ">\n");
	DBG_DUMP("Dump of storeRemainingBudget:\n");
	for(auto const &x : storeRemainingBudget)
		DBG_DUMP("-- " << x.first << ": <" << x.second.isValid << ", " << x.second.remainingBudget << ", " << x.second.baseAddress << ", " << x.second.offset << ">\n");
#endif

	assert(!readOnly && "Attempt to save remaining budget info on a read-only context manager");

	size_t totalFieldSize = 0;
	std::stringstream ss;
	totalFieldSize += writeElement<std::string, remainingBudgetTy>(ss, loadRemainingBudget);
	totalFieldSize += writeElement<std::string, remainingBudgetTy>(ss, storeRemainingBudget);
	commit(ContextManager::TYPE_REMAINING_BUDGET_INFO, ss, totalFieldSize, wholeLoopName, datapathType);
}

void ContextManager::getRemainingBudgetInfo(std::string wholeLoopName, unsigned datapathType,
	std::unordered_map<std::string, remainingBudgetTy> *loadRemainingBudget, std::unordered_map<std::string, remainingBudgetTy> *storeRemainingBudget) {
	assert(readOnly && "Attempt to read remaining budget info from a write-only context manager");
	assert(seekToIdentified(ContextManager::TYPE_REMAINING_BUDGET_INFO, wholeLoopName, datapathType) && "Requested remaining budget info not found at the context manager");

	readElement<std::string, remainingBudgetTy>(contextFile, *loadRemainingBudget);
	readElement<std::string, remainingBudgetTy>(contextFile, *storeRemainingBudget);

// TODO TODO TODO
#if 1//DBG_PRINT_ALL
	DBG_DUMP("Dump of loadRemainingBudget:\n");
	for(auto const &x : *loadRemainingBudget)
		DBG_DUMP("-- " << x.first << ": <" << x.second.isValid << ", " << x.second.remainingBudget << ", " << x.second.baseAddress << ", " << x.second.offset << ">\n");
	DBG_DUMP("Dump of storeRemainingBudget:\n");
	for(auto const &x : *storeRemainingBudget)
		DBG_DUMP("-- " << x.first << ": <" << x.second.isValid << ", " << x.second.remainingBudget << ", " << x.second.baseAddress << ", " << x.second.offset << ">\n");
#endif

	assert(!(contextFile.eof()) && "Context file is incomplete or corrupt");
}
#endif
