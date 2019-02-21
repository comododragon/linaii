#ifndef NODELATENCY_H
#define NODELATENCY_H

#include "profile_h/auxiliary.h"

class NodeLatency {
public:
	virtual ~NodeLatency() { }
	static NodeLatency *createInstance();

	virtual unsigned getLatency(unsigned opcode) = 0;
};

class XilinxNodeLatency : public NodeLatency {
	enum {
		LATENCY_LOAD = 2,
		LATENCY_STORE = 1,
		LATENCY_ADD = 1,
		LATENCY_SUB = 1,
		LATENCY_MUL32 = 6,
		LATENCY_DIV32 = 36,
		LATENCY_FADD32 = 5,
		LATENCY_FSUB32 = 5,
		LATENCY_FMUL32 = 4,
		LATENCY_FDIV32 = 16,
		LATENCY_FCMP = 1
	};
	enum {
		BRAM_PORTS_R = 2,
		BRAM_PORTS_W = 1
	};

public:
	//XilinxNodeLatency() { }
	//~XilinxNodeLatency() { }

	unsigned getLatency(unsigned opcode);
};

#endif
