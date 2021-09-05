// "ipc.hpp"
// for
//	pd2jack
//		Doug Garmon
#ifndef IPC_H
#define IPC_H 
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <PdTypes.hpp>
#include "pd2jack.hpp"

using namespace pd;

enum OSCtypeval {
	OSC_UNDEF = 0,
	OSC_DIRECT,		// Direct inject OSC msg
	OSC_MSG,			// general msg
	OSC_MSG_STR,		// convert Symbols to strings
	OSC_ICMD,			// P2J internal cmd
	OSC_PPAIR
};

enum OSC_len {
	OSC_PPAIR_LEN = 2,
	OSC_FNAME_LEN = 1
};

// protos
void sendOSC(p2jBase *, int, string, const List&);
void processOSCpp(float, float);

// lo osc protos
int setupOSC(p2jBase *);
int closeOSC(p2jBase *);
#endif