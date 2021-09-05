// pd2jack project
// interactive.hpp
// -- Doug Garmon
#ifndef INTERACTIVE_H
#define INTERACTIVE_H 

#include <PdBase.hpp>
#include <PdMidiReceiver.hpp>
#include "PdObject.hpp"
#include "pd2jack.hpp"

using namespace std;
using namespace pd;

// protos
void exitwGrace(p2jBase *);

int paramtokener(char *s, p2jBase *);
//int specTokens(char *s, p2jBase *);
void do_I_Mode(string&, p2jBase *);

// protos for I Mode command function, used as *-to-f
void fn_openPatch(char *, p2jBase *) ;
void fn_closePatch(char *, p2jBase *) ;
void fn_closeLastPatch(char *, p2jBase *) ;
void fn_quit(char *, p2jBase *) ;
void fn_dsp(char *, p2jBase *) ;
void fn_dspOff(char *, p2jBase *) ;
void fn_bypass(char *, p2jBase *);
void fn_bypassOff(char *, p2jBase *) ;
void fn_sendBang(char *, p2jBase *) ;
void fn_sendFloat(char *, p2jBase *) ;
void fn_sendSymbol(char *, p2jBase *) ;
void fn_sendNote(char *, p2jBase *) ;
void fn_sendCC(char *, p2jBase *) ;
void fn_sendProgC(char *, p2jBase *) ;
void fn_sendPBend(char *, p2jBase *) ;
void fn_sendAfterT(char *, p2jBase *) ;
void fn_sendPolyAT(char *, p2jBase *) ;
void fn_sendMidiB(char *, p2jBase *) ;
void fn_sendSysex(char *, p2jBase *) ;
void fn_sendSysRT(char *, p2jBase *) ;
void fn_startMsg(char *, p2jBase *) ;
void fn_addFloat(char *, p2jBase *) ;
void fn_addSymbol(char *, p2jBase *) ;
void fn_endMsg(char *, p2jBase *) ;
void fn_endList(char *, p2jBase *) ;
void fn_setPPair(char *, p2jBase *) ;
void fn_setIMode(char *, p2jBase *) ;
void fn_setOSCMode(char *, p2jBase *) ;
void fn_sleepTime(char *, p2jBase *) ;
void fn_setID(char *, p2jBase *) ;
void fn_prompt(char *, p2jBase *) ;
void fn_setPrompt(char *, p2jBase *) ;
void fn_version(char *, p2jBase *) ;
void fn_error(char *, p2jBase *) ;

// Interactive Mode error codes
enum IM_errorCodes {
	STATUS_NOOP = -1,
	STATUS_ERROR = 0,
	STATUS_SUCCESS,
	STATUS_RESET_DEFAULT,
	STATUS_INVALID_CMD
};
// ------------------------------------------------------------------------
enum pdMsgType {
	PD_NULL_MSG = 0,
	PD_START_MSG,
	PD_FLOAT_MSG,
	PD_SYMBOL_MSG,
	PD_FINISH_MSG,
	PD_FINISH_LIST_MSG
};
// class for LibPd compound messages, for Iteractive Mode
class pdMessage {
public:
	int type;
	float floatValue;
	string strValue;	// symbol
	string name;		// name for finishMessage
	
	pdMessage(void) {
	type = PD_NULL_MSG;	
	}
};

#endif
