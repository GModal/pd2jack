// pd2jack project
// interactive.cpp
// -- Doug Garmon

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <PdBase.hpp>
#include <PdMidiReceiver.hpp>
#include "PdObject.hpp"
#include "pd2jack.hpp"
#include <vector>
#include <string>

#include "interactive.hpp"
#include "hash.hpp"
#include "ipc.hpp"

using namespace std;

// NOOP means no IM commands have been entered
int lastError = STATUS_NOOP;
int lastCmd = STATUS_NOOP;

vector<pdMessage> pdCompoundMsg;
string recvName;
pdMessage pdMsg;

char delim[] = " \r\n\t";

// ---------------------------------------------------------------------------------------
// ------------------------- where to stream info & errors ------------------------
void veOut (string preStr, int errNum, int lastc) {
	cerr << preStr << errNum << " cmd: " << lastc << endl;	
}
// ------------------------------- Parse Parameter Pairs ----------------------------------
int paramtokener(char *s, p2jBase *base) {
int cnt = 0;
float parameter;

char *tok = strtok(s, " \t");
parameter = strtod(tok, nullptr);

base->lpd.startMessage();
base->lpd.addFloat(parameter);

	while (tok) {
		tok = strtok(nullptr, " ");
		if (tok) { 
			cnt++;
		    parameter = strtod(tok, nullptr);
		    base->lpd.addFloat(parameter);
		    }
	}
base->lpd.finishList(base->pPair);
return(cnt);
}

// ---------------------------- Send Compound Msg, I Mode ------------------------------
void sendCompoundMsg (p2jBase *base) {
	
	if (pdCompoundMsg[0].type == PD_START_MSG) {
		base->lpd.startMessage();
		
		for (int mIndex = 1; mIndex < pdCompoundMsg.size() ; mIndex++) {
	
			switch (pdCompoundMsg[mIndex].type) {
			
			case PD_FLOAT_MSG:
			base->lpd.addFloat( pdCompoundMsg[mIndex].floatValue );
			break;
			
			case PD_SYMBOL_MSG:
			base->lpd.addSymbol( pdCompoundMsg[mIndex].strValue );
			break;
			
			case PD_FINISH_MSG:
			base->lpd.finishMessage( pdCompoundMsg[mIndex].name, pdCompoundMsg[mIndex].strValue );
			break;
			
			case PD_FINISH_LIST_MSG:
			base->lpd.finishList( pdCompoundMsg[mIndex].strValue );
			break;
			}
		}
	}
}
// ------------------------- functions, Pointers to funtions array -------------------------------------

// array of pointers to I Mode functions
void (*functptr[])(char *, p2jBase *) = { 
	fn_sendSysex,			// replicated in array for short version of sendSysex
	fn_sendNote,			// replicated for short version of sendNote
	fn_sendCC,			// replicated for short version of sendCC
	fn_sendPBend,		// replicated for short version of sendPBend
	fn_openPatch,
	fn_closePatch,
	fn_closeLastPatch,
	fn_quit, 
	fn_dsp,
	fn_dspOff,
	fn_bypass,
	fn_bypassOff,
	fn_sendBang,
	fn_sendFloat,
	fn_sendSymbol,
	fn_sendNote,
	fn_sendCC,
	fn_sendProgC,
	fn_sendPBend,
	fn_sendAfterT,
	fn_sendPolyAT,
	fn_sendMidiB,
	fn_sendSysex,
	fn_sendSysRT,
	fn_startMsg,
	fn_addFloat,
	fn_addSymbol,
	fn_endMsg,
	fn_endList,
	fn_setPPair,
	fn_setIMode,
	fn_setOSCMode,
	fn_sleepTime,
	fn_setID,
	fn_prompt,
	fn_setPrompt,
	fn_version,
	fn_error
} ;

// functions for I Mode

// OPEN_PATCH_CMD
void fn_openPatch (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, delim);
	if (tok) {
		lastError = base->pj_openPatch(tok) ;
		if (lastError >= 0)
			lastError = STATUS_SUCCESS;
	}
}
// CLOSE_PATCH_CMD
void fn_closePatch (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, delim);
	if (tok) {
		int index = atoi(tok);
		base->pj_closePatch(index);
		lastError = STATUS_SUCCESS;
	}
	else {
		lastError = base->pj_closeAllPatches();
		lastError = STATUS_SUCCESS;
	}
}
// CLOSE_LAST_PATCH_CMD
void fn_closeLastPatch (char * tok, p2jBase *base) {
	lastError = base->pj_closeLastPatch();	
	lastError = STATUS_SUCCESS;
}
// DSP_OFF_CMD
void fn_dspOff (char * tok, p2jBase *base) {
	base->lpd.computeAudio(false);
	lastError = STATUS_SUCCESS;
}
// DSP_CMD
void fn_dsp (char * tok, p2jBase *base) {
	lastError = STATUS_SUCCESS;
	tok = strtok(nullptr, delim);
	if (tok) {
		int dspv = atoi(tok);
		if (dspv == 0 || dspv == 1)
			base->lpd.computeAudio(dspv);
	}
	else
	base->lpd.computeAudio(true);
}
// BYPASS_CMD
void fn_bypass (char * tok, p2jBase *base) {
	lastError = STATUS_SUCCESS;
	tok = strtok(nullptr, delim);
	if (tok) {
		int bpass = strtod(tok, NULL);
		if (bpass == 0.0)
			base->bypass = 0;
		else
			base->bypass = 1;
	}
}
// BYPASS_OFF_CMD
void fn_bypassOff (char * tok, p2jBase *base) {
	base->bypass = 0;
	lastError = STATUS_SUCCESS;
}
// QUIT_CMD
void fn_quit (char * tok, p2jBase *base) {
	lastError = STATUS_SUCCESS;
	base-> running = false;
	std::cout << "Exiting..." << endl;
}
// SEND_FLOAT_CMD
void fn_sendFloat (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, " \t");
	if (tok) {
		recvName = tok;
		tok = strtok(nullptr, delim);
		if (tok) {
			float sf = strtod(tok, nullptr);
			base->lpd.sendFloat((char *)recvName.c_str(), sf);
			lastError = STATUS_SUCCESS;
		}
	}	
}
// SEND_BANG_CMD
void fn_sendBang (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, delim);
	if (tok) {
		base->lpd.sendBang(tok);
		lastError = STATUS_SUCCESS;
		}
}
// SEND_SYMBOL_CMD
void fn_sendSymbol (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, " \t");
	if (tok) {
		recvName = tok;
		tok = strtok(nullptr, delim);
		if (tok) {
			base->lpd.sendSymbol((char *)recvName.c_str(), tok);
			lastError = STATUS_SUCCESS;
		}
	}	
}
//---------------------------------------------------------------------------------------------------------------
// MIDI input special cmds
// These replicate the function of MIDI input ports, from the I Mode console 

// Send a NoteOn event
void fn_sendNote (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, " \t");
	if (tok) {
		int chan = atoi(tok);
		tok = strtok(nullptr, " \t");
			if (tok) {
			int pitch = atoi(tok);
			tok = strtok(nullptr, delim);
				if (tok) {
					int velocity = atoi(tok);
					base->lpd.sendNoteOn(chan - 1, pitch, velocity);
					lastError = STATUS_SUCCESS;
				}
		}
	}	
}
// Send a CC event
void fn_sendCC (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, " \t");
	if (tok) {
		int chan = atoi(tok);
		tok = strtok(nullptr, " \t");
			if (tok) {
			int contrl = atoi(tok);
			tok = strtok(nullptr, delim);
				if (tok) {
					int val = atoi(tok);
					base->lpd.sendControlChange(chan - 1, contrl, val);
					lastError = STATUS_SUCCESS;
				}
		}
	}	
}
// Send a Program Change event
void fn_sendProgC (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, " \t");
	if (tok) {
		int chan = atoi(tok);
		tok = strtok(nullptr, delim);
		if (tok) {
			int val = atoi(tok);
			base->lpd.sendProgramChange(chan - 1, val);
			lastError = STATUS_SUCCESS;
		}
	}
}
// Send a Pitch Bend event
void fn_sendPBend (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, " \t");
	if (tok) {
		int chan = atoi(tok);
		tok = strtok(nullptr, delim);
		if (tok) {
			int val = atoi(tok);
			base->lpd.sendPitchBend(chan - 1, val);
			lastError = STATUS_SUCCESS;
		}
	}
}
// Send an Aftertouch event
void fn_sendAfterT (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, " \t");
	if (tok) {
		int chan = atoi(tok);
		tok = strtok(nullptr, delim);
		if (tok) {
			int val = atoi(tok);
			base->lpd.sendAftertouch(chan - 1, val);
			lastError = STATUS_SUCCESS;
		}
	}
}
// Send a Poly Aftertouch event
void fn_sendPolyAT (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, " \t");
	if (tok) {
		int chan = atoi(tok);
		tok = strtok(nullptr, " \t");
			if (tok) {
			int pitch= atoi(tok);
			tok = strtok(nullptr, delim);
				if (tok) {
					int val = atoi(tok);
					base->lpd.sendPolyAftertouch(chan - 1, pitch, val);
					lastError = STATUS_SUCCESS;
				}
		}
	}	
}
// SEND a MIDI byte
void fn_sendMidiB (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, " \t");
	if (tok) {
		int port = atoi(tok);
		tok = strtok(nullptr, delim);
		if (tok) {
			int byte = atoi(tok);
			base->lpd.sendMidiByte(port, (unsigned char)byte);
			lastError = STATUS_SUCCESS;
		}
	}	
}
// Send a Sysex MIDI byte
void fn_sendSysex (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, " \t");
	if (tok) {
		int port = atoi(tok);
		tok = strtok(nullptr, delim);
		if (tok) {
			int byte = atoi(tok);
			base->lpd.sendSysex(port, (unsigned char)byte);
			lastError = STATUS_SUCCESS;
		}
	}	
}
// Send a Realtime byte
void fn_sendSysRT (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, " \t");
	if (tok) {
		int port = atoi(tok);
		tok = strtok(nullptr, delim);
		if (tok) {
			int byte = atoi(tok);
			base->lpd.sendSysRealTime(port, (unsigned char)byte);
			lastError = STATUS_SUCCESS;
		}
	}	
}
//-----------------------------------------------------------------------------------------------------------
// START_MSG_CMD
void fn_startMsg (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	if (pdCompoundMsg.size())
		pdCompoundMsg.clear();
	
	pdMsg.type = PD_START_MSG;
	pdCompoundMsg.push_back(pdMsg);
	lastError = STATUS_SUCCESS;
}
// ADD_FLOAT_CMD
void fn_addFloat (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	if (pdCompoundMsg.size()) {
		tok = strtok(nullptr, delim);
		if (tok) {
			float sf = strtod(tok, nullptr);

			pdMsg.type = PD_FLOAT_MSG;
			pdMsg.floatValue = sf;
			pdCompoundMsg.push_back(pdMsg);
			lastError = STATUS_SUCCESS;
		}
	}
}
// ADD_SYMBOL_CMD
void fn_addSymbol (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	if (pdCompoundMsg.size()) {
		tok = strtok(nullptr, delim);
		if (tok) {
			pdMsg.type = PD_SYMBOL_MSG;
			pdMsg.strValue = tok;
			pdCompoundMsg.push_back(pdMsg);
			lastError = STATUS_SUCCESS;
		}
	}
}
// END_MSG_CMD
void fn_endMsg (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	if (pdCompoundMsg.size() > 1) {
		tok = strtok(nullptr, " \t");
		if (tok) {
			pdMsg.type = PD_FINISH_MSG;
			pdMsg.name = tok;
			tok = strtok(nullptr, delim);
			if (tok) {
				pdMsg.strValue = tok;
				pdCompoundMsg.push_back(pdMsg);
				sendCompoundMsg(base);
				lastError = STATUS_SUCCESS;
			}
		}
	}
}
// END_LIST_CMD
void fn_endList (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	if (pdCompoundMsg.size() > 1) {
		tok = strtok(nullptr, delim);
		if (tok) {
			pdMsg.type = PD_FINISH_LIST_MSG;
			pdMsg.strValue = tok;
			pdCompoundMsg.push_back(pdMsg);
			sendCompoundMsg(base);
			lastError = STATUS_SUCCESS;
		}
	}
}
// SET_PPAIR_CMD
void fn_setPPair (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, delim);
	if (tok) {
		base->pPair = tok;
		lastError = STATUS_SUCCESS;
	}
	else {
		base->pPair = "param";
		lastError = STATUS_RESET_DEFAULT;
	}
}
// I MODE_CMD
void fn_setIMode (char * tok, p2jBase *base) {
	lastError = STATUS_SUCCESS;
	tok = strtok(nullptr, delim);
	if (tok) {
		int iAct = atoi(tok);
		if (iAct == 0 || iAct == 1)
			base->interactive = iAct;
	}
}
// OSC_CMD
void fn_setOSCMode (char * tok, p2jBase *base) {
	lastError = STATUS_SUCCESS;
	tok = strtok(nullptr, delim);
	if (tok) {
		int omode = atoi(tok);
		if (omode == 0 || omode == 1) {
			if (base->OSCmode != omode) {
				base->OSCmode = omode;
				if (omode)
					setupOSC(base);
				else
					closeOSC(base);
			}
		}
		
	}
}
// SLEEP_TIME_CMD
void fn_sleepTime (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, delim);
	if (tok) {
		int sTime = atoi(tok);
		if (sTime >= 5 && sTime <= 500) {
			base->sleepyTime = sTime;
			lastError = STATUS_SUCCESS;
		}
	}	
}
// SET_ID_CMD
void fn_setID (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, delim);
	if (tok) {
		base->serverID = atoi(tok);
		lastError = STATUS_SUCCESS;
	}
}
// PROMPT_CMD
void fn_prompt (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, delim);
	if (tok) {
		int prmt = atoi(tok);
		if (prmt == 0 || prmt == 1) {
			base->showPrompt = prmt;
			lastError = STATUS_SUCCESS;
		}
	}	
}
// PROMPT_SET_CMD
void fn_setPrompt (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	tok = strtok(nullptr, ";");
	if (tok) {
		base->iPrompt = tok;
		lastError = STATUS_SUCCESS;
	}
}
// VERSION_CMD
void fn_version (char * tok, p2jBase *base) {
	lastError = STATUS_ERROR;
	cerr << versionString << endl;
	lastError = STATUS_SUCCESS;
}
// ERROR_CMD
void fn_error (char * tok, p2jBase *base) {
	cerr << "E: " << lastError << endl;
}
// ----------------------------  Special Tokens, I Mode ---------------------------------------
int specTokens(char *s, p2jBase *base) {
char *tok = strtok(s, delim);
int specID = STATUS_NOOP;

	specID = searchTable(tok);
	if (specID > STATUS_NOOP) {
		(*functptr[specID])(tok, base);
		lastCmd = specID;
	}
	else 
		lastError = STATUS_INVALID_CMD;
	
	if (base->verboseLevel)
		veOut("Status: ", lastError, specID);
	
return (specID);
}
// -------------------------entry for I Mode -----------------------------------------
void do_I_Mode (string& iStr, p2jBase *base) {
	char *interStr = (char *)iStr.c_str();
	// special Interactive Mode msgs start with "@"
	if (interStr[0] == 64)	
		specTokens(interStr, base);
	else
		paramtokener(interStr, base);
}
