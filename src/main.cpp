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
#include <errno.h>
#include <string.h>
#include <vector>
#include <queue>
#include <memory>
#include <iostream> 
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

int sampleRate = 0;
int ticks = 0;
int Silence_Print = false;
iP2j APcount, MPcount;

vector<midiMsgP2J> msgOutV;

vector<midiSysexP2J> sysexPorts;
vector<midiSysexP2J> sysexOutV;

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
	unsigned int oPort, realMidiChannel;

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
	midi_inputBuffer = jack_port_get_buffer( midi_inPorts[port], nframes );	
	event_count = jack_midi_get_event_count( midi_inputBuffer );
			
		for(int i=0; i<event_count; i++) {
			jack_midi_event_get(&in_event, midi_inputBuffer, i);
			midiChannel = (int)(in_event.buffer[0] & 0x0F);		// the original chan #
			pdMidiChannel = midiChannel + (16 * port);			// Pd chan & port	
			
			// a SYSEX event
			if (in_event.buffer[0] == SYSEX) {
				
				for (int evCount = 0; evCount < in_event.size; evCount++) {
					unsigned char sysex_byte = in_event.buffer[evCount];
					lpd.sendSysex(port, sysex_byte);
					}
			} 
			
			else if (int genEvent = in_event.buffer[0] & MIDI_STATUS_MASK) {
				// general event -- note on controllers, etc.
				switch (genEvent) {
			
					case NOTE_OFF :
					lpd.sendNoteOn(pdMidiChannel, (long)in_event.buffer[1], 0);
					break;
						
					case NOTE_ON :
					lpd.sendNoteOn(pdMidiChannel, (long)in_event.buffer[1], (long)in_event.buffer[2]);
					break;
					
					case POLY_AFTERTOUCH :
					lpd.sendPolyAftertouch(pdMidiChannel, (long)in_event.buffer[1], (long)in_event.buffer[2]);
					break;
						
					case CONT_CTRL :
					lpd.sendControlChange(pdMidiChannel, (long)in_event.buffer[1], (long)in_event.buffer[2]);
					break;
						
					case PROGRAM_CHANGE :
					lpd.sendProgramChange(pdMidiChannel, (long)in_event.buffer[1]);
					break;
					
					case CHANNEL_AFTERTOUCH :
					lpd.sendAftertouch(pdMidiChannel, (long)in_event.buffer[1]);
					break;
						
					case PITCH_BEND :
					// Do the pitchbend math
					{
						int bend = ((in_event.buffer[2] << 7) + in_event.buffer[1]) - 8192;
						lpd.sendPitchBend(pdMidiChannel, bend);
					}
					break;	
					
					case SONG_SELECT :
					// RT two bytes
					lpd.sendSysRealTime(port, (long)in_event.buffer[0]);
					lpd.sendSysRealTime(port, (long)in_event.buffer[1]);
					break;
					
					case SONG_POSITION :
					// RT three bytes				
					for (int h = 0; h < 3; h++)
						lpd.sendSysRealTime(port, (long)in_event.buffer[h]);
					break;
					
					// The rest...
					// RT one byte
					case TUNE_REQUEST :
					case TIMING_TICK :
					case START_SONG :
					case CONTINUE_SONG :
					case STOP_SONG :
					case ACTIVE_SENSING :
					case SYSTEM_RESET :
						
					lpd.sendSysRealTime(port, (long)in_event.buffer[0]);
					break;	
				}
			}
		}
	} 
	// ------------------------------------------------------------------------------------------------------------------
	// --------------------JACK "general" Midi Output (notes, bend, controllers, etc) --------------------
	//        - - - - - - - Output previously QUEUED midi data, from Pd to JACK output ports - - - - - - 

	// MUST clear the last midi output buffer, delete that event
	for (int j = msgOutV.size() - 1; j >= 0; j--) {	
		if (msgOutV[j].bufferPtr != nullptr) {			
			jack_midi_clear_buffer(msgOutV[j].bufferPtr);
			msgOutV.erase(msgOutV.begin() + j);
		}
	}
	int vsz = msgOutV.size();
	for (int vIndex = 0; vIndex < vsz; vIndex++) {	
		oPort = msgOutV[vIndex].port;
		realMidiChannel = msgOutV[vIndex].data[0] - oPort * 16;
		
		if (oPort < MidiOut_TotalPorts) {
			msgOutV[vIndex].bufferPtr = jack_port_get_buffer( midi_outPorts[oPort], nframes );	
 			unsigned char * eventBuffer = jack_midi_event_reserve( msgOutV[vIndex].bufferPtr, 0, msgOutV[vIndex].size);
 			
 			// NOT a realtime event (a note, CC, etc)
 			if (msgOutV[vIndex].realtime == REALTIME_FALSE) {
				eventBuffer[0] = msgOutV[vIndex].type + realMidiChannel;
				eventBuffer[1] = msgOutV[vIndex].data[1];
				if (msgOutV[vIndex].size > 2)
					eventBuffer[2] = msgOutV[vIndex].data[2];
			} 
			// IS a realtime event
			else {
				//cout << "RT Pt:"  << oPort << endl;
				eventBuffer[0] = msgOutV[vIndex].type;
				if (msgOutV[vIndex].realtime == REALTIME_2)
					eventBuffer[1] = msgOutV[vIndex].data[1];
			}	
		}
	}
	//------------------------------------------------------------------------------------------------ SYSEX -------------------
	// MUST clear the last SYSEX output buffer, delete that event
	
	for (int j = sysexOutV.size() - 1; j >= 0; j--) {
		
		if (sysexOutV[ j ].bufferPtr != nullptr) {		
			jack_midi_clear_buffer(sysexOutV[ j ].bufferPtr);
				if (sysexOutV[j].status == SYSEX_KILL) {
					sysexOutV.erase(sysexOutV.begin() + j);
			} 
		}
	}

	for (int vIndex = 0; vIndex < sysexOutV.size(); vIndex++) {
		oPort = sysexOutV[vIndex].port;
		
		if (oPort < MidiOut_TotalPorts && oPort >= 0) {
			if (sysexOutV[vIndex].status == SYSEX_DONE) {
				sysexOutV[vIndex].bufferPtr =  (unsigned char *)jack_port_get_buffer( midi_outPorts[oPort], nframes );	
				unsigned char * sysexBuffer = jack_midi_event_reserve( sysexOutV[vIndex].bufferPtr, 0, sysexOutV[vIndex].dataV.size() );
			
				for (int h = 0; h < sysexOutV[vIndex].dataV.size(); h++)
				{
					sysexBuffer[h] = sysexOutV[vIndex].dataV[h];
				}
				sysexOutV[vIndex].status = SYSEX_KILL;
			}
		}
	}
return 0;
}

// --------------------------------------------------------------------------------------
// --------- find if obj w/ current port exists in vector
//	- - -  return port index if true, -1 if false

int findObjByPort( int port) {
int portIndex = SYSEX_NO_PORT_INDEX;
	
	for(int i = 0; i < sysexPorts.size(); i++)
	{
		if (sysexPorts[i].port == port)
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
// -----------------------------------------------------------------------------------------
// LibPd MIDI callbacks
//
//  - - - - - - - -  MIDI data sent FROM the patch -> prepare this TO BE OUTPUT via JACK - - - - - - - - -
//	Note, etc., data could be output directly, but longer msg are single byte, so must be queued

void PdObject::receiveNoteOn(const int channel, const int pitch, const int velocity) {
	midiMsgP2J iMsg;

	iMsg.type = NOTE_ON;
	iMsg.port = (int) channel / 16;
	iMsg.size = 3;
	iMsg.realtime = REALTIME_FALSE;
	iMsg.data[0] = (unsigned char)channel;
	iMsg.data[1] = (unsigned char)pitch;
	iMsg.data[2] = (unsigned char)velocity;
	msgOutV.push_back(iMsg);
}

void PdObject::receiveControlChange(const int channel, const int controller, const int value) {
	midiMsgP2J iMsg;

	iMsg.type = CONT_CTRL;
	iMsg.port = (int) channel / 16;
	iMsg.size = 3;
	iMsg.realtime = REALTIME_FALSE;
	iMsg.data[0] = (unsigned char)channel;
	iMsg.data[1] = (unsigned char)controller;
	iMsg.data[2] = (unsigned char)value;
	msgOutV.push_back(iMsg);
}

void PdObject::receiveProgramChange(const int channel, const int value) {
	midiMsgP2J iMsg;

	iMsg.type = PROGRAM_CHANGE;
	iMsg.port = (int) channel / 16;
	iMsg.size = 2;
	iMsg.realtime = REALTIME_FALSE;
	iMsg.data[0] = (unsigned char)channel;
	iMsg.data[1] = (unsigned char)value;
	msgOutV.push_back(iMsg);
}

void PdObject::receivePitchBend(const int channel, const int value) {
	midiMsgP2J iMsg;
	unsigned int pbValue = value + 8192;

	iMsg.type = PITCH_BEND;
	iMsg.port = (int) channel / 16;
	iMsg.size = 3;
	iMsg.realtime = REALTIME_FALSE;
	iMsg.data[0] = (unsigned char)channel;
	iMsg.data[1] = (unsigned char)(pbValue & 0x007F);
	iMsg.data[2] = (unsigned char)((pbValue) >> 7) & 0x007F;
	msgOutV.push_back(iMsg);
}

void PdObject::receiveAftertouch(const int channel, const int value) {
	midiMsgP2J iMsg;

	iMsg.type = CHANNEL_AFTERTOUCH;
	iMsg.port = (int) channel / 16;
	iMsg.size = 2;
	iMsg.realtime = REALTIME_FALSE;
	iMsg.data[0] = (unsigned char)channel;
	iMsg.data[1] = (unsigned char)value;
	msgOutV.push_back(iMsg);
}

void PdObject::receivePolyAftertouch(const int channel, const int pitch, const int value) {
	midiMsgP2J iMsg;

	iMsg.type = POLY_AFTERTOUCH;
	iMsg.port = (int) channel / 16;
	iMsg.size = 3;
	iMsg.realtime = REALTIME_FALSE;
	iMsg.data[0] = (unsigned char)channel;
	iMsg.data[1] = (unsigned char)pitch;
	iMsg.data[2] = (unsigned char)value;
	msgOutV.push_back(iMsg);
}

void PdObject::receiveMidiByte(const int port, const int byte) {
int portIndex;
midiMsgP2J iMsg;
midiSysexP2J mSysex;
unsigned char ucByte;
bool byte_OK = true;

	ucByte = (unsigned char)(byte);
	portIndex = findObjByPort(port);

		if (portIndex != SYSEX_NO_PORT_INDEX) {
			if (sysexPorts[portIndex].status == SYSEX_IN_PROGRESS ) {
				if (ucByte >= 128 && ucByte != SYSEX_END) {
					byte_OK = false;
				}
			}
		}

		if (byte_OK) {
		
			switch (ucByte) {
		
				case TUNE_REQUEST :
				case TIMING_TICK :
				case START_SONG :
				case CONTINUE_SONG :
				case STOP_SONG :
				case ACTIVE_SENSING :
				case SYSTEM_RESET :
				
				iMsg.type = ucByte;
				iMsg.size = REALTIME_1;
				iMsg.port = port;
				//iMsg.realtime = REALTIME_1;
				iMsg.data[0] = ucByte;
				msgOutV.push_back(iMsg);
				break;
				
				case SONG_SELECT :
				// two bytes
				break;
				
				case SONG_POSITION :
				// three bytes				
				break;
		
				// * * * * * * * * * * * * * * NEW sysex msg
				case SYSEX :
				
				// should NOT be a existing sysex msg on this port, kill the old one
				if (portIndex != SYSEX_NO_PORT_INDEX) {
					if (sysexPorts[portIndex].status == SYSEX_IN_PROGRESS) {
						sysexPorts.erase(sysexPorts.begin() + portIndex);
					}
				}
				mSysex.dataV.clear();
				mSysex.status = SYSEX_IN_PROGRESS;
				mSysex.port = port;
		
				//mSysex.sysexSize = 1;
				mSysex.dataV.push_back(ucByte);
				mSysex.bufferPtr = nullptr;
				// add to vector
				sysexPorts.push_back(mSysex);
	
				break;
				
				//  * * * * * * * * * * * * * * END of sysex msg
				
				case SYSEX_END :
				if (portIndex != SYSEX_NO_PORT_INDEX) {
					if (sysexPorts[portIndex].status == SYSEX_IN_PROGRESS) {
						//sysexPorts[portIndex].sysexSize++;
						sysexPorts[portIndex].dataV.push_back(ucByte);
						sysexPorts[portIndex].status = SYSEX_DONE;
					}
				}
	
				break;
		
				// * * * * * * * * * * * * * * * CONTINUE (FILL) the sysex msg
			
				default:
				if (ucByte < 128) {
					if (portIndex != SYSEX_NO_PORT_INDEX) {
						if (sysexPorts[portIndex].status == SYSEX_IN_PROGRESS ) {
								//sysexPorts[portIndex].sysexSize++;
								sysexPorts[portIndex].dataV.push_back(ucByte);
						}
					}
				}
				break;
			}
		}
		// --------------------------------------------------------------------------------------------------------------------
		// * * * * * * * * * * * * *  READY to send Sysex
		// This just clears the "set" input buffers & pushes them on the output vector.
		// (the queue was originally for sysex data packets - not happening currently, but leaving as-is)
	
		//push on Q
		for (int qCount = 0; qCount < sysexPorts.size() ; qCount++) {	
			if (sysexPorts[ qCount].status == SYSEX_DONE) {
				sysexOutV.push_back(sysexPorts[ qCount ]);
				sysexPorts[ qCount].status = SYSEX_PREKILL;
			}
		}
		
		// delete
		for (int qCount = sysexPorts.size() - 1; qCount >= 0; qCount--) {
			if ( sysexPorts[ qCount ].status == SYSEX_PREKILL) {
				sysexPorts.erase(sysexPorts.begin() + qCount);
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
	duo->A = atoi(tok);
	tok = strtok(nullptr, ":");
	duo->B = atoi(tok);
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
				colontokener(optarg, &MPcount);
				if (MPcount.A < 0 || MPcount.A > MAX_MIDI_PORTS)
					MidiIn_TotalPorts = 2;
					else 
						MidiIn_TotalPorts = MPcount.A;
				if (MPcount.B < 0 || MPcount.B > MAX_MIDI_PORTS)
					MidiIn_TotalPorts = 2;
					else 
						MidiOut_TotalPorts = MPcount.B;	
				getcnt = optind;
				break;
				
			  	case 'a':
				colontokener(optarg, &APcount);
				if (APcount.A < 0 || APcount.A > MAX_AUDIO_PORTS)
					AudioIn_TotalPorts = 2;
					else 
						AudioIn_TotalPorts = APcount.A;
				if (APcount.B < 0 || APcount.B > MAX_AUDIO_PORTS)
					AudioIn_TotalPorts = 2;
					else 
						AudioOut_TotalPorts = APcount.B;	
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
					sleep(-1);
				}		  
			}
		}
		else 
		helpMsg();
	}
	exitwGrace();
	return 0;
}
