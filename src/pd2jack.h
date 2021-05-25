// pd2jack.h
// -- Doug Garmon

using namespace std;

enum midiMessages {
	NOTE_OFF = 0x80,
	NOTE_ON	= 0x90,
	POLY_AFTERTOUCH = 0xA0,
	CONT_CTRL = 0xB0,
	PROGRAM_CHANGE = 0xC0,
	CHANNEL_AFTERTOUCH = 0xD0,
	PITCH_BEND = 0xE0,
	MIDI_STATUS = 0xF0
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

class midiMsgPJ {
	public:
	unsigned char type;
	unsigned int size;
	unsigned char data[3];
};

