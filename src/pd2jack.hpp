// pd2jack project
// pd2jack.h
// -- Doug Garmon
#ifndef PD2JACK_H
#define PD2JACK_H 

#include <vector>
#include <string>
#include <unistd.h>
#include <assert.h>
#include <filesystem>
#include <jack/jack.h>
#include <jack/midiport.h>
#include "PdObject.hpp"
#include <PdBase.hpp>
#include <PdMidiReceiver.hpp>
#include <lo/lo.h>

using namespace std;
//using std::filesystem::current_path;

const int MAX_AUDIO_PORTS = 16;
const int MAX_MIDI_PORTS = 16;
const string versionString = "0.3.9";

enum midiGlobalTypes {
	// MIDI consts
	MIDI_STATUS_MASK = 0xF0,
	
	// ----------------------------------
	// Type :
	// midiGlobal mgType consts
	MG_VOICE_MIDI = 1,
	MG_SYSEX_MIDI,
	MG_REALTIME_MIDI,
	MG_COMMON_MIDI,
	
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
	MTC_QUARTER_FRAME = 0xF1,
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
	REALTIME_2,
	REALTIME_3,
	CAPTURE_MAX = 10000000,
	
	// Status consts	
	MSG_FREE = 0,
	MSG_IN_PROGRESS, 
	MSG_DONE,
	MSG_KILL,
	
	// --------------------------------------
	// port values
	NO_PORT_INDEX = -1,
};

//------------------------------------------------------------------------------------------
// pd file class
class patchFile {
public:
	string patchname;
	pd::Patch patch;
	int ID;
	
	patchFile(void) {
		ID = -1;	
	}
};
//------------------------------------------------------------------------------------------
// Base info class for pd2jack
class p2jBase {
	public:
	// variables
	bool running;
	int sleepyTime;
	int silencePrint;
	int interactive ;
	int OSCmode;
	int verboseLevel;
	int showPrompt;
	int bypass;
	
	int serverID;				// identity #: set this explicity -- NOT unique unless it's set to be
	
	string currentPath;			// current path, saved when pd2jack invoked
	string iPrompt;			// prompt string, when active
	string pPair;				// name of parameter pair receiver - "param" normally
	
	// OSC "address" names -- NOT IP address, uses *nix style file system conventions ("/name")
	string clientRootName;
	string serverRootName;

	string OSC_serv_url;			// lo-formatted URL text str, from server
	string OSC_send_url;			// lo-formatted URL text str, from outgoing
	
	lo_address oscOut;				// lo_address struct, for OSC output
	lo_server_thread st;				// lo server thread struct, for OSC input
	
	// OSC address txt format, for liblo
	string OSC_serverAddr;			// default multicast gets set by setupOSC()
	string OSC_sendAddr;			// for multicast, default is 224.0.0.1 multicast group
	
	string OSCport_Out;			// OSC output port #
	string OSCport_In;				// OSC input port #
	
	// JACK client
	jack_client_t *client;			// JACK client
	char *clientName;				// client name

	// LibPd objects
	pd::PdBase lpd;
	PdObject pdObject;

	vector<patchFile> patches;		// all the open pd patches
	
	p2jBase(void) {
		running = true;
		sleepyTime = 25000;
		silencePrint = false;
		interactive = false;
		OSCmode = false;
		verboseLevel = 0;
		showPrompt =false;
		bypass = 0;
		client = nullptr;
		
		currentPath = "./";
		iPrompt = "> ";
		pPair = "param";
		
		clientRootName = "/P2Jcli/";
		serverRootName = "/P2Jsrv/";
		serverID = 0;
		
		oscOut = NULL;
		st = NULL;
		
		//OSC_serverAddr = "224.0.0.1"; 		// multicast group -- default NOT set explicitly
		//OSC_sendAddr  = "224.0.0.1";			// default multicast, NULL is OK for local
		
		OSCport_In = "20331";				// server port -- input / listen		
		OSCport_Out = "20341";				// send -- talk, range 20341+
	}

	// ------------------------------- open a patch
	// 				check if file exist & is valid
	// 				Push on vector if valid
	//
	//			ID #s start at 0 and grow sequentially,  
	//			returns -1 on fail
	int pj_openPatch(char * patchName) {
	int cool = -1;
	patchFile pfile;	
	string pName; 		// string to hold name
	string fullPath;		// build full path for access()

	pName.assign(patchName);
	// access() test needs full path, server thread has different path base
	fullPath = currentPath + "/" + pName;
	
		if( access( (char *)fullPath.c_str(), F_OK ) == 0) {
			// 		OK, file exists
			pfile.patch = lpd.openPatch(pName,  currentPath);
			
			if (pfile.patch.isValid( )) {
				pfile.patchname = pName;
				pfile.ID = patches.size();
				cool = pfile.ID;
				// add this patchFile to vector of patches
				patches.push_back(pfile);
			}
		}
	return(cool);
	}
	// ------------------------------- close last patch
	int pj_closeLastPatch() {
	int ret = 0;
		if(patches.size() > 0) {
			if(patches.back().patch.isValid( )) {
				lpd.closePatch(patches.back().patch);
				patches.pop_back();
				ret = 1;
			}
		}
	return ret;	
	}
	// ------------------------------ close all open patches 
	int pj_closeAllPatches() {
	int len;
	len = patches.size();
	
		for (int i = 0; i < len; i++) {
			if(patches[i].patch.isValid( ))
				lpd.closePatch(patches[i].patch);
		}
		patches.clear();
	return len;
	}
	// ------------------------------close a patch by index 
	int pj_closePatch(int index) {
	int len;
	int ret = -1;
	len = patches.size();
	
		if(patches.size() > 0) {
			if (index > -1) {
				if (index < len) {		
					if(patches[index].patch.isValid( ))
						lpd.closePatch(patches[index].patch);
					patches.erase(patches.begin()+index);
					ret = index;
				}
			}
		}
	return ret;
	}
	// ---------------------- get and save current path
	void pj_getCurrentPath() {
		    char tmp[512];
		    if (getcwd(tmp, 512) != 0)
		    	currentPath.assign(tmp);
	}
};
//----------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------
// Replaced other MIDI classes (three) with one
class midiGlobal {
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
// ------------------------------------------------------------------------
// ints
class iP2j {
	public:
	int data[4] = {0,0,0,0};
	
	iP2j(void) {
	}	
};

#endif
