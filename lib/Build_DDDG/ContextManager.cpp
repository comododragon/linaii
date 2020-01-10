#include "profile_h/ContextManager.h"

#include <sstream>

#include "profile_h/BaseDatapath.h"

const std::unordered_map<int, ContextManager::cfd_t> ContextManager::typeMap = {
	{ContextManager::TYPE_PROGRESSIVE_TRACE_INFO, cfd_t(sizeof(long int) + sizeof(uint64_t))},
	{ContextManager::TYPE_LOOP_BOUND_INFO, cfd_t(-1)},
	{ContextManager::TYPE_PARSED_TRACE_CONTAINER, cfd_t(-1)},
	{ContextManager::TYPE_DDDG, cfd_t(-1)}
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
		DBG_DUMP(type << " " << ID << " " << foundTheRightOne << " " << readID << " " << readIDFirst << "\n");
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

			DBG_DUMP(ID << " " << readID << "\n");

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

template<typename E> size_t ContextManager::writeElement(std::stringstream &ss, std::vector<E> &elem) {
	size_t writtenSize = 0;
	size_t elementSize = elem.size();

	writtenSize += writeElement<size_t>(ss, elementSize);
	for(auto &it : elem)
		writtenSize += writeElement<E>(ss, it);

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

template<> void ContextManager::readElement<ParsedTraceContainer>(std::fstream &fs, ParsedTraceContainer &elem) {
	elem = ParsedTraceContainer(elem.getKernelName());

	elem.openAndClearAllFiles();

	size_t funcListSize;
	readElement<size_t>(fs, funcListSize);
	for(size_t i = 0; i < funcListSize; i++) {
		std::string ee;
		readElement<std::string>(fs, ee);
		DBG_DUMP(ee << "\n");

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
}

template<> void ContextManager::readElement<BaseDatapath>(std::fstream &fs, BaseDatapath &elem) {
	elem.setForDDDGImport();

	size_t edgeListFirstSize;
	readElement<size_t>(fs, edgeListFirstSize);
	for(size_t i = 0; i < edgeListFirstSize; i++) {
		unsigned kk;
		readElement<unsigned>(fs, kk);
		edgeNodeInfo ee;
		readElement<edgeNodeInfo>(fs, ee);

		elem.insertDDDGEdge(kk, ee.sink, ee.paramID);
	}

	size_t edgeListSecondSize;
	readElement<size_t>(fs, edgeListSecondSize);
	for(size_t i = 0; i < edgeListSecondSize; i++) {
		unsigned kk;
		readElement<unsigned>(fs, kk);
		edgeNodeInfo ee;
		readElement<edgeNodeInfo>(fs, ee);

		elem.insertDDDGEdge(kk, ee.sink, ee.paramID);
	}

	size_t microopsSize;
	readElement<size_t>(fs, microopsSize);
	for(size_t i = 0; i < microopsSize; i++) {
		int ee;
		readElement<int>(fs, ee);

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

	size_t totalFieldSize = 0;
	std::stringstream ss;
	std::pair<const u2eMMap, const u2eMMap> edgeTables = builder.getEdgeTables();
	totalFieldSize += writeElement<unsigned, edgeNodeInfo>(ss, const_cast<u2eMMap &>(edgeTables.first));
	totalFieldSize += writeElement<unsigned, edgeNodeInfo>(ss, const_cast<u2eMMap &>(edgeTables.second));
	totalFieldSize += writeElement<int>(ss, microops);
	commit(ContextManager::TYPE_DDDG, ss, totalFieldSize, wholeLoopName, datapathType);
}

void ContextManager::getDDDG(std::string wholeLoopName, unsigned datapathType, BaseDatapath &datapath) {
	assert(readOnly && "Attempt to read DDDG from a write-only context manager");
	assert(seekToIdentified(ContextManager::TYPE_DDDG, wholeLoopName, datapathType) && "Requested DDDG not found at the context manager");

	readElement<BaseDatapath>(contextFile, datapath);

	assert(!(contextFile.eof()) && "Context file is incomplete or corrupt");
}

// XXX LEgacy code
#if 0
	assert(!readOnly && "Attempt to save parsed trace container on a read-only context manager");

	char elemType = ContextManager::TYPE_PARSED_TRACE_CONTAINER;
	size_t totalFieldSize = 0;
	std::stringstream ss;
	size_t wholeLoopNameSize = wholeLoopName.size();

	// Write whole loop name
	ss.write((char *) &wholeLoopNameSize, sizeof(size_t));
	totalFieldSize += sizeof(size_t);
	ss.write(wholeLoopName.c_str(), wholeLoopNameSize);
	totalFieldSize += wholeLoopNameSize;

	// Now writing funcList

	const std::vector<std::string> &funcList = PC.getFuncList();
	size_t funcListSize = funcList.size();

	ss.write((char *) &funcListSize, sizeof(size_t));
	totalFieldSize += sizeof(size_t);

	for(auto &it : funcList) {
		size_t contentLength = it.length();

		// Write the content length
		ss.write((char *) &contentLength, sizeof(size_t));
		totalFieldSize += sizeof(size_t);

		// Write the content
		ss.write(it.c_str(), contentLength);
		totalFieldSize += contentLength;
	}

	// Now writing instIDList

	const std::vector<std::string> &instIDList = PC.getInstIDList();
	size_t instIDListSize = instIDList.size();

	ss.write((char *) &instIDListSize, sizeof(size_t));
	totalFieldSize += sizeof(size_t);

	for(auto &it : instIDList) {
		size_t contentLength = it.length();

		// Write the content length
		ss.write((char *) &contentLength, sizeof(size_t));
		totalFieldSize += sizeof(size_t);

		// Write the content
		ss.write(it.c_str(), contentLength);
		totalFieldSize += contentLength;
	}

	// Now writing lineNoList

	const std::vector<int> &lineNoList = PC.getLineNoList();
	size_t lineNoListSize = lineNoList.size();

	ss.write((char *) &lineNoListSize, sizeof(size_t));
	totalFieldSize += sizeof(size_t);

	for(auto &it : lineNoList) {
		// Write the content
		ss.write((char *) &it, sizeof(int));
		totalFieldSize += sizeof(int);
	}

	// Now writing memoryTraceList

	const std::unordered_map<int, std::pair<int64_t, unsigned>> &memoryTraceList = PC.getMemoryTraceList();
	size_t memoryTraceListSize = memoryTraceList.size();

	ss.write((char *) &memoryTraceListSize, sizeof(size_t));
	totalFieldSize += sizeof(size_t);

	for(auto &it : memoryTraceList) {
		// Write the key
		ss.write((char *) &(it.first), sizeof(int));
		totalFieldSize += sizeof(int);

		// Write the content (first)
		ss.write((char *) &(it.second.first), sizeof(int64_t));
		totalFieldSize += sizeof(int64_t);

		// Write the content (second)
		ss.write((char *) &(it.second.second), sizeof(unsigned));
		totalFieldSize += sizeof(unsigned);
	}

	// Now writing getElementPtrList

	const std::unordered_map<int, std::pair<std::string, int64_t>> &getElementPtrList = PC.getGetElementPtrList();
	size_t getElementPtrListSize = getElementPtrList.size();

	ss.write((char *) &getElementPtrListSize, sizeof(size_t));
	totalFieldSize += sizeof(size_t);

	for(auto &it : getElementPtrList) {
		size_t contentFirstLength = it.second.first.length();

		// Write the key
		ss.write((char *) &(it.first), sizeof(int));
		totalFieldSize += sizeof(int);

		// Write the content (first) length
		ss.write((char *) &contentFirstLength, sizeof(size_t));
		totalFieldSize += sizeof(size_t);
		// Write the content (first)
		ss.write(it.second.first.c_str(), contentFirstLength);
		totalFieldSize += contentFirstLength;

		// Write the content (second)
		ss.write((char *) &(it.second.second), sizeof(int64_t));
		totalFieldSize += sizeof(int64_t);
	}

	// Now writing prevBBList

	const std::vector<std::string> &prevBBList = PC.getPrevBBList();
	size_t prevBBListSize = prevBBList.size();

	ss.write((char *) &prevBBListSize, sizeof(size_t));
	totalFieldSize += sizeof(size_t);

	for(auto &it : prevBBList) {
		size_t contentLength = it.length();

		// Write the content length
		ss.write((char *) &contentLength, sizeof(size_t));
		totalFieldSize += sizeof(size_t);

		// Write the content
		ss.write(it.c_str(), contentLength);
		totalFieldSize += contentLength;
	}

	// Now writing currBBList

	const std::vector<std::string> &currBBList = PC.getCurrBBList();
	size_t currBBListSize = currBBList.size();

	ss.write((char *) &currBBListSize, sizeof(size_t));
	totalFieldSize += sizeof(size_t);

	for(auto &it : currBBList) {
		size_t contentLength = it.length();

		// Write the content length
		ss.write((char *) &contentLength, sizeof(size_t));
		totalFieldSize += sizeof(size_t);

		// Write the content
		ss.write(it.c_str(), contentLength);
		totalFieldSize += contentLength;
	}

	std::string toWrite(ss.str());
	size_t toWriteLen = toWrite.length();

	// Commit to file
	contextFile.write(&elemType, 1);
	contextFile.write((char *) &totalFieldSize, sizeof(size_t));
	contextFile.write(toWrite.c_str(), toWriteLen);
}
#endif

#if 0
	assert(readOnly && "Attempt to read parsed trace container from a write-only context manager");

	size_t totalFieldSize;
	size_t readWholeLoopNameSize;
	std::string readWholeLoopName = "";
	std::string readWholeLoopNameFirst = "none";
	bool foundTheRightOne = false;

	// Find the right PC for the right whole loop name (if any)
	while(!foundTheRightOne) {
		if(seekTo(ContextManager::TYPE_PARSED_TRACE_CONTAINER)) {
			contextFile.read((char *) &totalFieldSize, sizeof(size_t));
			contextFile.read((char *) &readWholeLoopNameSize, sizeof(size_t));

			assert(readWholeLoopNameSize < BUFF_STR_SZ && "A read of more than BUFF_STR_SZ bytes was requested from context file and it was blocked. If you believe that this was not caused by a corrupt context file, increase BUFF_STR_SZ");

			char buff[BUFF_STR_SZ];
			contextFile.read(buff, readWholeLoopNameSize);
			buff[readWholeLoopNameSize] = '\0';
			readWholeLoopName.assign(buff);

			if(wholeLoopName == readWholeLoopName)
				foundTheRightOne = true;
			else
				contextFile.seekg(totalFieldSize - sizeof(size_t) - readWholeLoopNameSize, std::ios::cur);

			if("none" == readWholeLoopNameFirst)
				readWholeLoopNameFirst.assign(readWholeLoopName);
			else if(readWholeLoopNameFirst == readWholeLoopName)
				break;
		}
		else {
			break;
		}
	}

	assert(foundTheRightOne && "Requested progressive trace container not found at the context manager");

	PC = ParsedTraceContainer();
	PC->openAndClearAllFiles();

	// Now reading funcList

	size_t funcListSize;
	contextFile.read((char *) &funcListSize, sizeof(size_t));

	for(size_t i = 0; i < funcListSize; i++) {
		size_t contentLength = 0;
		contextFile.read((char *) &contentLength, sizeof(size_t));

		assert(contentLength < BUFF_STR_SZ && "A read of more than BUFF_STR_SZ bytes was requested from context file and it was blocked. If you believe that this was not caused by a corrupt context file, increase BUFF_STR_SZ");

		char buff[BUFF_STR_SZ];
		contextFile.read(buff, contentLength);
		buff[contentLength] = '\0';
		std::string content(buff);

		PC->appendToFuncList(content);
	}

	assert(!(contextFile.eof()) && "Context file is incomplete or corrupt");
}
#endif
