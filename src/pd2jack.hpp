// pd2jack.h
// -- Doug Garmon
#include <vector>

using namespace std;

enum midiMessages {
	// MIDI consts
	NOTE_OFF = 0x80,
	NOTE_ON	= 0x90,
	POLY_AFTERTOUCH = 0xA0,
	CONT_CTRL = 0xB0,
	PROGRAM_CHANGE = 0xC0,
	CHANNEL_AFTERTOUCH = 0xD0,
	PITCH_BEND = 0xE0,
	
	MIDI_STATUS_MASK = 0xF0,
	
	SYSEX = 0xF0,
	SYSEX_END = 0xF7,
	SONG_POSITION = 0xF2,
	SONG_SELECT = 0xF3,
	TUNE_REQUEST = 0xF6,
	TIMING_TICK = 0xF8,
	START_SONG = 0xFA,
	CONTINUE_SONG = 0xFB,
	STOP_SONG = 0xFC,
	ACTIVE_SENSING = 0xFE,
	SYSTEM_RESET = 0xFF,
	
	// SYSEX consts
	
	// realtime
	REALTIME_FALSE = 0,
	REALTIME_1 = 1,
	REALTIME_2 = 2,
	REALTIME_3 = 3,
	
	// status
	SYSEX_FREE = 0,
	SYSEX_IN_PROGRESS = 1, 
	SYSEX_DONE = 2,
	SYSEX_KILL = 3,
	SYSEX_PREKILL =4,
	SYSEX_GARBAGE = 5,
	
	// port values
	SYSEX_NO_PORT_INDEX = -1
};

class iP2j {
	// ints
	public:
	int A;
	int B;
	int C;
	
	iP2j(void) {
		A = 0;
		B = 0;
		C = 0;
	}	
};

class midiMsgP2J {
	public:
	unsigned char type;
	int port;
	int size;
	int realtime;
	void * bufferPtr;
	unsigned char data[3];
	
	midiMsgP2J(void) {
	type = 0;
	size = 0;
	realtime = REALTIME_FALSE;
	port = SYSEX_NO_PORT_INDEX;
	bufferPtr = nullptr;
	}
};

class midiSysexP2J {
	public:
	void * bufferPtr;
	int port;
	int status;
	int sysexSize;
	std::vector<unsigned char> dataV;

	midiSysexP2J(void) {
		port = SYSEX_NO_PORT_INDEX;
		sysexSize = 0;
		status = SYSEX_FREE;
		bufferPtr = nullptr;
	}
};

