// Loosely based on https://gist.github.com/alexdlaird/3100f8c7c96871c5b94e


#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <zlib.h>
#include <vector>


// Uncomment line below if you want the buffered ranges to be printed at end of this daemon
//#define LOG_RANGES
// Uncomment line below if you want all log notices
//#define LOG_VERBOSE


#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/vector.hpp>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif


#define SHARED_MEMORY_NAME "LinadSharedMemory."
#define SHARED_MEMORY_MAX_SIZE 1073741824
#define PIPE_PATH "/tmp/linad.pipe."
#define BUFF_STR_SZ 999
#define KEY_SEPARATOR "?"
#define FILE_FUTURE_CACHE_MAGIC_STRING "!BU"
#define SHARED_DYNAMIC_TRACE_CONNECTION_SIZE 2048


#ifdef LOG_VERBOSE
#define VERBOSE_LOG(...) do {\
	syslog(LOG_NOTICE, __VA_ARGS__);\
} while(0)
#else
#define VERBOSE_LOG(...) do {\
} while(0)
#endif


/* Shared memory region used for the memory trace cache */
typedef boost::interprocess::allocator<uint64_t, boost::interprocess::managed_shared_memory::segment_manager> ShmAllocator;
typedef boost::interprocess::vector<uint64_t, ShmAllocator> MemoryTraceElementTy;


/*Shared memory regions used for dynamic trace cache connections */
typedef boost::interprocess::allocator<char, boost::interprocess::managed_shared_memory::segment_manager> ByteShmAllocator;
typedef boost::interprocess::vector<char, ByteShmAllocator> ConnectionRegionTy;


/* Buffered GZ File */
/* This class opens a GZ file, and keeps on memory all regions that have been read for fast access */
/* That is, it is better to keep each instance of this class only reading small regions of the file, otherwise the buffer may be huge! */
class BufferedGzFile {
	/* The region from GZ file to be buffered */
	char *buffer;

	/* Base cursor: this class supplies a special method that rewinds the buffer to the base cursor whenever called */
	z_off_t baseCursor;
	/* Lower bound cursor of the buffered region */
	z_off_t lowerCursor;
	/* Upper bound cursor of the buffered region */
	z_off_t higherCursor;
	/* Current cursor: points to the next character to be read from buffer or GZ file */
	z_off_t curCursor;
	/* Proposed cursor: either points to current cursor, or to an arbitrary position defined via rewind(), seek() or seekToBase() */
	z_off_t proposedCursor;

	/* True if EOF is on buffered region */
	bool eofFound;

	/* The so famous GZ file */
	gzFile file;

	/* When called, it forces invalidation of the buffer, forcing the next gets() call to refresh the buffer (i.e. read from GZ file) */
	void _invalidateBuffer() {
		if(buffer)
			free(buffer);
		buffer = NULL;
		eofFound = false;
	}

	/* Internal gets() method. This method assumes that all needed data is already on buffered cache */
	/* Then it just proceeds as a normal gets() call would */
	char *_gets(char *buf, int len) {
		// The beginning of gets() call guarantees that proposedCursor will be within the buffered region
		// That is, proposedCursor should never be above higherCursor
		if(proposedCursor >= higherCursor) {
			_invalidateBuffer();
			return Z_NULL;
		}

		// Also the logic before this call guarantees that there will be enough characters in buffer
		// to fulfill this function
		z_off_t idx;
		for(idx = proposedCursor; idx < higherCursor && (idx - proposedCursor) < (len - 1); idx++) {
			// If a newline was found, we found our match
			if('\n' == buffer[idx - lowerCursor]) {
				idx++;
				break;
			}
		}

		// Copy content and close string
		memcpy(buf, &buffer[proposedCursor - lowerCursor], idx - proposedCursor);
		buf[idx - proposedCursor] = '\0';

		// Update cursors
		proposedCursor = idx;
		curCursor = idx;

		return buf;
	}

public:

	const unsigned defaultBufferSize = 131072;

	BufferedGzFile(std::string path, z_off_t baseCursor) {
		buffer = NULL;

		this->baseCursor = baseCursor;
		curCursor = 0;
		proposedCursor = 0;

		eofFound = false;

		file = gzopen(path.c_str(), "r");
	}

	/* File is closed on object destruction. Beware of local instances and scoping! */
	~BufferedGzFile() {
		if(file)
			gzclose(file);
		if(buffer)
			free(buffer);
	}

	bool isOpen() {
		return file;
	}

	std::pair<z_off_t, z_off_t> bufferBounds() {
		if(buffer)
			return std::make_pair(lowerCursor, higherCursor);
		else
			return std::make_pair(0, 0);
	}

	z_off_t bufferSize() {
		return buffer? (higherCursor - lowerCursor) : 0;
	}

	z_off_t seek(z_off_t cursor, int whence) {
		if(whence != SEEK_SET && whence != SEEK_CUR)
			return -1;

		if(SEEK_CUR == whence)
			proposedCursor += cursor;
		else
			proposedCursor = cursor;

		return proposedCursor;
	}

	z_off_t seekToBase() {
		return seek(baseCursor, SEEK_SET);
	}

	int rewind() {
		proposedCursor = 0;
		return 0;
	}

	int eof() {
		return eofFound? (curCursor >= higherCursor) : 0;
	}

	z_off_t tell() {
		return proposedCursor;
	}

	char *gets(char *buf, int len) {
		if(buffer) {
			bool readjustBuffer = false;
			if(proposedCursor != curCursor) {
				// If proposed cursor is out of bounds of buffer, readjust buffer
				if(proposedCursor < lowerCursor) {
					lowerCursor = proposedCursor;
					readjustBuffer = true;
				}
				// Only readjust up if EOF has not been reached
				if(!eofFound && (proposedCursor > higherCursor)) {
					higherCursor = proposedCursor;
					readjustBuffer = true;
				}
			}
			// If buffer has no remaining len-1 elements to be read, readjust buffer
			// Only readjust up if EOF has not been reached
			if(!eofFound && ((proposedCursor + (len - 1)) > higherCursor)) {
				higherCursor = proposedCursor + defaultBufferSize;
				readjustBuffer = true;
			}

			// If buffer must be readjusted, re-read everything
			// XXX Perhaps implement this a better way if needed...
			if(readjustBuffer) {
				char *newBuffer = (char *) realloc(buffer, higherCursor - lowerCursor);
				if(!newBuffer) {
					_invalidateBuffer();
					return Z_NULL;
				}
				buffer = newBuffer;
				// Adjust the true cursor before reading
				int retVal = gzseek(file, lowerCursor, SEEK_SET);
				if(-1 == retVal) {
					_invalidateBuffer();
					return Z_NULL;
				}
				retVal = gzread(file, buffer, higherCursor - lowerCursor);
				if(-1 == retVal) {
					_invalidateBuffer();
					return Z_NULL;
				}
				else if(retVal < (higherCursor - lowerCursor)) {
					eofFound = true;
					higherCursor = proposedCursor + retVal;
				}
				curCursor = proposedCursor;
			}

			return _gets(buf, len);
		}
		/* No buffered data yet or it has been invalidated. Refresh buffer */
		else {
			buffer = (char *) malloc(defaultBufferSize);
			if(!buffer)
				return Z_NULL;
			// Adjust the true cursor before reading
			int retVal = gzseek(file, proposedCursor, SEEK_SET);
			if(-1 == retVal) {
				_invalidateBuffer();
				return Z_NULL;
			}
			retVal = gzread(file, buffer, defaultBufferSize);
			if(-1 == retVal) {
				_invalidateBuffer();
				return Z_NULL;
			}
			else if(retVal < defaultBufferSize) {
				eofFound = true;
			}
			lowerCursor = proposedCursor;
			higherCursor = proposedCursor + (eofFound? retVal : defaultBufferSize);
			curCursor = proposedCursor;

			return _gets(buf, len);
		}
	}
};


int main(int argc, char *argv[]) {
	int retVal = EXIT_SUCCESS;
	int pipeFD = -1;

	int pipeID = 0;
	if(2 == argc)
		pipeID = atoi(argv[1]);

	pid_t pid = fork();
	if(pid > 0)
		return EXIT_SUCCESS;
	else if(pid < 0)
		return EXIT_FAILURE;

	std::string fullSharedMemoryName(SHARED_MEMORY_NAME + std::to_string(pipeID));

	struct shm_remove {
		std::string fullSharedMemoryName;
		shm_remove(std::string fullSharedMemoryName) : fullSharedMemoryName(fullSharedMemoryName) {
			boost::interprocess::shared_memory_object::remove(fullSharedMemoryName.c_str());
		}
		~shm_remove() {
			boost::interprocess::shared_memory_object::remove(fullSharedMemoryName.c_str());
		}
	} remover(fullSharedMemoryName);

	boost::interprocess::managed_shared_memory segment(boost::interprocess::create_only, fullSharedMemoryName.c_str(), SHARED_MEMORY_MAX_SIZE);
	const ShmAllocator allocInst(segment.get_segment_manager());
	const ByteShmAllocator byteAllocInst(segment.get_segment_manager());
	std::vector<std::string> keys;
	std::string dynamicTraceFileName;
	std::vector<std::string> connections;
	std::map<std::string, std::string> attachedConnections;
	std::map<std::string, BufferedGzFile *> dynamicTraceFiles;
	syslog(LOG_NOTICE, "Succesfully created managed shared memory");

	std::string pipePath(PIPE_PATH + std::to_string(pipeID));
	syslog(LOG_NOTICE, "Pipe path: %s", pipePath.c_str());

	umask(0);

	openlog("linad", LOG_NOWAIT | LOG_PID, LOG_USER);
	syslog(LOG_NOTICE, "Started");

	pid_t sid = setsid();
	if(sid < 0) {
		syslog(LOG_ERR, "Failed to generate a session ID");
		retVal = EXIT_FAILURE;
		goto _err;
	}

	if(chdir("/") < 0) {
		syslog(LOG_ERR, "Failed to cd to system root folder");
		retVal = EXIT_FAILURE;
		goto _err;
	}

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	unlink(pipePath.c_str());
	if(mkfifo(pipePath.c_str(), 0644) < 0) {
		syslog(LOG_ERR, "Failed to mkfifo(): %s: %s", pipePath.c_str(), strerror(errno));
		retVal = EXIT_FAILURE;
		goto _err;
	}
	if((pipeFD = open(pipePath.c_str(), O_RDWR)) < 0) {
		syslog(LOG_ERR, "Failed to open(): %s: %s", pipePath.c_str(), strerror(errno));
		retVal = EXIT_FAILURE;
		goto _err;
	}

	/* We are now a daemon! */

	char buff[13];
	while(ssize_t nBytes = read(pipeFD, buff, 1)) {
		if(nBytes < 0) {
			syslog(LOG_ERR, "read() call failed");
			retVal = EXIT_FAILURE;
			goto _err;
		}

		// Load memory trace
		// lXXXYYYYYYYYYYY...
		// XXX            Size of YYYYYYYYYYY... string
		// YYYYYYYYYYY... Memory trace file name
		if('l' == buff[0]) {
			nBytes = read(pipeFD, buff, 3);
			if(nBytes != 3) {
				syslog(LOG_ERR, "read() call failed when getting the memory trace filename string size");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			buff[3] = '\0';
			unsigned fileNameSz = 0;
			try {
				fileNameSz = std::stoi(buff);
			}
			catch(const std::exception& e) {
				syslog(LOG_ERR, "Failed to convert memory trace filename string size to int");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			if(fileNameSz > BUFF_STR_SZ)
				fileNameSz = BUFF_STR_SZ;

			char fileName[BUFF_STR_SZ + 1];
			fileName[BUFF_STR_SZ] = '\0';
			nBytes = read(pipeFD, fileName, fileNameSz);
			if(nBytes != fileNameSz) {
				syslog(LOG_ERR, "read() call failed when getting the memory trace filename string");
				retVal = EXIT_FAILURE;
				goto _err;
			}
			fileName[fileNameSz] = '\0';

			syslog(LOG_NOTICE, "Received memory trace file to load: %s", fileName);

			syslog(LOG_NOTICE, "Cleaning previous memory trace (if any)");
			for(auto elem : keys)
				segment.destroy<MemoryTraceElementTy>(elem.c_str());
			keys.clear();

			std::ifstream traceShortFile;
			std::string bufferedWholeLoopName = "";

			traceShortFile.open(fileName, std::ios::binary);
			if(!(traceShortFile.is_open())) {
				syslog(LOG_ERR, "Failed to open the supplied memory trace filename");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			while(!(traceShortFile.eof())) {
				size_t bufferSz;
				char buffer[BUFF_STR_SZ];
				size_t addrVecSize;

				if(!(traceShortFile.read((char *) &bufferSz, sizeof(size_t))))
					break;
				if(bufferSz >= BUFF_STR_SZ) {
					traceShortFile.close();
					syslog(LOG_ERR, "Buffer string not big enough!");
					retVal = EXIT_FAILURE;
					goto _err;
				}
				traceShortFile.read(buffer, bufferSz);
				buffer[bufferSz] = '\0';
				bufferedWholeLoopName.assign(buffer);

				if(!(traceShortFile.read((char *) &bufferSz, sizeof(size_t))))
					break;
				if(bufferSz >= BUFF_STR_SZ) {
					traceShortFile.close();
					syslog(LOG_ERR, "Buffer string not big enough!");
					retVal = EXIT_FAILURE;
					goto _err;
				}
				traceShortFile.read(buffer, bufferSz);
				buffer[bufferSz] = '\0';

				std::string wholeLoopNameInstNamePair = bufferedWholeLoopName + KEY_SEPARATOR + std::string(buffer);
				if(std::find(keys.begin(), keys.end(), wholeLoopNameInstNamePair) != keys.end()) {
					traceShortFile.close();
					syslog(LOG_ERR, "Duplicate elements found in memory trace");
					retVal = EXIT_FAILURE;
					goto _err;
				}
				keys.push_back(wholeLoopNameInstNamePair);
				MemoryTraceElementTy *addrVec = segment.construct<MemoryTraceElementTy>(wholeLoopNameInstNamePair.c_str())(allocInst);

				traceShortFile.read((char *) &addrVecSize, sizeof(size_t));
				addrVec->resize(addrVecSize);

				traceShortFile.read((char *) addrVec->data(), addrVecSize * sizeof(uint64_t));
			}

			traceShortFile.close();

			syslog(LOG_NOTICE, "Finished loading memory trace to shared memory!");
		}
		// Load dynamic trace
		// dXXXYYYYYYYYYYY...
		// XXX            Size of YYYYYYYYYYY... string
		// YYYYYYYYYYY... Dynamic trace file name
		else if('d' == buff[0]) {
			nBytes = read(pipeFD, buff, 3);
			if(nBytes != 3) {
				syslog(LOG_ERR, "read() call failed when getting the dynamic trace filename string size");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			buff[3] = '\0';
			unsigned fileNameSz = 0;
			try {
				fileNameSz = std::stoi(buff);
			}
			catch(const std::exception& e) {
				syslog(LOG_ERR, "Failed to convert dynamic trace filename string size to int");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			if(fileNameSz > BUFF_STR_SZ)
				fileNameSz = BUFF_STR_SZ;

			char fileName[BUFF_STR_SZ + 1];
			fileName[BUFF_STR_SZ] = '\0';
			nBytes = read(pipeFD, fileName, fileNameSz);
			if(nBytes != fileNameSz) {
				syslog(LOG_ERR, "read() call failed when getting the dynamic trace filename string");
				retVal = EXIT_FAILURE;
				goto _err;
			}
			fileName[fileNameSz] = '\0';

			dynamicTraceFileName.assign(fileName);
			syslog(LOG_NOTICE, "Received dynamic trace file to use in future commands: %s", fileName);

			if(dynamicTraceFiles.size()) {
				syslog(LOG_NOTICE, "Closing previous open dynamic trace files...");
				for(auto &elem : dynamicTraceFiles)
					delete elem.second;
				dynamicTraceFiles.clear();
				syslog(LOG_NOTICE, "Done");
			}

			if(connections.size()) {
				syslog(LOG_NOTICE, "Closing previous connections...");
				for(auto &elem : connections)
					segment.destroy<ConnectionRegionTy>(elem.c_str());
				connections.clear();
				syslog(LOG_NOTICE, "Done");
			}

			attachedConnections.clear();
		}
		// Create a connection
		// cXXXYYYYYYYYYYY...
		// XXX            Size of YYYYYYYYYYY... string
		// YYYYYYYYYYY... Connection name
		else if('c' == buff[0]) {
			nBytes = read(pipeFD, buff, 3);
			if(nBytes != 3) {
				syslog(LOG_ERR, "read() call failed when getting the connection name string size");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			buff[3] = '\0';
			unsigned connectionNameSz = 0;
			try {
				connectionNameSz = std::stoi(buff);
			}
			catch(const std::exception& e) {
				syslog(LOG_ERR, "Failed to convert connection name string size to int");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			if(connectionNameSz > BUFF_STR_SZ)
				connectionNameSz = BUFF_STR_SZ;

			char connectionName[BUFF_STR_SZ + 1];
			connectionName[BUFF_STR_SZ] = '\0';
			nBytes = read(pipeFD, connectionName, connectionNameSz);
			if(nBytes != connectionNameSz) {
				syslog(LOG_ERR, "read() call failed when getting the connection name string");
				retVal = EXIT_FAILURE;
				goto _err;
			}
			connectionName[connectionNameSz] = '\0';

			VERBOSE_LOG("Attempt to connect");
			if(std::find(connections.begin(), connections.end(), connectionName) != connections.end()) {
				syslog(LOG_NOTICE, "Connection already exists with following name, nothing will change: %s", connectionName);
			}
			else {
				VERBOSE_LOG("Received following connection name: %s", connectionName);

				VERBOSE_LOG("Creating connection...");
				ConnectionRegionTy *connection = segment.construct<ConnectionRegionTy>(connectionName)(byteAllocInst);
				if(connection) {
					connection->resize(SHARED_DYNAMIC_TRACE_CONNECTION_SIZE);
					connections.push_back(connectionName);
				}
				else {
					syslog(LOG_ERR, "Failed to construct connection region");
					retVal = EXIT_FAILURE;
					goto _err;
				}
				VERBOSE_LOG("Done");
			}
		}
		// Disconnect
		// xXXXYYYYYYYYYYY...
		// XXX            Size of YYYYYYYYYYY... string
		// YYYYYYYYYYY... Connection name
		else if('x' == buff[0]) {
			nBytes = read(pipeFD, buff, 3);
			if(nBytes != 3) {
				syslog(LOG_ERR, "read() call failed when getting the connection name string size");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			buff[3] = '\0';
			unsigned connectionNameSz = 0;
			try {
				connectionNameSz = std::stoi(buff);
			}
			catch(const std::exception& e) {
				syslog(LOG_ERR, "Failed to convert connection name string size to int");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			if(connectionNameSz > BUFF_STR_SZ)
				connectionNameSz = BUFF_STR_SZ;

			char connectionName[BUFF_STR_SZ + 1];
			connectionName[BUFF_STR_SZ] = '\0';
			nBytes = read(pipeFD, connectionName, connectionNameSz);
			if(nBytes != connectionNameSz) {
				syslog(LOG_ERR, "read() call failed when getting the connection name string");
				retVal = EXIT_FAILURE;
				goto _err;
			}
			connectionName[connectionNameSz] = '\0';

			VERBOSE_LOG("Attempt to disconnect");
			auto iter = std::find(connections.begin(), connections.end(), connectionName);
			if(connections.end() == iter) {
				syslog(LOG_NOTICE, "No such connection, nothing will change: %s", connectionName);
			}
			else {
				VERBOSE_LOG("Received following connection name: %s", connectionName);

				VERBOSE_LOG("Disconnecting...");
				segment.destroy<ConnectionRegionTy>(connectionName);
				connections.erase(iter);
				attachedConnections.erase(*iter);
				VERBOSE_LOG("Done");
			}
		}
		// "Attach to / open" a cached dynamic trace file
		// aXXXYYYYYYYYYY...ZZZZZZZZZZWWWKKKKKKKKKK...
		// XXX           Size of YYYYYYYYYY... string
		// YYYYYYYYYY... Connection name
		// ZZZZZZZZZZ    Transaction ID
		// WWW           Size of KKKKKKKKKK... string
		// KKKKKKKKKK... Cache entry name to attach to
		else if('a' == buff[0]) {
			nBytes = read(pipeFD, buff, 3);
			if(nBytes != 3) {
				syslog(LOG_ERR, "read() call failed when getting the connection name string size");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			buff[3] = '\0';
			unsigned connectionNameSz = 0;
			try {
				connectionNameSz = std::stoi(buff);
			}
			catch(const std::exception& e) {
				syslog(LOG_ERR, "Failed to convert connection name string size to int");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			if(connectionNameSz > BUFF_STR_SZ)
				connectionNameSz = BUFF_STR_SZ;

			char connectionName[BUFF_STR_SZ + 1];
			connectionName[BUFF_STR_SZ] = '\0';
			nBytes = read(pipeFD, connectionName, connectionNameSz);
			if(nBytes != connectionNameSz) {
				syslog(LOG_ERR, "read() call failed when getting the connection name string");
				retVal = EXIT_FAILURE;
				goto _err;
			}
			connectionName[connectionNameSz] = '\0';

			nBytes = read(pipeFD, buff, 10);
			if(nBytes != 10) {
				syslog(LOG_ERR, "read() call failed when getting the transaction ID");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			buff[10] = '\0';
			unsigned transactionID = 0;
			try {
				transactionID = std::stoi(buff);
			}
			catch(const std::exception& e) {
				syslog(LOG_ERR, "Failed to convert transaction ID to int");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			nBytes = read(pipeFD, buff, 3);
			if(nBytes != 3) {
				syslog(LOG_ERR, "read() call failed when getting the cache key string size");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			buff[3] = '\0';
			unsigned cacheKeySz = 0;
			try {
				cacheKeySz = std::stoi(buff);
			}
			catch(const std::exception& e) {
				syslog(LOG_ERR, "Failed to convert cache key string size to int");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			if(cacheKeySz > BUFF_STR_SZ)
				cacheKeySz = BUFF_STR_SZ;

			char cacheKey[BUFF_STR_SZ + 1];
			cacheKey[BUFF_STR_SZ] = '\0';
			nBytes = read(pipeFD, cacheKey, cacheKeySz);
			if(nBytes != cacheKeySz) {
				syslog(LOG_ERR, "read() call failed when getting the cache key string");
				retVal = EXIT_FAILURE;
				goto _err;
			}
			cacheKey[cacheKeySz] = '\0';

			std::string connectionNameStr(connectionName);
			std::string cacheKeyStr(cacheKey);
			std::string fullCacheKeyStr(connectionNameStr + KEY_SEPARATOR + cacheKeyStr);

			ConnectionRegionTy *connection = segment.find<ConnectionRegionTy>(connectionName).first;
			// Abort if there is no connection
			if(!connection) {
				syslog(LOG_ERR, "No such connection found: %s", connectionName);
				retVal = EXIT_FAILURE;
				goto _err;
			}

			// Remove previous attached connection if any
			const auto &elem = attachedConnections.find(connectionName);
			if(elem != attachedConnections.end())
				attachedConnections.erase(elem);

			bool success = false;

			// Open file if not already opened
			const auto &elem2 = dynamicTraceFiles.find(fullCacheKeyStr);
			if(elem2 != dynamicTraceFiles.end()) {
				success = (elem2->second->seekToBase() != -1);
			}
			else {
				syslog(LOG_NOTICE, "Shared dynamic trace entry does not exist. Creating...");
				syslog(LOG_NOTICE, "Connection name: %s", connectionName);
				syslog(LOG_NOTICE, "Cache key: %s", cacheKey);

				std::ifstream futureCacheFile;
				futureCacheFile.open(connectionName, std::ios::in | std::ios::binary);
				if(futureCacheFile.is_open()) {
					/* Check for magic bits in future cache file */
					char magicBits[4];
					futureCacheFile.read(magicBits, std::string(FILE_FUTURE_CACHE_MAGIC_STRING).size());
					magicBits[3] = '\0';
					if(FILE_FUTURE_CACHE_MAGIC_STRING == std::string(magicBits)) {
						/* Read cache size */
						size_t mapSize;
						futureCacheFile.read((char *) &mapSize, sizeof(size_t));
						/* Now read each element */
						for(unsigned i = 0; i < mapSize; i++) {
							size_t cacheKeySize;
							futureCacheFile.read((char *) &cacheKeySize, sizeof(size_t));

							char cacheBuff[BUFF_STR_SZ];
							futureCacheFile.read(cacheBuff, cacheKeySize);
							cacheBuff[cacheKeySize] = '\0';
							std::string curCacheKey(cacheBuff);

							long int gzCursor;
							futureCacheFile.read((char *) &gzCursor, sizeof(long int));
							futureCacheFile.seekg(5 * sizeof(uint64_t) + sizeof(long int), std::ios_base::cur);

							if(cacheKey != curCacheKey)
								continue;

							syslog(LOG_NOTICE, "Cache element: %s", cacheBuff);
							syslog(LOG_NOTICE, "Cursor position: %ld", gzCursor);

							BufferedGzFile *gzFileElement = new BufferedGzFile(dynamicTraceFileName, gzCursor);//, dbgFile);
							if(!(gzFileElement && gzFileElement->isOpen()))
								break;
							if(-1 == gzFileElement->seekToBase()) {
								delete gzFileElement;
								futureCacheFile.close();
								break;
							}

							dynamicTraceFiles.insert(std::make_pair(fullCacheKeyStr, gzFileElement));
							success = true;

							syslog(LOG_NOTICE, "Completed loading shared dynamic trace: %s", fullCacheKeyStr.c_str());
							break;
						}

						futureCacheFile.close();
					}
				}
			}

			// Update attached file if success
			if(success) {
				VERBOSE_LOG("Successfully attached:");
				VERBOSE_LOG("Connection name: %s", connectionName);
				VERBOSE_LOG("Cache key: %s", cacheKey);

				attachedConnections.insert(std::make_pair(connectionNameStr, cacheKeyStr));
			}
			else {
				syslog(LOG_NOTICE, "Failed to attach:");
				syslog(LOG_NOTICE, "Connection name: %s", connectionName);
				syslog(LOG_NOTICE, "Cache key: %s", cacheKey);
			}

			// Set up return values
			memcpy(&(connection->data())[sizeof(unsigned)], &success, 1);
			memcpy(connection->data(), &transactionID, sizeof(unsigned));
		}
		// Release cached dynamic trace file, returning its cursor
		// rXXXYYYYYYYYYY...
		// XXX           Size of YYYYYYYYYY... string
		// YYYYYYYYYY... Connection name
		else if('r' == buff[0]) {
			nBytes = read(pipeFD, buff, 3);
			if(nBytes != 3) {
				syslog(LOG_ERR, "read() call failed when getting the connection name string size");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			buff[3] = '\0';
			unsigned connectionNameSz = 0;
			try {
				connectionNameSz = std::stoi(buff);
			}
			catch(const std::exception& e) {
				syslog(LOG_ERR, "Failed to convert connection name string size to int");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			if(connectionNameSz > BUFF_STR_SZ)
				connectionNameSz = BUFF_STR_SZ;

			char connectionName[BUFF_STR_SZ + 1];
			connectionName[BUFF_STR_SZ] = '\0';
			nBytes = read(pipeFD, connectionName, connectionNameSz);
			if(nBytes != connectionNameSz) {
				syslog(LOG_ERR, "read() call failed when getting the connection name string");
				retVal = EXIT_FAILURE;
				goto _err;
			}
			connectionName[connectionNameSz] = '\0';

			std::string connectionNameStr(connectionName);

			ConnectionRegionTy *connection = segment.find<ConnectionRegionTy>(connectionName).first;
			// Abort if there is no connection
			if(!connection) {
				syslog(LOG_ERR, "No such connection found: %s", connectionName);
				retVal = EXIT_FAILURE;
				goto _err;
			}

			// Release attached connection (if any) and rewind cursor
			const auto &elem = attachedConnections.find(connectionName);
			if(elem != attachedConnections.end()) {
				std::string fullCacheKeyStr(connectionNameStr + KEY_SEPARATOR + elem->second);

				const auto &elem2 = dynamicTraceFiles.find(fullCacheKeyStr);
				if(dynamicTraceFiles.end() == elem2) {
					syslog(LOG_ERR, "Could not find open dynamic trace file when it should");
					syslog(LOG_ERR, "Connection name: %s", connectionName);
					syslog(LOG_ERR, "Cache key: %s", elem->second.c_str());
					retVal = EXIT_FAILURE;
					goto _err;
				}

				attachedConnections.erase(elem);

				VERBOSE_LOG("Successfully released:");
				VERBOSE_LOG("Connection name: %s", connectionName);
			}
			else {
				syslog(LOG_NOTICE, "Nothing was attached. Nothing to release:");
				syslog(LOG_NOTICE, "Connection name: %s", connectionName);
			}
		}
		// Perform a gzip command directly
		// gXXXXXXXXXXXXXXXXXX...
		else if('g' == buff[0]) {
			nBytes = read(pipeFD, buff, 1);
			if(nBytes < 0) {
				syslog(LOG_ERR, "read() call failed when getting the gzip command character");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			// Perform gzseek
			// gsXXXYYYYYYYYYY...ZZZZZZZZZZOOOOOOOOOOW
			// XXX           Size of YYYYYYYYYY... string
			// YYYYYYYYYY... Connection name
			// ZZZZZZZZZZ    Transaction ID
			// OOOOOOOOOO    Argument 1: offset
			// W             Argument 2: whence: 1 to SEEK_CUR, 0 to SEEK_SET
			if('s' == buff[0]) {
				nBytes = read(pipeFD, buff, 3);
				if(nBytes != 3) {
					syslog(LOG_ERR, "read() call failed when getting the connection name string size");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				buff[3] = '\0';
				unsigned connectionNameSz = 0;
				try {
					connectionNameSz = std::stoi(buff);
				}
				catch(const std::exception& e) {
					syslog(LOG_ERR, "Failed to convert connection name string size to int");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				if(connectionNameSz > BUFF_STR_SZ)
					connectionNameSz = BUFF_STR_SZ;

				char connectionName[BUFF_STR_SZ + 1];
				connectionName[BUFF_STR_SZ] = '\0';
				nBytes = read(pipeFD, connectionName, connectionNameSz);
				if(nBytes != connectionNameSz) {
					syslog(LOG_ERR, "read() call failed when getting the connection name string");
					retVal = EXIT_FAILURE;
					goto _err;
				}
				connectionName[connectionNameSz] = '\0';

				nBytes = read(pipeFD, buff, 10);
				if(nBytes != 10) {
					syslog(LOG_ERR, "read() call failed when getting the transaction ID");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				buff[10] = '\0';
				unsigned transactionID = 0;
				try {
					transactionID = std::stoi(buff);
				}
				catch(const std::exception& e) {
					syslog(LOG_ERR, "Failed to convert transaction ID to int");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				nBytes = read(pipeFD, buff, 10);
				if(nBytes != 10) {
					syslog(LOG_ERR, "read() call failed when getting gzseek argument 0: offset");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				buff[10] = '\0';
				z_off_t offset = 0;
				try {
					offset = std::stoi(buff);
				}
				catch(const std::exception& e) {
					syslog(LOG_ERR, "Failed to convert gzseek argument 0: offset, to int");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				nBytes = read(pipeFD, buff, 1);
				if(nBytes < 0) {
					syslog(LOG_ERR, "read() call failed when getting gzseek argument 1: whence");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				int whence = ('1' == buff[0])? SEEK_CUR : SEEK_SET;

				std::string connectionNameStr(connectionName);
				auto found = attachedConnections.find(connectionNameStr);
				if(attachedConnections.end() == found) {
					syslog(LOG_ERR, "No such connection found: %s", connectionName);
					retVal = EXIT_FAILURE;
					goto _err;
				}
				std::string cacheKeyStr(found->second);
				std::string fullCacheKeyStr(connectionNameStr + KEY_SEPARATOR + cacheKeyStr);

				ConnectionRegionTy *connection = segment.find<ConnectionRegionTy>(connectionName).first;
				// Abort if there is no connection
				if(!connection) {
					syslog(LOG_ERR, "No such connection found: %s", connectionName);
					retVal = EXIT_FAILURE;
					goto _err;
				}

				auto found2 = dynamicTraceFiles.find(fullCacheKeyStr);
				if(dynamicTraceFiles.end() == found2) {
					syslog(LOG_ERR, "Could not find open trace file with calculated key");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				z_off_t retVal = found2->second->seek(offset, whence);

				// Set up return values
				memcpy(&(connection->data())[sizeof(unsigned)], &retVal, sizeof(z_off_t));
				memcpy(connection->data(), &transactionID, sizeof(unsigned));
			}
			// Perform gzrewind
			// grXXXYYYYYYYYYY...ZZZZZZZZZZ
			// XXX           Size of YYYYYYYYYY... string
			// YYYYYYYYYY... Connection name
			// ZZZZZZZZZZ    Transaction ID
			else if('r' == buff[0]) {
				nBytes = read(pipeFD, buff, 3);
				if(nBytes != 3) {
					syslog(LOG_ERR, "read() call failed when getting the connection name string size");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				buff[3] = '\0';
				unsigned connectionNameSz = 0;
				try {
					connectionNameSz = std::stoi(buff);
				}
				catch(const std::exception& e) {
					syslog(LOG_ERR, "Failed to convert connection name string size to int");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				if(connectionNameSz > BUFF_STR_SZ)
					connectionNameSz = BUFF_STR_SZ;

				char connectionName[BUFF_STR_SZ + 1];
				connectionName[BUFF_STR_SZ] = '\0';
				nBytes = read(pipeFD, connectionName, connectionNameSz);
				if(nBytes != connectionNameSz) {
					syslog(LOG_ERR, "read() call failed when getting the connection name string");
					retVal = EXIT_FAILURE;
					goto _err;
				}
				connectionName[connectionNameSz] = '\0';

				nBytes = read(pipeFD, buff, 10);
				if(nBytes != 10) {
					syslog(LOG_ERR, "read() call failed when getting the transaction ID");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				buff[10] = '\0';
				unsigned transactionID = 0;
				try {
					transactionID = std::stoi(buff);
				}
				catch(const std::exception& e) {
					syslog(LOG_ERR, "Failed to convert transaction ID to int");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				std::string connectionNameStr(connectionName);
				auto found = attachedConnections.find(connectionNameStr);
				if(attachedConnections.end() == found) {
					syslog(LOG_ERR, "No such connection found: %s", connectionName);
					retVal = EXIT_FAILURE;
					goto _err;
				}
				std::string cacheKeyStr(found->second);
				std::string fullCacheKeyStr(connectionNameStr + KEY_SEPARATOR + cacheKeyStr);

				ConnectionRegionTy *connection = segment.find<ConnectionRegionTy>(connectionName).first;
				// Abort if there is no connection
				if(!connection) {
					syslog(LOG_ERR, "No such connection found: %s", connectionName);
					retVal = EXIT_FAILURE;
					goto _err;
				}

				auto found2 = dynamicTraceFiles.find(fullCacheKeyStr);
				if(dynamicTraceFiles.end() == found2) {
					syslog(LOG_ERR, "Could not find open trace file with calculated key");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				int retVal = found2->second->rewind();

				// Set up return values
				memcpy(&(connection->data())[sizeof(unsigned)], &retVal, sizeof(int));
				memcpy(connection->data(), &transactionID, sizeof(unsigned));
			}
			// Perform gzeof
			// geXXXYYYYYYYYYY...ZZZZZZZZZZ
			// XXX           Size of YYYYYYYYYY... string
			// YYYYYYYYYY... Connection name
			// ZZZZZZZZZZ    Transaction ID
			else if('e' == buff[0]) {
				nBytes = read(pipeFD, buff, 3);
				if(nBytes != 3) {
					syslog(LOG_ERR, "read() call failed when getting the connection name string size");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				buff[3] = '\0';
				unsigned connectionNameSz = 0;
				try {
					connectionNameSz = std::stoi(buff);
				}
				catch(const std::exception& e) {
					syslog(LOG_ERR, "Failed to convert connection name string size to int");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				if(connectionNameSz > BUFF_STR_SZ)
					connectionNameSz = BUFF_STR_SZ;

				char connectionName[BUFF_STR_SZ + 1];
				connectionName[BUFF_STR_SZ] = '\0';
				nBytes = read(pipeFD, connectionName, connectionNameSz);
				if(nBytes != connectionNameSz) {
					syslog(LOG_ERR, "read() call failed when getting the connection name string");
					retVal = EXIT_FAILURE;
					goto _err;
				}
				connectionName[connectionNameSz] = '\0';

				nBytes = read(pipeFD, buff, 10);
				if(nBytes != 10) {
					syslog(LOG_ERR, "read() call failed when getting the transaction ID");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				buff[10] = '\0';
				unsigned transactionID = 0;
				try {
					transactionID = std::stoi(buff);
				}
				catch(const std::exception& e) {
					syslog(LOG_ERR, "Failed to convert transaction ID to int");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				std::string connectionNameStr(connectionName);
				auto found = attachedConnections.find(connectionNameStr);
				if(attachedConnections.end() == found) {
					syslog(LOG_ERR, "No such connection found: %s", connectionName);
					retVal = EXIT_FAILURE;
					goto _err;
				}
				std::string cacheKeyStr(found->second);
				std::string fullCacheKeyStr(connectionNameStr + KEY_SEPARATOR + cacheKeyStr);

				ConnectionRegionTy *connection = segment.find<ConnectionRegionTy>(connectionName).first;
				// Abort if there is no connection
				if(!connection) {
					syslog(LOG_ERR, "No such connection found: %s", connectionName);
					retVal = EXIT_FAILURE;
					goto _err;
				}

				auto found2 = dynamicTraceFiles.find(fullCacheKeyStr);
				if(dynamicTraceFiles.end() == found2) {
					syslog(LOG_ERR, "Could not find open trace file with calculated key");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				int retVal = found2->second->eof();

				// Set up return values
				memcpy(&(connection->data())[sizeof(unsigned)], &retVal, sizeof(int));
				memcpy(connection->data(), &transactionID, sizeof(unsigned));
			}
			// Perform gztell
			// gtXXXYYYYYYYYYY...ZZZZZZZZZZ
			// XXX           Size of YYYYYYYYYY... string
			// YYYYYYYYYY... Connection name
			// ZZZZZZZZZZ    Transaction ID
			else if('t' == buff[0]) {
				nBytes = read(pipeFD, buff, 3);
				if(nBytes != 3) {
					syslog(LOG_ERR, "read() call failed when getting the connection name string size");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				buff[3] = '\0';
				unsigned connectionNameSz = 0;
				try {
					connectionNameSz = std::stoi(buff);
				}
				catch(const std::exception& e) {
					syslog(LOG_ERR, "Failed to convert connection name string size to int");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				if(connectionNameSz > BUFF_STR_SZ)
					connectionNameSz = BUFF_STR_SZ;

				char connectionName[BUFF_STR_SZ + 1];
				connectionName[BUFF_STR_SZ] = '\0';
				nBytes = read(pipeFD, connectionName, connectionNameSz);
				if(nBytes != connectionNameSz) {
					syslog(LOG_ERR, "read() call failed when getting the connection name string");
					retVal = EXIT_FAILURE;
					goto _err;
				}
				connectionName[connectionNameSz] = '\0';

				nBytes = read(pipeFD, buff, 10);
				if(nBytes != 10) {
					syslog(LOG_ERR, "read() call failed when getting the transaction ID");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				buff[10] = '\0';
				unsigned transactionID = 0;
				try {
					transactionID = std::stoi(buff);
				}
				catch(const std::exception& e) {
					syslog(LOG_ERR, "Failed to convert transaction ID to int");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				std::string connectionNameStr(connectionName);
				auto found = attachedConnections.find(connectionNameStr);
				if(attachedConnections.end() == found) {
					syslog(LOG_ERR, "No such connection found: %s", connectionName);
					retVal = EXIT_FAILURE;
					goto _err;
				}
				std::string cacheKeyStr(found->second);
				std::string fullCacheKeyStr(connectionNameStr + KEY_SEPARATOR + cacheKeyStr);

				ConnectionRegionTy *connection = segment.find<ConnectionRegionTy>(connectionName).first;
				// Abort if there is no connection
				if(!connection) {
					syslog(LOG_ERR, "No such connection found: %s", connectionName);
					retVal = EXIT_FAILURE;
					goto _err;
				}

				auto found2 = dynamicTraceFiles.find(fullCacheKeyStr);
				if(dynamicTraceFiles.end() == found2) {
					syslog(LOG_ERR, "Could not find open trace file with calculated key");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				z_off_t retVal = found2->second->tell();

				// Set up return values
				memcpy(&(connection->data())[sizeof(unsigned)], &retVal, sizeof(z_off_t));
				memcpy(connection->data(), &transactionID, sizeof(unsigned));
			}
			// Perform gzgets
			// ggXXXYYYYYYYYYY...ZZZZZZZZZZLLLLLLLLLL
			// XXX           Size of YYYYYYYYYY... string
			// YYYYYYYYYY... Connection name
			// ZZZZZZZZZZ    Transaction ID
			// LLLLLLLLLL    Argument 1: len
			else if('g' == buff[0]) {
				nBytes = read(pipeFD, buff, 3);
				if(nBytes != 3) {
					syslog(LOG_ERR, "read() call failed when getting the connection name string size");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				buff[3] = '\0';
				unsigned connectionNameSz = 0;
				try {
					connectionNameSz = std::stoi(buff);
				}
				catch(const std::exception& e) {
					syslog(LOG_ERR, "Failed to convert connection name string size to int");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				if(connectionNameSz > BUFF_STR_SZ)
					connectionNameSz = BUFF_STR_SZ;

				char connectionName[BUFF_STR_SZ + 1];
				connectionName[BUFF_STR_SZ] = '\0';
				nBytes = read(pipeFD, connectionName, connectionNameSz);
				if(nBytes != connectionNameSz) {
					syslog(LOG_ERR, "read() call failed when getting the connection name string");
					retVal = EXIT_FAILURE;
					goto _err;
				}
				connectionName[connectionNameSz] = '\0';

				nBytes = read(pipeFD, buff, 10);
				if(nBytes != 10) {
					syslog(LOG_ERR, "read() call failed when getting the transaction ID");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				buff[10] = '\0';
				unsigned transactionID = 0;
				try {
					transactionID = std::stoi(buff);
				}
				catch(const std::exception& e) {
					syslog(LOG_ERR, "Failed to convert transaction ID to int");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				nBytes = read(pipeFD, buff, 10);
				if(nBytes != 10) {
					syslog(LOG_ERR, "read() call failed when getting gzseek argument 1: len");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				buff[10] = '\0';
				int len = 0;
				try {
					len = std::stoi(buff);
				}
				catch(const std::exception& e) {
					syslog(LOG_ERR, "Failed to convert gzseek argument 1: len, to int");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				std::string connectionNameStr(connectionName);
				auto found = attachedConnections.find(connectionNameStr);
				if(attachedConnections.end() == found) {
					syslog(LOG_ERR, "No such connection found: %s", connectionName);
					retVal = EXIT_FAILURE;
					goto _err;
				}
				std::string cacheKeyStr(found->second);
				std::string fullCacheKeyStr(connectionNameStr + KEY_SEPARATOR + cacheKeyStr);

				ConnectionRegionTy *connection = segment.find<ConnectionRegionTy>(connectionName).first;
				// Abort if there is no connection
				if(!connection) {
					syslog(LOG_ERR, "No such connection found: %s", connectionName);
					retVal = EXIT_FAILURE;
					goto _err;
				}

				auto found2 = dynamicTraceFiles.find(fullCacheKeyStr);
				if(dynamicTraceFiles.end() == found2) {
					syslog(LOG_ERR, "Could not find open trace file with calculated key");
					retVal = EXIT_FAILURE;
					goto _err;
				}

				char *retVal = found2->second->gets(&(connection->data())[sizeof(unsigned) + 1], len);
				bool retValIsNotNull = (retVal != Z_NULL);

				// Set up return values
				memcpy(&(connection->data())[sizeof(unsigned)], &retValIsNotNull, 1);
				memcpy(connection->data(), &transactionID, sizeof(unsigned));
			}
			else {
				syslog(LOG_ERR, "Invalid gzip command received");
				retVal = EXIT_FAILURE;
				goto _err;
			}
		}
		else if('\0' == buff[0] || '\n' == buff[0]) {
			continue;
		}
		else {
			break;
		}
	}

_err:

	/* CLEANUP */

#if 0
	syslog(LOG_NOTICE, "==========Ranges=============");
	for(auto &elem : dynamicTraceFiles) {
		syslog(LOG_NOTICE, "== [%s] (min: %ld, max: %ld, size: %ld)",
			elem.first.c_str(),
			elem.second->bufferBounds().first, elem.second->bufferBounds().second,
			elem.second->bufferSize()
		);
	}
	syslog(LOG_NOTICE, "=============================");
#endif

	for(auto &elem : dynamicTraceFiles)
		delete elem.second;
	dynamicTraceFiles.clear();

	for(auto &elem : connections)
		segment.destroy<ConnectionRegionTy>(elem.c_str());

	if(pipeFD != -1)
		close(pipeFD);

	unlink(pipePath.c_str());

	syslog(LOG_NOTICE, "Finished, bye!");
	closelog();

	return retVal;
}
