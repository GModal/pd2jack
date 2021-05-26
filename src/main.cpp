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

#include "pd2jack.h"

using namespace std;

int sampleRate = 0;
int ticks = 0;
int Silence_Print = false;
unsigned int i;
int bend;
iP2j APcount, MPcount;

midiMsgPJ mMsg;
queue<midiMsgPJ> msgQ;

jack_client_t *client = NULL;

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
float *input = NULL;
float *output = NULL;

const int MAX_MIDI_PORTS = 16;
void * midi_buffer;

int BufferSet = 0;
void * midiOut_portBuffer;
unsigned char * eventBuffer;

uint midiChannel;
uint pdMidiChannel;

// ========================
// = JACK AUDIO CALLBACKS =
// ========================

int process(jack_nframes_t nframes, void *arg){
	// audio
    jack_default_audio_sample_t *sampleIn, *sampleOut;
    // midi
	jack_nframes_t event_count = 0;
	jack_nframes_t event_index = 0;
	jack_midi_event_t in_event;
	unsigned int oPort, jChan;

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
    
    // ----------------------------------------------------------------------------------------
    // -------------------------- Midi --------------------------------------------------------
  
    for( jack_nframes_t port = 0 ; port < MidiIn_TotalPorts ; ++port )
    {
	midi_buffer = jack_port_get_buffer( midi_inPorts[port], nframes );		   
	event_count = jack_midi_get_event_count( midi_buffer );
	
	  if(event_count > 0)
	  {
		for(int i=0; i<event_count; i++)
		{
			jack_midi_event_get(&in_event, midi_buffer, i);
			
			midiChannel = (uint)(in_event.buffer[0] & 0x0F);	// the original chan #
			pdMidiChannel = midiChannel + (16 * port);			// Pd chan & port
			 
			switch ((long)in_event.buffer[0] & MIDI_STATUS) {

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
				bend = ((in_event.buffer[2] << 7) + in_event.buffer[1]) - 8192;
				lpd.sendPitchBend(pdMidiChannel, bend);
				break;
				
			}
		}
	  }
	} 
	
	// ------------------------------------------------------------------------------------------------------------------
	// ------------------------------------------------- MIDI output 
	
	// MUST clear the last midi output buffer
	if (BufferSet) {
		jack_midi_clear_buffer(midiOut_portBuffer);
		BufferSet = 0;
	}
	
	int qc = msgQ.size();
	for (int j = 0; j < qc; j++)
	{
		mMsg = msgQ.front();
		msgQ.pop();
		oPort = mMsg.data[0] / 16;
		jChan = mMsg.data[0] - oPort * 16;
		// cout << "Event port: " << oPort << " Midi Chan: " << jChan << endl;			
		if (oPort < MidiOut_TotalPorts) {
			midiOut_portBuffer = jack_port_get_buffer( midi_outPorts[oPort], nframes );	

 			eventBuffer = jack_midi_event_reserve( midiOut_portBuffer, 0, mMsg.size);
			eventBuffer[0] = mMsg.type + jChan;
			eventBuffer[1] = mMsg.data[1];
			if (mMsg.size > 2)
				eventBuffer[2] = mMsg.data[2];
				
			BufferSet = 1;
		}
	}
   return 0;
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
//		Set the message size NOW in the callback, rather than work backwards from msg type
//
// ** MIDI data sent FROM the patch. **

void PdObject::receiveNoteOn(const int channel, const int pitch, const int velocity) {
	midiMsgPJ iMsg;

	iMsg.type = NOTE_ON;
	iMsg.size = 3;
	iMsg.data[0] = (unsigned char)channel;
	iMsg.data[1] = (unsigned char)pitch;
	iMsg.data[2] = (unsigned char)velocity;
	msgQ.push(iMsg);
}

void PdObject::receiveControlChange(const int channel, const int controller, const int value) {
	midiMsgPJ iMsg;

	iMsg.type = CONT_CTRL;
	iMsg.size = 3;
	iMsg.data[0] = (unsigned char)channel;
	iMsg.data[1] = (unsigned char)controller;
	iMsg.data[2] = (unsigned char)value;
	msgQ.push(iMsg);
}

void PdObject::receiveProgramChange(const int channel, const int value) {
	midiMsgPJ iMsg;

	iMsg.type = PROGRAM_CHANGE;
	iMsg.size = 2;
	iMsg.data[0] = (unsigned char)channel;
	iMsg.data[1] = (unsigned char)value;
	msgQ.push(iMsg);
}

void PdObject::receivePitchBend(const int channel, const int value) {
	midiMsgPJ iMsg;
	unsigned int pbValue = value + 8192;

	iMsg.type = PITCH_BEND;
	iMsg.size = 3;
	iMsg.data[0] = (unsigned char)channel;
	iMsg.data[1] = (unsigned char)(pbValue & 0x007F);
	iMsg.data[2] = (unsigned char)((pbValue) >> 7) & 0x007F;
	msgQ.push(iMsg);
}

void PdObject::receiveAftertouch(const int channel, const int value) {
	midiMsgPJ iMsg;

	iMsg.type = CHANNEL_AFTERTOUCH;
	iMsg.size = 2;
	iMsg.data[0] = (unsigned char)channel;
	iMsg.data[1] = (unsigned char)value;
	msgQ.push(iMsg);
}

void PdObject::receivePolyAftertouch(const int channel, const int pitch, const int value) {
	midiMsgPJ iMsg;

	iMsg.type = POLY_AFTERTOUCH;
	iMsg.size = 3;
	iMsg.data[0] = (unsigned char)channel;
	iMsg.data[1] = (unsigned char)pitch;
	iMsg.data[2] = (unsigned char)value;
	msgQ.push(iMsg);
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
	
	const char *server_name = NULL;
	jack_options_t options = JackNullOption;
	jack_status_t status;
   
   // create client
   client = jack_client_open(jname, options, NULL);
   
	if (client == NULL) {
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

	if (client != NULL) {
		jack_deactivate(client);
		// close jack client
		jack_client_close(client);
		}
}

// ----------------------------- Parse colon separated numbers -----------------------
int colontokener(char *s, iP2j *duo) {

	char *tok = strtok(s, ":");
	duo->A = atoi(tok);
	tok = strtok(NULL, ":");
	duo->B = atoi(tok);
return(0);
}

// ------------------------------- Parse Parameters ----------------------------------
int paramtokener(char *s) {
int cnt = 0;
float parameter;

char *tok = strtok(s, " ");
parameter = strtod(tok, NULL);

lpd.startMessage();
lpd.addFloat(parameter);

	while (tok) {
		tok = strtok(NULL, " ");
		if (tok) { 
			cnt++;
		    parameter = strtod(tok, NULL);
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
	std::cout << "-m : Midi ports (0-16) - default: 0:0" << endl;
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
	char *jName = NULL;
	char patchName[] = ".pd";
	char *pName = NULL;
   
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
					input = (float*)malloc(bufsize*AudioIn_TotalPorts*sizeof(float));
				if (AudioOut_TotalPorts)
					output  = (float*)malloc(bufsize*AudioOut_TotalPorts*sizeof(float));
				
				// 		initialize the Audio ports
				initJPorts();
				
				if (getcnt < argc ) {
					for (int i = getcnt; i < argc; i++) {
						if (paramtokener(argv[i]))
							paramCnt++;
					}
				}

				if (verbose){
					std::cout << "-Audio: " << AudioIn_TotalPorts << ":" << AudioOut_TotalPorts << " Midi: " << MidiIn_TotalPorts << ":" << MidiOut_TotalPorts;
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
