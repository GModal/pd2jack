//
// the original "pdtest_jack" libpd/JACK code is:
// Copyright (c) 2014 Rafael Vega <rvega@elsoftwarehamuerto.org>
//
// also thx to "jack_large_number_ports.cpp", helpful for dynamically opening multiple ports
// by Daniel Hams
// https://github.com/danielhams
//
// "pd2jack"
// https://github.com/GModal/pd2jack
//  	by Doug Garmon
// v0.1.6 working

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <vector>
#include <queue>
#include <memory>
#include <iostream> 
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <jack/jack.h>
#include <jack/midiport.h>

#include <PdBase.hpp>
#include <PdMidiReceiver.hpp>
#include "PdObject.h"

#include "pd2jack.hpp"

using namespace std;
using namespace pd;

int sampleRate = 0;
int ticks = 0;
int Silence_Print = false;

vector<midiGlobal> mgReady;
vector<midiGlobal> mgInProgress;

jack_client_t *client = nullptr;

int AudioIn_TotalPorts = 2;
int AudioOut_TotalPorts = 2;
vector<jack_port_t*> audio_inPorts;
vector<jack_port_t*> audio_outPorts;

int MidiIn_TotalPorts = 1;
int MidiOut_TotalPorts = 1;
vector<jack_port_t*> midi_inPorts;
vector<jack_port_t*> midi_outPorts;

pd::PdBase lpd;
PdObject pdObject;
pd::Patch patch;

// buffer pointers
const int MAX_AUDIO_PORTS = 16;
float *input = nullptr;
float *output = nullptr;

const int MAX_MIDI_PORTS = 16;
void * midi_inputBuffer;

int midiChannel;
int pdMidiChannel;
int RTsendSchema = RTSEND_SYSRT;

// ========================
// = JACK AUDIO CALLBACKS =
// ========================

int process(jack_nframes_t nframes, void *arg) {
	// audio
    jack_default_audio_sample_t *sampleIn, *sampleOut;
    // midi
	jack_nframes_t event_count = 0;
	//jack_nframes_t event_index = 0;
	jack_midi_event_t in_event;
	unsigned int oPort;

	// -------------------------------------------------------------------------------------
	// ------------------------- Audio ----------------------------------------------------
    for( jack_nframes_t port = 0 ; port < AudioIn_TotalPorts ; ++port )
    {
	sampleIn = (jack_default_audio_sample_t*)jack_port_get_buffer( audio_inPorts[port], nframes );		   
		// Jack uses mono ports and pd expects interleaved buffers.
		for(unsigned int i=0; i<nframes; i++) {
		input[(i * AudioIn_TotalPorts) + port] = *sampleIn;
		sampleIn++;
		}
	}
	
   // pass audio samples to/from libpd
   ticks = nframes / 64;
   lpd.processFloat(ticks, input, output);
   
    for( jack_nframes_t port = 0 ; port < AudioOut_TotalPorts ; ++port )
    {
	sampleOut = (jack_default_audio_sample_t*)jack_port_get_buffer( audio_outPorts[port], nframes );
		// Jack uses mono ports and pd expects interleaved buffers.
		for(unsigned int i=0; i<nframes; i++){
		*sampleOut = output[(i * AudioOut_TotalPorts) + port];
		sampleOut++;
		}	
    }    
    // ---------------------------------------------------------------------------------------------
    // --------------------------JACK Midi Input ----------------------------------------------
    //            - - - - - Send input midi data TO the Pd patch  - - - 
  
    for( jack_nframes_t port = 0 ; port < MidiIn_TotalPorts ; ++port )
    {
    	int genEvent;
    	bool stillValid = true;
    	
	midi_inputBuffer = jack_port_get_buffer( midi_inPorts[port], nframes );	
	event_count = jack_midi_get_event_count( midi_inputBuffer );
			
		for(int i=0; i<event_count; i++) {
			jack_midi_event_get(&in_event, midi_inputBuffer, i);
			
			genEvent = in_event.buffer[0];					// the first (event) byte
			midiChannel = genEvent & 0x0F;					// the original chan #
			pdMidiChannel = midiChannel + (16 * port);			// Pd chan & port	
			
			// a SYSEX event
			if (genEvent== SYSEX) {
				
				for (int evCount = 0; evCount < in_event.size; evCount++) {
					unsigned char sysex_byte = in_event.buffer[evCount];
					lpd.sendSysex(port, sysex_byte);
					}
				stillValid = false;
			} 
			
			if ( stillValid) {
				// general event -- note on controllers, etc.
				stillValid = false;
				
				switch (genEvent & MIDI_STATUS_MASK) {
			
					case NOTE_OFF :
					lpd.sendNoteOn(pdMidiChannel, (unsigned char)in_event.buffer[1], 0);
					break;
						
					case NOTE_ON :
					lpd.sendNoteOn(pdMidiChannel, (unsigned char)in_event.buffer[1], (unsigned char)in_event.buffer[2]);
					break;
					
					case POLY_AFTERTOUCH :
					lpd.sendPolyAftertouch(pdMidiChannel, (unsigned char)in_event.buffer[1], (unsigned char)in_event.buffer[2]);
					break;
						
					case CONT_CTRL :
					lpd.sendControlChange(pdMidiChannel, (unsigned char)in_event.buffer[1], (unsigned char)in_event.buffer[2]);
					break;
						
					case PROGRAM_CHANGE :
					lpd.sendProgramChange(pdMidiChannel, (unsigned char)in_event.buffer[1]);
					break;
					
					case CHANNEL_AFTERTOUCH :
					lpd.sendAftertouch(pdMidiChannel, (unsigned char)in_event.buffer[1]);
					break;
						
					case PITCH_BEND :
					// Do the pitchbend math
					{
						int bend = ((in_event.buffer[2] << 7) + in_event.buffer[1]) - 8192;
						lpd.sendPitchBend(pdMidiChannel, bend);
					}
					break;	
					
					default:
					stillValid = true;
					break;
				}
			}
			if ( stillValid) {
				switch (genEvent) {
					// RT one byte
					
					// technically a system common msg, so don't send as RT
					case TUNE_REQUEST :
						
					lpd.sendMidiByte(port, (unsigned char)in_event.buffer[0]);
					break;
					
					// system real-time
					case CLOCK_TICK :
					case START_SONG :
					case CONTINUE_SONG :
					case STOP_SONG :
					case ACTIVE_SENSING :
					case SYSTEM_RESET :
					
					// Optional: send all single status bytes as "regular" bytes. Probably not really useful...
					switch (RTsendSchema) {
						// send as a regular byte
						case RTSEND_PLAIN:
						lpd.sendMidiByte(port, (unsigned char)in_event.buffer[0]);
						break;
						
						// send as RT (default)
						// add 1 to port# with sendSysRealTime()
						default:
						lpd.sendSysRealTime(port + 1, (unsigned char)in_event.buffer[0]);
						break;
					}
					break;
					
					//These are not RT, so send as regular bytes
					case MTC_QUARTER_FRAME :
					case SONG_SELECT :
					
					// (not) RT two bytes
					lpd.sendMidiByte(port, (unsigned char)in_event.buffer[0]);
					lpd.sendMidiByte(port, (unsigned char)in_event.buffer[1]);
					break;
					
					case SONG_POSITION :
					// (not) RT three bytes				
					for (int h = 0; h < 3; h++)
						lpd.sendMidiByte(port, (unsigned char)in_event.buffer[h]);
					break;
				}
			}
		}
	} 
	//---------------------------------------------------------------------------------------------------------------------------
	// 		The general output routine: libpd -> JACK out 
	//
	// MUST clear the last MIDI output buffer from the previous event, then delete that event obj from the Ready Q
	for (int qC = mgReady.size() - 1; qC >= 0; qC--) {
		
		if (mgReady[ qC ].mgBufferPtr != nullptr) {		
			jack_midi_clear_buffer(mgReady[ qC ].mgBufferPtr);
				if (mgReady[qC].mgStatus == MSG_KILL) {
					mgReady.erase(mgReady.begin() + qC );
			} 
		}
	}

	// cycle through the events in the MIDI "Ready" queue
	// Now much simplified
	for (int vIndex = 0; vIndex < mgReady.size(); vIndex++) {
		oPort = mgReady[vIndex].mgPort;
		
		if (oPort < MidiOut_TotalPorts && oPort >= 0) {
			
			int mType = mgReady[vIndex].mgType;
			mgReady[vIndex].mgBufferPtr =  (unsigned char *)jack_port_get_buffer( midi_outPorts[oPort], nframes );
			unsigned char * mglobalBuffer = jack_midi_event_reserve( mgReady[vIndex].mgBufferPtr, 0, mgReady[vIndex].mgDataV.size() );
			
			// first byte special case for Voice midi
			if (mType == MG_VOICE_MIDI) {
				unsigned int realMidiChannel = mgReady[vIndex].mgDataV[0]- oPort * 16;
				mglobalBuffer[0] = mgReady[vIndex].mgSubtype + realMidiChannel;
			}
			else 
				mglobalBuffer[0] = mgReady[vIndex].mgDataV[0];

			for (int h = 1; h < mgReady[vIndex].mgDataV.size(); h++)
			{
				mglobalBuffer[h] = mgReady[vIndex].mgDataV[h];
			}
			mgReady[vIndex].mgStatus = MSG_KILL;
		}
	}
return 0;
}

// --------------------------------------------------------------------------------------
// --------- find if obj w/ current port exists in vector
//	- - -  return port index if true, -1 if false

int findObjByPort( int port) {
int portIndex = NO_PORT_INDEX;
	
	for(int i = 0; i < mgInProgress.size(); i++)
	{
		if (mgInProgress[i].mgPort == port)
			portIndex = i;
	}
  return portIndex;
}
// -----------------------------------------------------------------------------------------
// LibPd generic callbacks
// print obj

void PdObject::print(const std::string& message) {
	if (!Silence_Print)
		cout << message << endl;
}

void PdObject::receiveFloat(const std::string& dest, float num) {
	cout << "P2J: float " << dest << ": " << num << endl;
}

void PdObject::receiveMessage(const std::string& dest, const std::string& msg, const List& list) {
	int lsft = 0;
	std::string RTSchStr = "RTschema";
	
	// Message: "RTschema"--
	//		format: "P2J RTschema <#>"
	if (msg == RTSchStr) {
		if (list.len() == 1) 
			if(list.isFloat(0)) {
				lsft = list.getFloat(0);
					   
				if ( lsft) {
				RTsendSchema = lsft;
				}
			}
	}
}

// -----------------------------------------------------------------------------------------
// LibPd MIDI callbacks
//
//  - - - - - - - -  MIDI data sent FROM the patch -> prepare this TO BE OUTPUT via JACK 
//
// 	All MIDI VOICE messages arrive complete (from LibPd objs), so can be pushed directly into "ready" Q

void PdObject::receiveNoteOn(const int channel, const int pitch, const int velocity) {
	midiGlobal mgMsg;

	mgMsg.mgType = MG_VOICE_MIDI;
	mgMsg.mgSubtype = NOTE_ON;
	mgMsg.mgPort = (int) channel / 16;
	mgMsg.mgSize = 3;

	mgMsg.mgDataV.push_back((unsigned char)channel);
	mgMsg.mgDataV.push_back((unsigned char)pitch);
	mgMsg.mgDataV.push_back((unsigned char)velocity);
	mgReady.push_back(mgMsg);
}

void PdObject::receiveControlChange(const int channel, const int controller, const int value) {
	midiGlobal mgMsg;

	mgMsg.mgType = MG_VOICE_MIDI;
	mgMsg.mgSubtype = CONT_CTRL;
	mgMsg.mgPort = (int) channel / 16;
	mgMsg.mgSize = 3;

	mgMsg.mgDataV.push_back((unsigned char)channel);
	mgMsg.mgDataV.push_back((unsigned char)controller);
	mgMsg.mgDataV.push_back((unsigned char)value);
	mgReady.push_back(mgMsg);
}

void PdObject::receiveProgramChange(const int channel, const int value) {
	midiGlobal mgMsg;

	mgMsg.mgType = MG_VOICE_MIDI;
	mgMsg.mgSubtype = PROGRAM_CHANGE;
	mgMsg.mgPort = (int) channel / 16;
	mgMsg.mgSize = 2;

	mgMsg.mgDataV.push_back((unsigned char)channel);
	mgMsg.mgDataV.push_back((unsigned char)value);
	mgReady.push_back(mgMsg);
}

void PdObject::receivePitchBend(const int channel, const int value) {
	midiGlobal mgMsg;
	unsigned int pbValue = value + 8192;
	
	mgMsg.mgType = MG_VOICE_MIDI;
	mgMsg.mgSubtype = PITCH_BEND;
	mgMsg.mgPort = (int) channel / 16;
	mgMsg.mgSize = 3;
	
	mgMsg.mgDataV.push_back((unsigned char)channel);
	mgMsg.mgDataV.push_back((unsigned char)pbValue & 0x007F);
	mgMsg.mgDataV.push_back((unsigned char)((pbValue) >> 7) & 0x007F);
	mgReady.push_back(mgMsg);
}

void PdObject::receiveAftertouch(const int channel, const int value) {
	midiGlobal mgMsg;

	mgMsg.mgType = MG_VOICE_MIDI;
	mgMsg.mgSubtype = CHANNEL_AFTERTOUCH;
	mgMsg.mgPort = (int) channel / 16;
	mgMsg.mgSize = 2;

	mgMsg.mgDataV.push_back((unsigned char)channel);
	mgMsg.mgDataV.push_back((unsigned char)value);
	mgReady.push_back(mgMsg);
}

void PdObject::receivePolyAftertouch(const int channel, const int pitch, const int value) {
	midiGlobal mgMsg;

	mgMsg.mgType = MG_VOICE_MIDI;
	mgMsg.mgSubtype = POLY_AFTERTOUCH;
	mgMsg.mgPort = (int) channel / 16;
	mgMsg.mgSize = 3;

	mgMsg.mgDataV.push_back((unsigned char)channel);
	mgMsg.mgDataV.push_back((unsigned char)pitch);
	mgMsg.mgDataV.push_back((unsigned char)value);
	mgReady.push_back(mgMsg);
}
// ------------------------------------------------------------------------------------------------------------------------------------------
// 	Internal Pd patch [midiout] output -> libpd out
// SYSEX, RT and COMMON messages are byte streams, so must be queued "in progress" then flagging as ready when complete

void PdObject::receiveMidiByte(const int port, const int byte) {
int portIndex;

midiGlobal mgMsg;
unsigned char ucByte;
bool byte_OK = true;

	ucByte = (unsigned char)(byte);
	portIndex = findObjByPort(port);
	
	if (byte_OK) {
		switch (ucByte) {
			
			// two bytes RT --------------------------------------------
			case SONG_SELECT :
			case MTC_QUARTER_FRAME :
				
			mgMsg.mgDataV.clear();
			mgMsg.mgType = MG_REALTIME_MIDI;
			mgMsg.mgStatus = MSG_IN_PROGRESS;
			mgMsg.mgPort = port;
			mgMsg.mgSize = REALTIME_2;
			mgMsg.mgCaptureCount = REALTIME_2 - 1;

			mgMsg.mgDataV.push_back( ucByte);
			mgMsg.mgBufferPtr = nullptr;
			// add to vector
			mgInProgress.push_back(mgMsg);
			break;
			
			// two bytes RT --------------------------------------------
			case SONG_POSITION :
			
			mgMsg.mgDataV.clear();
			mgMsg.mgType = MG_REALTIME_MIDI;
			mgMsg.mgStatus = MSG_IN_PROGRESS;
			mgMsg.mgPort = port;
			mgMsg.mgSize = REALTIME_3;
			mgMsg.mgCaptureCount = REALTIME_3 - 1;

			mgMsg.mgDataV.push_back( ucByte);
			mgMsg.mgBufferPtr = nullptr;
			// add to vector
			mgInProgress.push_back(mgMsg);
			break;
	
			// Single-byte RT events ------------------------------
			case TUNE_REQUEST :
			case CLOCK_TICK :
			case START_SONG :
			case CONTINUE_SONG :
			case STOP_SONG :
			case ACTIVE_SENSING :
			case SYSTEM_RESET :
				
			mgMsg.mgType = MG_REALTIME_MIDI;
			mgMsg.mgStatus = MSG_DONE;
			mgMsg.mgPort = port;
			mgMsg.mgSize = REALTIME_1;

			mgMsg.mgDataV.push_back( ucByte);
			mgMsg.mgBufferPtr = nullptr;
			// add to vector
			mgReady.push_back(mgMsg);
			break;
	
			// * * * * * * * * * * * * * * NEW sysex msg
			case SYSEX :
			// should NOT be a existing sysex msg on this port, kill the existing one
			if (portIndex != NO_PORT_INDEX) {
				if (mgInProgress[portIndex].mgStatus == MSG_IN_PROGRESS) {
					mgInProgress.erase(mgInProgress.begin() + portIndex);
				}
			}
			
			mgMsg.mgType = MG_SYSEX_MIDI;
			mgMsg.mgDataV.clear();
			mgMsg.mgStatus = MSG_IN_PROGRESS;
			mgMsg.mgPort = port;

			mgMsg.mgDataV.push_back(ucByte);
			mgMsg.mgCaptureCount = CAPTURE_MAX;
			mgMsg.mgBufferPtr = nullptr;
			// add to vector
			mgInProgress.push_back(mgMsg);

			break;
			//  * * * * * * * * * * * * * * END of sysex msg
			case SYSEX_END :
			if (portIndex != NO_PORT_INDEX) {
				if (mgInProgress[portIndex].mgStatus == MSG_IN_PROGRESS) {
					mgInProgress[portIndex].mgDataV.push_back(ucByte);
					mgInProgress[portIndex].mgStatus = MSG_DONE;
				}
			}
			break;
			
			// * * * * * * * * * * * * * * * CONTINUE (FILL) the "in process" msg
			default:
			if (ucByte < 128) {
				if (portIndex != NO_PORT_INDEX) {
					if (mgInProgress[portIndex].mgStatus == MSG_IN_PROGRESS) {
						mgInProgress[portIndex].mgCaptureCount--;
						mgInProgress[portIndex].mgDataV.push_back(ucByte);
						
						//cout << "Stage 4   fill :  B: "<< (unsigned int)ucByte  << "\tCnt: "  << mgInProgress[portIndex].mgCaptureCount
						// <<"\tPort: " << mgInProgress[portIndex].mgPort << "\tpIndex: " << portIndex  << endl;
						
						// check count for "set" length data
						if (mgInProgress[portIndex].mgCaptureCount == 0)
							mgInProgress[portIndex].mgStatus = MSG_DONE;	
					}
				}
			}
			break;
		}
	}
	// --------------------------------------------------------------------------------------------------------------------
	// * * * * * * * * * * * * *  READY to send Sysex/RT/
	// This just clears the "set" input buffers & pushes them on the output vector.
	// (the queue was originally for sysex data packets - not happening currently, but leaving as-is)
	
	//push on Q
	for (int qCount = 0; qCount < mgInProgress.size() ; qCount++) {	
		if (mgInProgress[ qCount].mgStatus == MSG_DONE) {
			//cout << "Stage 4-\tCapture done, push on Ready Q" << endl << endl; 
			mgReady.push_back(mgInProgress[ qCount ]);
			mgInProgress[ qCount].mgStatus = MSG_KILL;
		}
	}
	
	// delete any objs flagged for KILL
	for (int qCount = mgInProgress.size() - 1; qCount >= 0; qCount--) {
		if ( mgInProgress[ qCount ].mgStatus == MSG_KILL) {
			mgInProgress.erase(mgInProgress.begin() + qCount);
		}
	}
}

// --------------------------------------------------------------------------------------
// JACK calls this shutdown_callback if the server ever shuts down or
// decides to disconnect the client.

void
jack_shutdown (void *arg)
{
	exit (1);
}

// ---------------------------------------- Init LibPd ------------------------------
void initPd(int srate) {
// init pd
// initialize 2 inputs, 2 outputs, @ samplerate
	if(!lpd.init(AudioIn_TotalPorts, AudioOut_TotalPorts, srate)) {
		std::cerr << "Could not init pd" << std::endl;
		exit(1);
	}
	// receive messages from pd
	lpd.setReceiver(&pdObject);
	lpd.setMidiReceiver(&pdObject);
	// libpd midi callbacks
	lpd.receiveMidi();
	
	// listen for "P2J" msgs
	lpd.subscribe("P2J");

	// send DSP 1 message to pd (ON)
	lpd.computeAudio(true);
	
	// add "pd" subdir to path -- to find abstractions
	lpd.addToSearchPath("./pd");
	lpd.addToSearchPath("~/pd2jack/pd");
}

// ----------------------------------- Init JACK client -----------------------------
int initJack(char * jname) {
	
	const char *server_name = nullptr;
	jack_options_t options = JackNullOption;
	jack_status_t status;
   
   // create client
   client = jack_client_open(jname, options, nullptr);
   
	if (client == nullptr) {
	std::cerr << "Could not open JACK client" << std::endl;
	exit(1);
	}
	// return sample rate from jack server
	return(jack_get_sample_rate(client));
}

// ------------------------------------ initialize JACK ports ------------------

void initJPorts() {
   
   // register audio process callback
   jack_set_process_callback(client, process, 0);

	// JACK shutdown callback
   jack_on_shutdown (client, jack_shutdown, 0);
   
    // ----------------------------------------------------------- create our AUDIO JACK ports 
    // adapted from "jack_large_number_of_ports.cpp"
    //
    // ------------------------------------  input ports
    for( jack_nframes_t i = 0 ; i < AudioIn_TotalPorts ; ++i )
    {
	stringstream ss( stringstream::out );
	ss << "AudioIn-" << i;
	string port_name( ss.str() );
	jack_port_t * tmp_port = jack_port_register( client, port_name.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0 );
	
	if( tmp_port == nullptr )
	{
	    cerr << "Failed to register input port " << i << endl;
	    exit(1);
	}
	audio_inPorts.push_back( tmp_port );
    }

	// ------------------------------------------ output ports
    for( jack_nframes_t o = 0 ; o < AudioOut_TotalPorts ; ++o )
    {
	stringstream ss( stringstream::out );
	ss << "AudioOut-" << o;
	string port_name( ss.str() );
	jack_port_t * tmp_port = jack_port_register( client, port_name.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0 );
	if( tmp_port == nullptr )
	{
	    cerr << "Failed to register output port " << o << endl;
	    exit(1);
	}
	audio_outPorts.push_back( tmp_port );
    }

    // ---------------------------------------------------------  create our MIDI JACK ports 
    // ----------------------------------------  input ports
    for( jack_nframes_t i = 0 ; i < MidiIn_TotalPorts ; ++i )
    {
	stringstream ss( stringstream::out );
	ss << "MidiIn-" << i;
	string port_name( ss.str() );
	jack_port_t * tmp_port = jack_port_register( client, port_name.c_str(), JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0 );
	if( tmp_port == nullptr )
	{
	    cerr << "Failed to register input port " << i << endl;
	    exit(1);
	}
	midi_inPorts.push_back( tmp_port );
    }
    
	// ------------------------------------------ output ports
    for( jack_nframes_t o = 0 ; o < MidiOut_TotalPorts ; ++o )
    {
	stringstream ss( stringstream::out );
	ss << "MidiOut-" << o;
	string port_name( ss.str() );
	jack_port_t * tmp_port = jack_port_register( client, port_name.c_str(), JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0 );
	if( tmp_port == nullptr )
	{
	    cerr << "Failed to register output port " << o << endl;
	    exit(1);
	}
	midi_outPorts.push_back( tmp_port );
    }
   // go!
   if (jack_activate (client)) {
      std::cout << "Could not activate client";
      exit(1);
   }
}

// ---------------------------------- exit gracefully on shutdown ---------------------------
void exitwGrace() {
	// close pd patch
	if(patch.isValid( ))
		lpd.closePatch(patch);

	if (client != nullptr) {
		jack_deactivate(client);
		// close jack client
		jack_client_close(client);
		}
	if (input == nullptr )
		delete[] input;
	if (output == nullptr )
		delete[] output;
}
// ----------------------------- Parse colon separated numbers -----------------------
int colontokener(char *s, iP2j *duo) {

	char *tok = strtok(s, ":");
	duo->data[0] = atoi(tok);
	tok = strtok(nullptr, ":");
	duo->data[1] = atoi(tok);
return(0);
}

// ------------------------------- Parse Parameters ----------------------------------
int paramtokener(char *s) {
int cnt = 0;
float parameter;

char *tok = strtok(s, " ");
parameter = strtod(tok, nullptr);

lpd.startMessage();
lpd.addFloat(parameter);

	while (tok) {
		tok = strtok(nullptr, " ");
		if (tok) { 
			cnt++;
		    parameter = strtod(tok, nullptr);
		    lpd.addFloat(parameter);
		    }
	}
lpd.finishList("param");
return(cnt);
}

void helpMsg() {
	std::cout << "Help: pd2jack <options> -p 'file.pd' <'param strs'>" << endl;
	std::cout << "-p : Pd patch Name" << endl;
	std::cout << " Optional args:" << endl;
	std::cout << "-a : Audio ports (0-16) - default: 2:2 " << endl;
	std::cout << "-h : Help msg " << endl;
	std::cout << "-m : Midi ports (0-16) - default: 1:1" << endl;
	std::cout << "-n : JACK port Name" << endl;
	std::cout << "-s : Silence Pd [print] objects" << endl;
	std::cout << "-v : verbosity (0-1) - default: 0" << endl;
}

// ------------  Define the function to be called when ctrl-c (SIGINT) is sent to process
void signal_callback_handler(int signum) {
   std::cout << endl;

   exitwGrace();
   // Terminate program   
   exit(signum);
}

int main (int argc, char *argv[]) {
	int opt;
	int paramCnt = 0;
	int legal = 1;
	int verbose = 0;
	int getcnt = 1;
	jack_nframes_t bufsize;
	char client_name[] = "pd2jack";
	char *jName = nullptr;
	char patchName[] = ".pd";
	char *pName = nullptr;
	iP2j Pcount;
   
	jName = client_name;
	pName = patchName;
	
	// Register signal and signal handler -- grab the ctrl-c   
	signal(SIGINT, signal_callback_handler);
   
	if ( !(argc > 1 ))  { 	 
			helpMsg();
		}
		else {
		
		  while ((opt = getopt(argc, argv, "a:hm:n:p:sv:")) != -1) 
		  {
			 switch (opt) 
			 {
			  	case 'm':
				colontokener(optarg, &Pcount);
				if (Pcount.data[0] < 0 || Pcount.data[0] > MAX_MIDI_PORTS)
					MidiIn_TotalPorts = 2;
					else 
						MidiIn_TotalPorts = Pcount.data[0];
				if (Pcount.data[1] < 0 || Pcount.data[1] > MAX_MIDI_PORTS)
					MidiIn_TotalPorts = 2;
					else 
						MidiOut_TotalPorts = Pcount.data[1];	
				getcnt = optind;
				break;
				
			  	case 'a':
				colontokener(optarg, &Pcount);
				if (Pcount.data[0] < 0 || Pcount.data[0] > MAX_AUDIO_PORTS)
					AudioIn_TotalPorts = 2;
					else 
						AudioIn_TotalPorts = Pcount.data[0];
				if (Pcount.data[1] < 0 || Pcount.data[1] > MAX_AUDIO_PORTS)
					AudioIn_TotalPorts = 2;
					else 
						AudioOut_TotalPorts = Pcount.data[1];	
				getcnt = optind;
				break;
				
				case 'n':
				jName = optarg;
				getcnt = optind;
				break;
				
				case 'p':
				pName = optarg;
				getcnt = optind;
				break;

				case 'v':
				verbose = atoi(optarg);
				if (verbose < 0 || verbose > 1)
					verbose = 0;
				getcnt = optind;
				break;

				case 's':
				Silence_Print = true; 
				getcnt = optind;
				break;
				
				case 'h':
				legal = 0;
				break;
								
			  	case '?':
			  	legal = 0;
				break;
			 }
		  }

		std::string filenm = pName;
		const char * fstr = filenm.c_str();
		
		if( access( fstr, F_OK ) == 0 && legal) {
    		// 		OK, file exists
			sampleRate = initJack(jName);
			initPd(sampleRate);
			// 		load the Pd patch
	   		patch = lpd.openPatch(pName, "./");
	   			
			// Always check to see if file opening succeeded
			if(patch.isValid( )) {
				// 		init the port buffers
				bufsize = jack_get_buffer_size(client);
				
				if (AudioIn_TotalPorts)
					input = new float[bufsize*AudioIn_TotalPorts*sizeof(float)];
				if (AudioOut_TotalPorts)
					output = new float [bufsize*AudioOut_TotalPorts*sizeof(float)];
					
				initJPorts();			// 	initialize the Audio ports			
				if (getcnt < argc ) {
					for (int i = getcnt; i < argc; i++) {
						if (paramtokener(argv[i]))
							paramCnt++;
					}
				}
				if (verbose){
					std::cout << "-Audio: " << AudioIn_TotalPorts << ":" << AudioOut_TotalPorts;
					std::cout << " Midi: " << MidiIn_TotalPorts << ":" << MidiOut_TotalPorts;
					if (paramCnt) 
						std::cout << " -Params: " << paramCnt;
					std::cout << endl;
				}
				// --------------------------------------------------------------------------
				// 		Loop until it's killed with Ctrl+C
				while(1){
					lpd.receiveMessages();
					usleep(150);
				}		  
			}
		}
		else 
		helpMsg();
	}
	exitwGrace();
	return 0;
}
