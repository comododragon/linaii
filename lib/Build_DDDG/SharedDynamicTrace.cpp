#include "profile_h/SharedDynamicTrace.h"

#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include "profile_h/auxiliary.h"

using namespace llvm;

unsigned SharedDynamicTrace::transactionID = 0;

/* Internal gets() method. This method assumes that all needed data is already on buffered cache */
/* Then it just proceeds as a normal gets() call would */
char *SharedDynamicTrace::_gets(char *buff, int len) {
	// The beginning of gets() call guarantees that proposedCursor will be within the buffered region
	// That is, proposedCursor should never be above higherCursor
	if(proposedCursor >= DYNAMIC_TRACE_GET3_HIGHER(dynamicTrace)) {
		DYNAMIC_TRACE_INVALIDATE(dynamicTrace);
		return Z_NULL;
	}

	// Also the logic before this call guarantees that there will be enough characters in buffer
	// to fulfill this function
	z_off_t idx;
	for(idx = proposedCursor; idx < DYNAMIC_TRACE_GET3_HIGHER(dynamicTrace) && (idx - proposedCursor) < (len - 1); idx++) {
		// If a newline was found, we found our match
		if('\n' == DYNAMIC_TRACE_GET4_DATA(dynamicTrace)[idx - DYNAMIC_TRACE_GET2_LOWER(dynamicTrace)]) {
			idx++;
			break;
		}
	}

	// Copy content and close string
	memcpy(buff, &(DYNAMIC_TRACE_GET4_DATA(dynamicTrace))[proposedCursor - DYNAMIC_TRACE_GET2_LOWER(dynamicTrace)], idx - proposedCursor);
	buff[idx - proposedCursor] = '\0';

	// Update cursors
	proposedCursor = idx;
	curCursor = idx;

	return buff;
}

SharedDynamicTrace::SharedDynamicTrace(std::string dynamicTraceFileName, std::string futureCacheFileName) {
	curCacheKey = "uncached";
	curCursor = 0;
	proposedCursor = 0;

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

void SharedDynamicTrace::attach(std::string cacheKey, long int baseCursor) {
	std::stringstream ss;
	ss << std::setfill('0') << "a"
		<< std::setw(3) << futureCacheRealName.length() << futureCacheRealName
		<< std::setw(10) << ++transactionID
		<< std::setw(3) << cacheKey.length() << cacheKey;
	linadPipe << ss.str() << std::flush;

	// Wait for reply
	while((((unsigned *) (connection->data()))[0]) != transactionID);

	bool switchToUncached = false;
	if((*connection)[sizeof(unsigned)]) {
		std::string fullCacheKeyStr(futureCacheRealName + DYNAMIC_TRACE_KEY_SEPARATOR + cacheKey);
		// Wait until dynamic trace region is created, or timeout
		for(int i = 0; i < 5; i++) {
			dynamicTrace = segment.find<DynamicTraceRegionTy>(fullCacheKeyStr.c_str()).first;
			if(dynamicTrace)
				break;
			sleep(1.0);
		}
		if(!dynamicTrace)
			switchToUncached = true;
	}
	else {
		switchToUncached = true;
	}

	// If attach did not succeed, switch to uncached
	if(switchToUncached) {
		curCacheKey = "uncached";
		gzseek(uncachedFile, baseCursor, SEEK_SET);
		VERBOSE_PRINT(errs() << "[linad-link] Could not attach to " << curCacheKey << ", using local file instead\n");
	}
	else {
		curCacheKey = cacheKey;
		proposedCursor = baseCursor;
		VERBOSE_PRINT(errs() << "[linad-link] Successfully attached to " << curCacheKey << "\n");
	}
}


void SharedDynamicTrace::release() {
	if(curCacheKey != "uncached") {
		std::stringstream ss;
		ss << std::setfill('0') << "r"
			<< std::setw(3) << futureCacheRealName.length() << futureCacheRealName;
		linadPipe << ss.str() << std::flush;

		curCacheKey = "uncached";
		dynamicTrace = NULL;

		VERBOSE_PRINT(errs() << "[linad-link] Successfully released cache file\n");
	}
}

z_off_t SharedDynamicTrace::seek(z_off_t offset, int whence) {
	if("uncached" == curCacheKey) {
		return gzseek(uncachedFile, offset, whence);
	}
	else {
		if(whence != SEEK_SET && whence != SEEK_CUR)
			return -1;

		if(SEEK_CUR == whence)
			proposedCursor += offset;
		else
			proposedCursor = offset;

		return proposedCursor;
	}
}

int SharedDynamicTrace::rewind() {
	if("uncached" == curCacheKey) {
		return gzrewind(uncachedFile);
	}
	else {
		proposedCursor = 0;
		return 0;
	}
}

int SharedDynamicTrace::eof() {
	if("uncached" == curCacheKey) {
		return gzeof(uncachedFile);
	}
	else {
		if(DYNAMIC_TRACE_GET0_ISINIT(dynamicTrace))
			return DYNAMIC_TRACE_GET1_EOFFOUND(dynamicTrace)? (curCursor >= DYNAMIC_TRACE_GET3_HIGHER(dynamicTrace)) : 0;
		else
			return 0;
	}
}

z_off_t SharedDynamicTrace::tell() {
	if("uncached" == curCacheKey) {
		return gztell(uncachedFile);
	}
	else {
		return proposedCursor;
	}
}

char *SharedDynamicTrace::gets(char *buff, int len) {
	if("uncached" == curCacheKey) {
		return gzgets(uncachedFile, buff, len);
	}
	else {
		if(DYNAMIC_TRACE_GET0_ISINIT(dynamicTrace)) {
			bool readjustBuffer = false;
			if(proposedCursor != curCursor) {
				// If proposed cursor is out of bounds of buffer, readjust buffer
				if(proposedCursor < DYNAMIC_TRACE_GET2_LOWER(dynamicTrace)) {
					DYNAMIC_TRACE_SET2_LOWER(dynamicTrace, proposedCursor);
					readjustBuffer = true;
				}
				// Only readjust up if EOF has not been reached
				if(!DYNAMIC_TRACE_GET1_EOFFOUND(dynamicTrace) && (proposedCursor > DYNAMIC_TRACE_GET3_HIGHER(dynamicTrace))) {
					DYNAMIC_TRACE_SET3_HIGHER(dynamicTrace, proposedCursor);
					readjustBuffer = true;
				}
			}
			// If buffer has no remaining len-1 elements to be read, readjust buffer
			// Only readjust up if EOF has not been reached
			if(!DYNAMIC_TRACE_GET1_EOFFOUND(dynamicTrace) && ((proposedCursor + (len - 1)) > DYNAMIC_TRACE_GET3_HIGHER(dynamicTrace))) {
				DYNAMIC_TRACE_SET3_HIGHER(dynamicTrace, proposedCursor + DYNAMIC_TRACE_DEFAULT_BUFFER_SIZE);
				readjustBuffer = true;
			}

			// If buffer must be readjusted, issue refresh command
			if(readjustBuffer) {
				std::stringstream ss;
				ss << std::setfill('0') << "g"
					<< std::setw(3) << futureCacheRealName.length() << futureCacheRealName
					<< std::setw(10) << ++transactionID
					<< std::setw(10) << proposedCursor
					<< std::setw(10) << len;
				linadPipe << ss.str() << std::flush;

				// Wait for reply
				while((((unsigned *) (connection->data()))[0]) != transactionID);

				// If reply is NULL, return NULL
				if(!((*connection)[sizeof(unsigned)]))
					return Z_NULL;

				curCursor = proposedCursor;
			}

			return _gets(buff, len);
		}
		/* No buffered data yet or it has been invalidated. Issue refresh command */
		else {
			std::stringstream ss;
			ss << std::setfill('0') << "g"
				<< std::setw(3) << futureCacheRealName.length() << futureCacheRealName
				<< std::setw(10) << ++transactionID
				<< std::setw(10) << proposedCursor
				<< std::setw(10) << len;
			linadPipe << ss.str() << std::flush;

			// Wait for reply
			while((((unsigned *) (connection->data()))[0]) != transactionID);

			// If reply is NULL, return NULL
			if(!((*connection)[sizeof(unsigned)]))
				return Z_NULL;

			curCursor = proposedCursor;

			return _gets(buff, len);
		}
	}
}
