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

#include <stdio.h>
#include <fcntl.h>
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
#include "PdObject.hpp"
#include "interactive.hpp"
#include "pd2jack.hpp"
#include "hash.hpp"
#include "ipc.hpp"

using namespace std;
using namespace pd;

p2jBase p2jb;

vector<midiGlobal> mgReady;
vector<midiGlobal> mgInProgress;

// JACK ports
int AudioIn_TotalPorts = 2;
int AudioOut_TotalPorts = 2;
vector<jack_port_t*> audio_inPorts;
vector<jack_port_t*> audio_outPorts;

int MidiIn_TotalPorts = 1;
int MidiOut_TotalPorts = 1;
vector<jack_port_t*> midi_inPorts;
vector<jack_port_t*> midi_outPorts;

// Audio buffer pointers
float *input = nullptr;
float *output = nullptr;
float *swapIO = nullptr;

// ========================
// = JACK AUDIO CALLBACKS =
// ========================
int process(jack_nframes_t nframes, void *arg) {
	// audio
	jack_default_audio_sample_t *sampleIn, *sampleOut;
    	// midi
	jack_nframes_t event_count = 0;
	jack_midi_event_t in_event;
	unsigned int oPort;
	int ticks = nframes / 64;
	
	// -------------------------------------------------------------------------------------
	// ------------------------- Audio ----------------------------------------------------
	
	// swap I/O pointers for bypassing
	swapIO = input;
	
	for( jack_nframes_t port = 0 ; port < AudioIn_TotalPorts ; ++port )
	{
	sampleIn = (jack_default_audio_sample_t*)jack_port_get_buffer( audio_inPorts[port], nframes );		   
		// Jack uses mono ports and pd expects interleaved buffers.
		for(unsigned int i=0; i<nframes; i++) {
		input[(i * AudioIn_TotalPorts) + port] = *sampleIn;
		sampleIn++;
		}
	}
		
	if (p2jb.bypass == 0) {		
		// pass audio samples to/from libpd
		p2jb.lpd.processFloat(ticks, input, output);
		swapIO = output;
	}
	   
	for( jack_nframes_t port = 0 ; port < AudioOut_TotalPorts ; ++port )
	{
	sampleOut = (jack_default_audio_sample_t*)jack_port_get_buffer( audio_outPorts[port], nframes );
		// Jack uses mono ports and pd expects interleaved buffers.
		for(unsigned int i=0; i<nframes; i++){
		*sampleOut = swapIO[(i * AudioOut_TotalPorts) + port];
		sampleOut++;
		}	
	}    
    // ---------------------------------------------------------------------------------------------
    // --------------------------JACK Midi Input ----------------------------------------------
    //            - - - - - Send input midi data TO the Pd patch  - - - 
  
    for( jack_nframes_t port = 0 ; port < MidiIn_TotalPorts ; ++port )
    {
    	int midiChannel;
    	int pdMidiChannel;
    	void * midi_inputBuffer;
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
					p2jb.lpd.sendSysex(port, sysex_byte);
					}
				stillValid = false;
			} 
			
			if ( stillValid) {
				// general event -- note on controllers, etc.
				stillValid = false;
				
				switch (genEvent & MIDI_STATUS_MASK) {
			
					case NOTE_OFF :
					p2jb.lpd.sendNoteOn(pdMidiChannel, (unsigned char)in_event.buffer[1], 0);
					break;
						
					case NOTE_ON :
					p2jb.lpd.sendNoteOn(pdMidiChannel, (unsigned char)in_event.buffer[1], (unsigned char)in_event.buffer[2]);
					break;
					
					case POLY_AFTERTOUCH :
					p2jb.lpd.sendPolyAftertouch(pdMidiChannel, (unsigned char)in_event.buffer[1], (unsigned char)in_event.buffer[2]);
					break;
						
					case CONT_CTRL :
					p2jb.lpd.sendControlChange(pdMidiChannel, (unsigned char)in_event.buffer[1], (unsigned char)in_event.buffer[2]);
					break;
						
					case PROGRAM_CHANGE :
					p2jb.lpd.sendProgramChange(pdMidiChannel, (unsigned char)in_event.buffer[1]);
					break;
					
					case CHANNEL_AFTERTOUCH :
					p2jb.lpd.sendAftertouch(pdMidiChannel, (unsigned char)in_event.buffer[1]);
					break;
						
					case PITCH_BEND :
					// Do the pitchbend math
					{
						int bend = ((in_event.buffer[2] << 7) + in_event.buffer[1]) - 8192;
						p2jb.lpd.sendPitchBend(pdMidiChannel, bend);
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
						
					p2jb.lpd.sendMidiByte(port, (unsigned char)in_event.buffer[0]);
					break;
					
					// system real-time
					case CLOCK_TICK :
					case START_SONG :
					case CONTINUE_SONG :
					case STOP_SONG :
					case ACTIVE_SENSING :
					case SYSTEM_RESET :

					// removed realtime option to send as regular midi
					p2jb.lpd.sendSysRealTime(port + 1, (unsigned char)in_event.buffer[0]);
					break;
					
					//These are not RT, so send as regular bytes
					case MTC_QUARTER_FRAME :
					case SONG_SELECT :
					
					// (not) RT two bytes
					p2jb.lpd.sendMidiByte(port, (unsigned char)in_event.buffer[0]);
					p2jb.lpd.sendMidiByte(port, (unsigned char)in_event.buffer[1]);
					break;
					
					case SONG_POSITION :
					// (not) RT three bytes				
					for (int h = 0; h < 3; h++)
						p2jb.lpd.sendMidiByte(port, (unsigned char)in_event.buffer[h]);
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
	if (!p2jb.silencePrint)
		cout << message << endl;
}

void PdObject::receiveFloat(const std::string& dest, float num) {
	cout << "P2J.xx: float " << dest << ": " << num << endl;
}

//--------------------------------------------------------------------------------------------
void doFromList(string msg, const List& list) {
stringstream buildStream;
string tStr;

	buildStream << msg;
	for (int i = 0; i <list.len(); i++)  {
		if(list.isFloat(i))
			buildStream << " " << list.getFloat(i);
		if(list.isSymbol(i))
			buildStream << " " << list.getSymbol(i);
	}
	//buildStream << std::endl;
	tStr = buildStream.str();
	do_I_Mode (tStr, &p2jb);
}
//-------------------------------------------------------------------------------------------
// OSC msgs from LibPd patch
// 	to send ParamPair, using Pd object in patch : [sendOSC pp <param#>]
//
// Sends the value on 1st inlet as a Pd msg, in format:
//	"P2JoS pp <f0> <f1>"
// where: 
// 	"P2JoS" is the Pd recv addr for "send"		: dest
//	"pp" is the cmd						: msg
//	<f0> <f1>								: list -- the parameter pair values
//
// This will be output over OSC as
//	"/P2Jsrv/<thisID>/pp <f0> <f1>"
//
// where: <thisID> is the int ID# for this server

void PdObject::receiveMessage(const std::string& dest, const std::string& msg, const List& list) {
	if(dest == "P2JoS")
			sendOSC(&p2jb, OSC_MSG, msg, list);
	// if recv'd here, convert Symbols to Strings in outgoing OSC msg
	else if(dest == "P2Jos")
			sendOSC(&p2jb, OSC_MSG_STR, msg, list);
	else if (dest == "P2J") {
		if (msg[0] == '@') {
			doFromList(msg, list);
			//cout << "P2J msg received" << endl;
		}
	}
}

void PdObject::receiveList(const std::string& dest, const List& list) {
	
	if(dest == "P2J List: ") {
		cout << "len: " << list.len() << " " << list << endl;
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
				
			mgMsg.mgType = MG_REALTIME_MIDI;
			mgMsg.mgStatus = MSG_IN_PROGRESS;
			mgMsg.mgPort = port;
			mgMsg.mgSize = REALTIME_2;
			mgMsg.mgCaptureCount = REALTIME_2 - 1;

			mgMsg.mgDataV.push_back( ucByte);
			// add to vector
			mgInProgress.push_back(mgMsg);
			break;
			
			// two bytes RT --------------------------------------------
			case SONG_POSITION :
			
			mgMsg.mgType = MG_REALTIME_MIDI;
			mgMsg.mgStatus = MSG_IN_PROGRESS;
			mgMsg.mgPort = port;
			mgMsg.mgSize = REALTIME_3;
			mgMsg.mgCaptureCount = REALTIME_3 - 1;

			mgMsg.mgDataV.push_back( ucByte);
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
			mgMsg.mgStatus = MSG_IN_PROGRESS;
			mgMsg.mgPort = port;
			mgMsg.mgDataV.push_back(ucByte);
			
			// STUPID high count-down number for sysex transfers
			mgMsg.mgCaptureCount = CAPTURE_MAX; 
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
// initialize with ports, @ samplerate
	if(!p2jb.lpd.init(AudioIn_TotalPorts, AudioOut_TotalPorts, srate)) {
		std::cerr << "Could not init pd" << std::endl;
		exit(1);
	}
	// receive messages from pd
	p2jb.lpd.setReceiver(&p2jb.pdObject);
	p2jb.lpd.setMidiReceiver(&p2jb.pdObject);
	// libpd midi callbacks
	p2jb.lpd.receiveMidi();
	
	// listen for "P2J" msgs
	// P2J is the general send "target" name (for @cmds)
	p2jb.lpd.subscribe("P2J");
	// P2joS is the pd2jack OSC send "target" - OSC msgs sent from patch go here
	p2jb.lpd.subscribe("P2JoS");
	// send target, converts Pd symbols to strings
	p2jb.lpd.subscribe("P2Jos");

	// send DSP 1 message to pd (ON)
	p2jb.lpd.computeAudio(true);
	
	// add "pd" subdir to path -- to find abstractions
	p2jb.lpd.addToSearchPath("./pd");
	//p2jb.lpd.addToSearchPath("~/.pd2jack/pd");
}

// ----------------------------------- Init JACK client -----------------------------
int initJack(char * jname) {
	
	const char *server_name = nullptr;
	jack_options_t options = JackNullOption;
	jack_status_t status;
   
   // create client
   p2jb.client = jack_client_open(jname, options, nullptr);
   
	if (p2jb.client == nullptr) {
	std::cerr << "Could not open JACK client" << std::endl;
	exit(1);
	}
	
	p2jb.clientName = jack_get_client_name( p2jb.client);
	// return sample rate from jack server
	return(jack_get_sample_rate(p2jb.client));
}

// ------------------------------------ initialize JACK ports ------------------

void initJPorts() {
   
   // register audio process callback
   jack_set_process_callback(p2jb.client, process, 0);

  // JACK shutdown callback
   jack_on_shutdown (p2jb.client, jack_shutdown, 0);
   
    // --------------------------------create our AUDIO JACK ports -----------------------------
    // adapted from "jack_large_number_of_ports.cpp"
    //
    // ------------------------------------  input ports
    for( jack_nframes_t i = 0 ; i < AudioIn_TotalPorts ; ++i )
    {
	stringstream ss( stringstream::out );
	ss << "AudioIn-" << i;
	string port_name( ss.str() );
	jack_port_t * tmp_port = jack_port_register( p2jb.client, port_name.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0 );
	
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
	jack_port_t * tmp_port = jack_port_register(p2jb. client, port_name.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0 );
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
	jack_port_t * tmp_port = jack_port_register( p2jb.client, port_name.c_str(), JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0 );
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
	jack_port_t * tmp_port = jack_port_register( p2jb.client, port_name.c_str(), JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0 );
	if( tmp_port == nullptr )
	{
	    cerr << "Failed to register output port " << o << endl;
	    exit(1);
	}
	midi_outPorts.push_back( tmp_port );
    }
   // go!
   if (jack_activate (p2jb.client)) {
      std::cout << "Could not activate client";
      exit(1);
   }
}
// ---------------------------------- exit gracefully on shutdown ---------------------------
void exitwGrace() {
	// close pd patch
	p2jb.pj_closeAllPatches();
	
	if (p2jb.client != nullptr) {
		jack_deactivate(p2jb.client);
		// close jack client
		jack_client_close(p2jb.client);
		}
	if (input == nullptr )
		delete[] input;
	if (output == nullptr )
		delete[] output;
	
	// restore console terminal blocking
	fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) & ~O_NONBLOCK);
}
// ----------------------------- Parse colon separated numbers -----------------------
int colontokener(char *s, iP2j *duo) {
	char *tok = strtok(s, ":");
	duo->data[0] = atoi(tok);
	tok = strtok(nullptr, ":");
	duo->data[1] = atoi(tok);
return(0);
}
// ------------------------- Prompt for Interactive Mode ------------------------
void imPrompt() {
	if (p2jb.showPrompt)
		std::cout << p2jb.iPrompt;
}
// -----------------------------------------------------------------------------------------------------
void helpMsg() {
	std::cout << "Help: pd2jack <options> -p 'file.pd' <'param strs'>" << endl;
	std::cout << "-p : Pd patch Name" << endl;
	std::cout << " Optional args:" << endl;
	std::cout << "-a : Audio ports (0-16) <i:i> - default: 2:2 " << endl;
	std::cout << "-d : Descriptor <ID str>" << endl;
	std::cout << "-h : Help msg" << endl;
	std::cout << "-i : Interactive Mode" << endl;
	std::cout << "-m : Midi ports (0-16) <i:i> - default: 1:1" << endl;
	std::cout << "-n : JACK port Name" << endl;
	std::cout << "-o : OSC I/O Mode" << endl;
	std::cout << "-P : OSC port #s <in:out> - default: 20331:20341" << endl;
	std::cout << "-I : OSC In URL <addr> - default: auto" << endl;
	std::cout << "-O : OSC Out URL <addr> - default: auto" << endl;
	std::cout << "-s : Silence Pd [print] objects" << endl;
	std::cout << "-v : verbosity (0-1) - default: 0" << endl;
}

// ------------  Define the function to be called when ctrl-c (SIGINT) is sent to process
void signal_callback_handler(int signum) {
	cout << endl;
   exitwGrace();
   // Terminate program   
   exit(signum);
}
// -------------------------------------------------------------------------------------------------------------------
int main (int argc, char *argv[]) {
	int opt;
	int paramCnt = 0;
	int legal = 1;
	int getcnt = 1;
	int sampleRate = 0;
	char buf[256] ={0};
	string inString;
	jack_nframes_t bufsize;
	char client_name[] = "pd2jack";
	char *jName = nullptr;
	char patchName[] = ".pd";
	char *pName = nullptr;
	char *interStr;
	iP2j Pcount;
   
	jName = client_name;
	pName = patchName;
	
	// Register signal and signal handler -- grab the ctrl-c   
	signal(SIGINT, signal_callback_handler);
   
	if ( !(argc > 1 ))  { 	 
			helpMsg();
		}
		else {
		
		  while ((opt = getopt(argc, argv, "a:d:hiI:oO:m:n:p:P:sv:")) != -1) 
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
				
				case 'P':
				colontokener(optarg, &Pcount);
				if (Pcount.data[0] > 0 || Pcount.data[0] < 65535)
					p2jb.OSCport_In = to_string(Pcount.data[0]);
				if (Pcount.data[1] > 0 || Pcount.data[1] < 65535)
					p2jb.OSCport_Out = to_string(Pcount.data[1]);
				getcnt = optind;
				break;
				
				case 'I':
				p2jb.OSC_serverAddr.assign(optarg);
				getcnt = optind;
				break;
				
				case 'O':
				p2jb.OSC_sendAddr.assign(optarg);
				getcnt = optind;
				break;

				case 'v':
				p2jb.verboseLevel = atoi(optarg);
				if (p2jb.verboseLevel < 0 || p2jb.verboseLevel > 2)
					p2jb.verboseLevel = 0;
				getcnt = optind;
				break;

				case 's':
				p2jb.silencePrint = true; 
				getcnt = optind;
				break;
				
				case 'h':
				legal = 0;
				break;
				
				case 'i':
				p2jb.interactive = true; 
				p2jb.sleepyTime = 125;
				getcnt = optind;
				break;
				
				case 'o':
				p2jb.OSCmode= true; 
				p2jb.sleepyTime = 125;
				getcnt = optind;
				break;
				
				case 'd':
				p2jb.serverID = atoi(optarg);;
				getcnt = optind;
				break;
								
			  	case '?':
			  	legal = 0;
				break;
			 }
		  }

		sampleRate = initJack(jName);
		initPd(sampleRate);
		
		if (legal) {
			
			// set current working path
			p2jb.pj_getCurrentPath();
			// load the Pd patch
	   		// Checks to see if file opening succeeded
	   		if (p2jb.pj_openPatch(pName) >= 0) {
				// init the port buffers
				bufsize = jack_get_buffer_size(p2jb.client);
				
				if (AudioIn_TotalPorts)
					input = new float[bufsize*AudioIn_TotalPorts*sizeof(float)];
				if (AudioOut_TotalPorts)
					output = new float [bufsize*AudioOut_TotalPorts*sizeof(float)];
					
				initJPorts();		// initialize the Audio ports			
				if (getcnt < argc ) {
					for (int i = getcnt; i < argc; i++) {
						if (paramtokener(argv[i], &p2jb))
							paramCnt++;
					}
				}
				
				// build the hash table of keywords
				buildHashTable();
				
				// Non-blocking term input setup:
				// Read from stdin direct -- not ideal but prevents
				// segfaults from @quit cmd within PdReceiver callback
				fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
				
				// open an OSC port on localhost
				if (p2jb.OSCmode)
					setupOSC(&p2jb);
			
				if (p2jb.verboseLevel){
					std::cout << "Version: " << versionString << endl;
					std::cout << " -Audio: " << AudioIn_TotalPorts << ":" << AudioOut_TotalPorts;
					std::cout << " -Midi: " << MidiIn_TotalPorts << ":" << MidiOut_TotalPorts;
					std::cout << " -ID: " << p2jb.serverID;
					
					if (paramCnt) 
						std::cout << " -Params: " << paramCnt;
					std::cout << endl;
					
					if(p2jb.OSCmode)
						std::cout << " -OSC server URL: " << p2jb.OSC_serv_url  << " Out: " << p2jb.OSC_send_url << endl;
				}
				if (p2jb.interactive) {
					imPrompt();
				}
				// --------------------------------------------------------------------------
				// 		Loop until it's killed with Ctrl+C, or IMode @quit
				while(p2jb.running){
					// interactive mode, separate w/newline
					if (p2jb.interactive) {
						// non-blocking line read
						int numRead = read(STDIN_FILENO, buf, 250);
						if (numRead > 0) {
							buf[numRead] = 0; // add a null byte
							inString.assign(buf);
							do_I_Mode (inString, &p2jb);
						}
						// prompt
						imPrompt();
					}
					usleep(p2jb.sleepyTime);
				}
			}
			else
				legal = false;
		}
		else
			legal = false;
	}
		
	if (!legal)
		helpMsg();
	
	exitwGrace();
	return 0;
}
