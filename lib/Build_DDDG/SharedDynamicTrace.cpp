#include "profile_h/SharedDynamicTrace.h"

#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include "profile_h/auxiliary.h"

using namespace llvm;

unsigned SharedDynamicTrace::transactionID = 0;

SharedDynamicTrace::SharedDynamicTrace(std::string dynamicTraceFileName, std::string futureCacheFileName) {
	curCacheKey = "uncached";

#if 0
	// If linad pipe file is not visible, perhaps linad is not running
	// Check if an instance of linad is open by checking the pipe file
	bool hadToSpawn = false;
	struct stat statBuff;
	if(stat(PIPE_PATH, &statBuff)) {
		VERBOSE_PRINT(errs() << "[linad-link] could not find \"linad\" pipe file. Will try to spawn a \"linad\" instance...\n");
		system("linad");

		// For one second, check if file is now up
		for(int i = 0; i < 5 && stat(PIPE_PATH, &statBuff); i++)
			sleep(0.2);
		assert(!stat(PIPE_PATH, &statBuff) && "After 1 second, linad pipe file is still not present");

		hadToSpawn = true;
	}

	linadPipe.open(PIPE_PATH);
	assert(linadPipe.is_open() && "Could not open linad pipe file");
	VERBOSE_PRINT(errs() << "[linad-link] Successfully opened \"linad\" pipe file\n");

	if(hadToSpawn) {
		VERBOSE_PRINT(errs() << "[linad-link] Attaching dynamic trace\n");

		std::stringstream ss;
		ss << std::setw(3) << std::setfill('0') << dynamicTraceFileName.length();
		linadPipe << "d" << ss.str() << dynamicTraceFileName << std::flush;
	}
#endif 

	// The full future cache path is used as connection key with linad
	// TODO This could be improved at some point (for example, using a command-line argument as key)
	char *buff2 = realpath(futureCacheFileName.c_str(), NULL);
	assert(buff2 && "Could not retrieve absolute path for readlink() return value");
	futureCacheRealName.assign(buff2);
	free(buff2);

	// TODO Improve this
	// (i.e. somehow get an ID value that defines the futurecache from command line)
	// That'll need mods on cirith
	std::string pipeIDStr;
	if("." == futureCacheRealName.substr(futureCacheRealName.length() - 2, 1))
		pipeIDStr.assign(futureCacheRealName.substr(futureCacheRealName.length() - 1, 1));
	else
		pipeIDStr.assign(futureCacheRealName.substr(futureCacheRealName.length() - 2, 2));
	std::string pipePath(PIPE_PATH + pipeIDStr);
	linadPipe.open(pipePath);
	assert(linadPipe.is_open() && "Could not open linad pipe file");
	VERBOSE_PRINT(errs() << "[linad-link] Successfully opened \"linad\" pipe file " << pipePath << "\n");

	// Create connection
	std::stringstream ss;
	ss << std::setw(3) << std::setfill('0') << futureCacheRealName.length();
	linadPipe << "c" << ss.str() << futureCacheRealName << std::flush;

	// TODO Improve this
	std::string fullSharedMemoryName(SHARED_MEMORY_NAME + pipeIDStr);
	segment = boost::interprocess::managed_shared_memory(boost::interprocess::open_only, fullSharedMemoryName.c_str());
	// Wait until connection memory region is created, or timeout
	for(int i = 0; i < 5; i++) {
		connection = segment.find<ConnectionRegionTy>(futureCacheRealName.c_str()).first;
		if(connection)
			break;
		sleep(1.0);
	}
	assert(connection && "Could not retrieve connection memory region");

	uncachedFile = gzopen(dynamicTraceFileName.c_str(), "r");
	assert(uncachedFile && "Could not open uncached dynamic trace");

	VERBOSE_PRINT(errs() << "[linad-link] Shared dynamic trace initialised\n");
}

SharedDynamicTrace::~SharedDynamicTrace() {
	if(uncachedFile)
		gzclose(uncachedFile);

	if(linadPipe.is_open()) {
		// Disconnect
		std::stringstream ss;
		ss << std::setw(3) << std::setfill('0') << futureCacheRealName.length();
		linadPipe << "x" << ss.str() << futureCacheRealName << std::flush;

		linadPipe.close();

		VERBOSE_PRINT(errs() << "[linad-link] Connection closed\n");
	}
}

void SharedDynamicTrace::_attach(std::string cacheKey, long int *fallbackCursor) {
	std::stringstream ss;
	ss << std::setfill('0') << "a"
		<< std::setw(3) << futureCacheRealName.length() << futureCacheRealName
		<< std::setw(10) << ++transactionID
		<< std::setw(3) << cacheKey.length() << cacheKey;
	linadPipe << ss.str() << std::flush;

	// Wait for reply
	while((((unsigned *) (connection->data()))[0]) != transactionID);

	// If attach did not succeed, switch to uncached
	if((*connection)[sizeof(unsigned)]) {
		curCacheKey = cacheKey;
		VERBOSE_PRINT(errs() << "[linad-link] Successfully attached to " << curCacheKey << "\n");
	}
	else {
		curCacheKey = "uncached";
		VERBOSE_PRINT(errs() << "[linad-link] Could not attach to " << curCacheKey << ", using local file instead\n");

		if(fallbackCursor) {
			VERBOSE_PRINT(errs() << "[linad-link] Using fallback cursor " << std::to_string(*fallbackCursor) << "\n");
			gzseek(uncachedFile, *fallbackCursor, SEEK_SET);
		}
	}
}

void SharedDynamicTrace::attach(std::string cacheKey) {
	return _attach(cacheKey, NULL);
}

void SharedDynamicTrace::attach(std::string cacheKey, long int fallbackCursor) {
	return _attach(cacheKey, &fallbackCursor);
}


void SharedDynamicTrace::release() {
	if(curCacheKey != "uncached") {
		std::stringstream ss;
		ss << std::setfill('0') << "r"
			<< std::setw(3) << futureCacheRealName.length() << futureCacheRealName;
		linadPipe << ss.str() << std::flush;

		curCacheKey = "uncached";

		VERBOSE_PRINT(errs() << "[linad-link] Successfully released cache file\n");
	}
}

z_off_t SharedDynamicTrace::seek(z_off_t offset, int whence) {
	if("uncached" == curCacheKey) {
		return gzseek(uncachedFile, offset, whence);
	}
	else {
		std::stringstream ss;
		ss << std::setfill('0') << "gs"
			<< std::setw(3) << futureCacheRealName.length() << futureCacheRealName
			<< std::setw(10) << ++transactionID
			<< std::setw(10) << offset
			<< ((SEEK_CUR == whence)? "1" : "0");
		linadPipe << ss.str() << std::flush;

		// Wait for reply
		while((((unsigned *) (connection->data()))[0]) != transactionID);

		return ((z_off_t *) &(connection->data())[sizeof(unsigned)])[0];
	}
}

int SharedDynamicTrace::rewind() {
	if("uncached" == curCacheKey) {
		return gzrewind(uncachedFile);
	}
	else {
		std::stringstream ss;
		ss << std::setfill('0') << "gr"
			<< std::setw(3) << futureCacheRealName.length() << futureCacheRealName
			<< std::setw(10) << ++transactionID;
		linadPipe << ss.str() << std::flush;

		// Wait for reply
		while((((unsigned *) (connection->data()))[0]) != transactionID);

		return ((int *) &(connection->data())[sizeof(unsigned)])[0];
	}
}

int SharedDynamicTrace::eof() {
	if("uncached" == curCacheKey) {
		return gzeof(uncachedFile);
	}
	else {
		std::stringstream ss;
		ss << std::setfill('0') << "ge"
			<< std::setw(3) << futureCacheRealName.length() << futureCacheRealName
			<< std::setw(10) << ++transactionID;
		linadPipe << ss.str() << std::flush;

		// Wait for reply
		while((((unsigned *) (connection->data()))[0]) != transactionID);

		return ((int *) &(connection->data())[sizeof(unsigned)])[0];
	}
}

z_off_t SharedDynamicTrace::tell() {
	if("uncached" == curCacheKey) {
		return gztell(uncachedFile);
	}
	else {
		std::stringstream ss;
		ss << std::setfill('0') << "gt"
			<< std::setw(3) << futureCacheRealName.length() << futureCacheRealName
			<< std::setw(10) << ++transactionID;
		linadPipe << ss.str() << std::flush;

		// Wait for reply
		while((((unsigned *) (connection->data()))[0]) != transactionID);

		return ((z_off_t *) &(connection->data())[sizeof(unsigned)])[0];
	}
}

char *SharedDynamicTrace::gets(char *buff, int len) {
	if("uncached" == curCacheKey) {
		return gzgets(uncachedFile, buff, len);
	}
	else {
		std::stringstream ss;
		ss << std::setfill('0') << "gg"
			<< std::setw(3) << futureCacheRealName.length() << futureCacheRealName
			<< std::setw(10) << ++transactionID
			<< std::setw(10) << len;
		linadPipe << ss.str() << std::flush;

		// Wait for reply
		while((((unsigned *) (connection->data()))[0]) != transactionID);

		// Check if gets returned NULL or not
		// Return buffer if gets did not return NULL
		if((*connection)[sizeof(unsigned)]) {
			memcpy(buff, &(connection->data())[sizeof(unsigned) + 1], len);
			return buff;
		}

		return Z_NULL;
	}
}
