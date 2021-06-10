// pd2jack.h
// -- Doug Garmon
#include <vector>

using namespace std;

enum midiMessages {
	// MIDI consts
	MIDI_STATUS_MASK = 0xF0,
	
	// ----------------------------------
	// Type :
	// midiGlobal mgType consts
	MG_VOICE_MIDI = 1,
	MG_SYSEX_MIDI = 2,
	MG_REALTIME_MIDI = 3,
	MG_COMMON_MIDI = 4,
	
	// ----------------------------------
	// Subtype :
	// ACTUAL MIDI DATA BYTES
	
	// MIDI VOICE 
	NOTE_OFF = 0x80,
	NOTE_ON	= 0x90,
	POLY_AFTERTOUCH = 0xA0,
	CONT_CTRL = 0xB0,
	PROGRAM_CHANGE = 0xC0,
	CHANNEL_AFTERTOUCH = 0xD0,
	PITCH_BEND = 0xE0,
	
	// MIDI SYSTEM COMMON 
	SYSEX = 0xF0,
	SYSEX_END = 0xF7,
	SONG_POSITION = 0xF2,
	SONG_SELECT = 0xF3,
	TUNE_REQUEST = 0xF6,
	
	// MIDI REAL-TIME
	CLOCK_TICK = 0xF8,
	START_SONG = 0xFA,
	CONTINUE_SONG = 0xFB,
	STOP_SONG = 0xFC,
	ACTIVE_SENSING = 0xFE,
	SYSTEM_RESET = 0xFF,
	
	// --------------------------------------
	// Extra:
	// realtime (total # of bytes)
	REALTIME_1 = 1,
	REALTIME_2 = 2,
	REALTIME_3 = 3,
	CAPTURE_MAX = 10000000,
	
	// Status consts	
	MSG_FREE = 0,
	MSG_IN_PROGRESS = 1, 
	MSG_DONE = 2,
	MSG_KILL = 3,
	
	// --------------------------------------
	// port values
	NO_PORT_INDEX = -1,
	
	// --------------------------------------
	// RT send schema
	RTSEND_NOOP = 0,
	RTSEND_SYSRT = 1,	
	RTSEND_PLAIN = 2,
};

// ints
class iP2j {
	public:
	int data[4] = {0,0,0,0};
	
	iP2j(void) {
	}	
};

// Replaced other MIDI classes (three) with one
class midiGlobal {
public:
	public:
	unsigned char mgType;
	int mgSubtype;
	unsigned char mgSubExtra;
	
	void * mgBufferPtr;
	int mgPort;
	int mgStatus;
	int mgSize;
	int mgCaptureCount;
	std::vector<unsigned char> mgDataV;
	
	midiGlobal(void) {
		mgBufferPtr = nullptr;
		mgPort = NO_PORT_INDEX;
		mgStatus = 0;
		mgType = 0;
		mgSize = 0;
		mgType = 0;
		mgCaptureCount = CAPTURE_MAX;
	}
};