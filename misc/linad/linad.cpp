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
typedef boost::interprocess::vector<char, ByteShmAllocator> DynamicTraceRegionTy;


#define DYNAMIC_TRACE_DEFAULT_BUFFER_SIZE 131072
#define DYNAMIC_TRACE_GET0_ISINIT(traceRegion) (((bool *) (traceRegion->data()))[0])
#define DYNAMIC_TRACE_GET1_EOFFOUND(traceRegion) (((bool *) (traceRegion->data()))[1])
#define DYNAMIC_TRACE_GET2_LOWER(traceRegion) *((z_off_t *) (&(traceRegion->data())[sizeof(bool) + sizeof(bool)]))
#define DYNAMIC_TRACE_GET3_HIGHER(traceRegion) *((z_off_t *) (&(traceRegion->data())[sizeof(bool) + sizeof(bool) + sizeof(z_off_t)]))
#define DYNAMIC_TRACE_GET4_DATA(traceRegion) (&(traceRegion->data())[sizeof(bool) + sizeof(bool) + sizeof(z_off_t) + sizeof(z_off_t)])
#define DYNAMIC_TRACE_SET0_ISINIT(traceRegion, isInit) do {\
	bool _isInit = isInit;\
	memcpy(traceRegion->data(), &_isInit, sizeof(bool));\
} while(0)
#define DYNAMIC_TRACE_SET1_EOFFOUND(traceRegion, eofFound) do {\
	bool _eofFound = eofFound;\
	memcpy(&(traceRegion->data())[sizeof(bool)], &_eofFound, sizeof(bool));\
} while(0)
#define DYNAMIC_TRACE_SET2_LOWER(traceRegion, lower) do {\
	z_off_t _lower = lower;\
	memcpy(&(traceRegion->data())[sizeof(bool) + sizeof(bool)], &_lower, sizeof(z_off_t));\
} while(0)
#define DYNAMIC_TRACE_SET3_HIGHER(traceRegion, higher) do {\
	z_off_t _higher = higher;\
	memcpy(&(traceRegion->data())[sizeof(bool) + sizeof(bool) + sizeof(z_off_t)], &_higher, sizeof(z_off_t));\
} while(0)
#define DYNAMIC_TRACE_SET4_DATA(traceRegion, data, size) do {\
	memcpy(&(traceRegion->data())[sizeof(bool) + sizeof(bool) + sizeof(z_off_t) + sizeof(z_off_t)], data, size);\
} while(0)
#define DYNAMIC_TRACE_RESIZE(traceRegion, len) traceRegion->resize(sizeof(bool) + sizeof(bool) + sizeof(z_off_t) + sizeof(z_off_t) + (len))
#define DYNAMIC_TRACE_INVALIDATE(traceRegion) do {\
	DYNAMIC_TRACE_RESIZE(traceRegion, 0);\
	DYNAMIC_TRACE_SET0_ISINIT(traceRegion, false);\
	DYNAMIC_TRACE_SET1_EOFFOUND(traceRegion, false);\
} while(0)


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
	std::map<std::string, gzFile> dynamicTraceFiles;
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
				for(auto &elem : dynamicTraceFiles) {
					gzclose(elem.second);
					segment.destroy<DynamicTraceRegionTy>(elem.first.c_str());
				}
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
				success = true;
			}
			else {
				syslog(LOG_NOTICE, "Shared dynamic trace entry does not exist. Creating...");
				syslog(LOG_NOTICE, "Connection name: %s", connectionName);
				syslog(LOG_NOTICE, "Cache key: %s", cacheKey);

				gzFile file = gzopen(dynamicTraceFileName.c_str(), "r");
				if(file) {
					DynamicTraceRegionTy *traceRegion = segment.construct<DynamicTraceRegionTy>(fullCacheKeyStr.c_str())(byteAllocInst);
					if(traceRegion) {
						DYNAMIC_TRACE_RESIZE(traceRegion, 0);
						DYNAMIC_TRACE_SET0_ISINIT(traceRegion, false);
						DYNAMIC_TRACE_SET1_EOFFOUND(traceRegion, false);

						dynamicTraceFiles.insert(std::make_pair(fullCacheKeyStr, file));
						success = true;

						syslog(LOG_NOTICE, "Completed loading shared dynamic trace: %s", fullCacheKeyStr.c_str());
					}
					else {
						gzclose(file);
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
		// Release cached dynamic trace file
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
		// Refresh/get dynamic trace buffer
		// gXXXYYYYYYYYYY...ZZZZZZZZZZWWWWWWWWWWLLLLLLLLLL
		// XXX           Size of YYYYYYYYYY... string
		// YYYYYYYYYY... Connection name
		// ZZZZZZZZZZ    Transaction ID
		// WWWWWWWWWW    Proposed cursor
		// LLLLLLLLLL    Max. amount of data to be read starting from proposed cursor
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
				syslog(LOG_ERR, "read() call failed when getting the proposed cursor");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			buff[10] = '\0';
			z_off_t proposedCursor = 0;
			try {
				proposedCursor = std::stoi(buff);
			}
			catch(const std::exception& e) {
				syslog(LOG_ERR, "Failed to convert proposed cursor to int");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			nBytes = read(pipeFD, buff, 10);
			if(nBytes != 10) {
				syslog(LOG_ERR, "read() call failed when getting the max. read length");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			buff[10] = '\0';
			unsigned len = 0;
			try {
				len = std::stoi(buff);
			}
			catch(const std::exception& e) {
				syslog(LOG_ERR, "Failed to convert max. read length to int");
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

			DynamicTraceRegionTy *traceRegion = segment.find<DynamicTraceRegionTy>(fullCacheKeyStr.c_str()).first;
			if(!traceRegion) {
				syslog(LOG_ERR, "Could not find trace region with calculated key");
				retVal = EXIT_FAILURE;
				goto _err;
			}

			bool isInit = DYNAMIC_TRACE_GET0_ISINIT(traceRegion);
			bool eofFound = DYNAMIC_TRACE_GET1_EOFFOUND(traceRegion);
			z_off_t lower = DYNAMIC_TRACE_GET2_LOWER(traceRegion);
			z_off_t higher = DYNAMIC_TRACE_GET3_HIGHER(traceRegion);

			syslog(LOG_NOTICE, "Refresh requested on cache element \"%s\"" , fullCacheKeyStr.c_str());
			syslog(LOG_NOTICE, "Metadata before refresh:");
			syslog(LOG_NOTICE, "  isInit: %s", isInit? "true" : "false");
			if(isInit) {
				syslog(LOG_NOTICE, "  eofFound: %s", eofFound? "true" : "false");
				syslog(LOG_NOTICE, "  lower: %ld", lower);
				syslog(LOG_NOTICE, "  higher: %ld", higher);
			}
			syslog(LOG_NOTICE, "Received proposed cursor: %ld", proposedCursor);
			syslog(LOG_NOTICE, "Max. amount of data to be read starting from proposed cursor: %d", len);

			if(isInit) {
				VERBOSE_LOG("Refreshing buffer...");
				bool retValIsNull = false;

				DYNAMIC_TRACE_RESIZE(traceRegion, higher - lower);

				// Adjust the true cursor before reading
				int retVal = gzseek(found2->second, lower, SEEK_SET);
				if(-1 == retVal)
					retValIsNull = true;

				if(!retValIsNull) {
					int retVal = gzread(found2->second, DYNAMIC_TRACE_GET4_DATA(traceRegion), higher - lower);
					if(-1 == retVal) {
						retValIsNull = true;
					}
					else if(retVal < (higher - lower)) {
						eofFound = true;
						higher = proposedCursor + retVal;
					}
				}

				if(retValIsNull) {
					syslog(LOG_NOTICE, "Refresh buffer failed, returning NULL and invalidating buffer");

					DYNAMIC_TRACE_INVALIDATE(traceRegion);
				}
				else {
					DYNAMIC_TRACE_SET0_ISINIT(traceRegion, true);
					DYNAMIC_TRACE_SET1_EOFFOUND(traceRegion, eofFound);
					DYNAMIC_TRACE_SET2_LOWER(traceRegion, lower);
					DYNAMIC_TRACE_SET3_HIGHER(traceRegion, higher);

					syslog(LOG_NOTICE, "Refresh buffer finished");
					syslog(LOG_NOTICE, "Metadata after refresh:");
					syslog(LOG_NOTICE, "  isInit: %s", isInit? "true" : "false");
					syslog(LOG_NOTICE, "  eofFound: %s", eofFound? "true" : "false");
					syslog(LOG_NOTICE, "  lower: %ld", lower);
					syslog(LOG_NOTICE, "  higher: %ld", higher);
				}

				// Set up return values
				bool returnIsInverted = !retValIsNull;
				memcpy(&(connection->data())[sizeof(unsigned)], &returnIsInverted, sizeof(bool));
				memcpy(connection->data(), &transactionID, sizeof(unsigned));
			}
			else {
				VERBOSE_LOG("Initialising buffer...");
				bool retValIsNull = false;

				DYNAMIC_TRACE_RESIZE(traceRegion, DYNAMIC_TRACE_DEFAULT_BUFFER_SIZE);

				// Adjust the true cursor before reading
				int retVal = gzseek(found2->second, proposedCursor, SEEK_SET);
				if(-1 == retVal)
					retValIsNull = true;

				if(!retValIsNull) {
					int retVal = gzread(found2->second, DYNAMIC_TRACE_GET4_DATA(traceRegion), DYNAMIC_TRACE_DEFAULT_BUFFER_SIZE);
					if(-1 == retVal) {
						retValIsNull = true;
					}
					else if(retVal < DYNAMIC_TRACE_DEFAULT_BUFFER_SIZE) {
						eofFound = true;
						lower = proposedCursor;
						higher = proposedCursor + retVal;
					}
					else {
						lower = proposedCursor;
						higher = proposedCursor + DYNAMIC_TRACE_DEFAULT_BUFFER_SIZE;
					}
				}

				if(retValIsNull) {
					syslog(LOG_NOTICE, "Initialise buffer failed, returning NULL and invalidating buffer");

					DYNAMIC_TRACE_INVALIDATE(traceRegion);
				}
				else {
					DYNAMIC_TRACE_SET0_ISINIT(traceRegion, true);
					DYNAMIC_TRACE_SET1_EOFFOUND(traceRegion, eofFound);
					DYNAMIC_TRACE_SET2_LOWER(traceRegion, lower);
					DYNAMIC_TRACE_SET3_HIGHER(traceRegion, higher);

					syslog(LOG_NOTICE, "Initialise buffer finished");
					syslog(LOG_NOTICE, "Metadata after initialise:");
					syslog(LOG_NOTICE, "  isInit: %s", isInit? "true" : "false");
					syslog(LOG_NOTICE, "  eofFound: %s", eofFound? "true" : "false");
					syslog(LOG_NOTICE, "  lower: %ld", lower);
					syslog(LOG_NOTICE, "  higher: %ld", higher);
				}

				// Set up return values
				bool returnIsInverted = !retValIsNull;
				memcpy(&(connection->data())[sizeof(unsigned)], &returnIsInverted, sizeof(bool));
				memcpy(connection->data(), &transactionID, sizeof(unsigned));
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

	for(auto &elem : dynamicTraceFiles) {
		gzclose(elem.second);
		segment.destroy<DynamicTraceRegionTy>(elem.first.c_str());
	}
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
