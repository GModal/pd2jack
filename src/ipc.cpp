// "ipc.cpp"
// for
//	pd2jack
//		Doug Garmon
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iomanip>
#include <iostream> 
#include <sstream>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <ifaddrs.h>
#include <PdTypes.hpp>
#include "pd2jack.hpp"
#include "interactive.hpp"
#include "ipc.hpp"

#include <lo/lo.h>

using namespace pd;

// store the p2jBase for OSC callbacks
p2jBase *localBase;
// error for lo
int loerror;
string sPath;
string outStr;

void error(int num, const char *m, const char *path);

int general_handler(const char *path, const char *types, lo_arg ** argv,
                int argc, lo_message data, void *user_data);

//--------------------------------------------------------------------------------------------
// 'type' not needed to build msgs, but used for other options,
//	like converting symbols to string
void sendOSC(p2jBase *base, int type, string msg, const List& list) {
float oscData[8];
string oscAddr;
stringstream buildStream;
lo_message loMes = lo_message_new();

	if(base->oscOut) {
		// build addr + ID
		buildStream << base->serverRootName << base->serverID  << "/" << msg;
		oscAddr = buildStream.str();

		switch (type) {
			
		case OSC_MSG:
		case OSC_MSG_STR:
		default:
		// add tags + data to msg
		for (int i = 0; i <list.len(); i++)  {
			if(list.isFloat(i)) {
				lo_message_add_float(loMes, list.getFloat(i));
				}
			if(list.isSymbol(i)) {
				// convert symbol to string
				if (type == OSC_MSG_STR)
					lo_message_add_string(loMes, list.getSymbol(i).c_str());
				else
					lo_message_add_symbol(loMes, list.getSymbol(i).c_str());
				}
			}
		lo_send_message(base->oscOut, oscAddr.c_str(), loMes);
		break;		
		}
	}
}
//----------------------------------------------------------------------------------------------------------------
void processOSCpp(float p1, float p2) {
std::stringstream outStream;

	// format as a parameter pair, send to I Mode
	outStream << p1 << " " << p2 << std::endl;
	outStr = outStream.str();
	do_I_Mode (outStr, localBase);
}
//----------------------------------------------------------------------------------------------------------------
void buildHstr(string& istr, const char *types, lo_arg ** argv, int argc) {
std::stringstream outStream;

	for (int i = 0; i < argc; i++) {
	// p2j should only be handling floats or symbols. Try to convert similar types...
		switch (types[i]) {
		case 'f':
		outStream << " " << std::to_string(argv[i]->f);
		break;
		case 'i':
		outStream << " " << std::to_string(argv[i]->i);
		break;
		case 's':
		outStream << " "  << &argv[i]->s;
		break;
		case 'S':
		outStream << " "  << &argv[i]->S;
		break;
		}
	}
	outStream << endl;
	istr = outStream.str();
	istr.erase(0, istr.find_first_not_of(" \n\t"));
	//std::cout << imstr;
}
// -------------------------------------------osc code -- uses lo lib  --------------------------------------------------------
// The server method handlers
void error(int num, const char *msg, const char *path)
{
    printf("liblo server error %d in path %s: %s\n", num, path, msg);
    fflush(stdout);
    loerror = 1;
}

// Primary incoming OSC handler INTO pd2jack FROM outside clients
// this would be WAY easier if Ubuntu/Debian/etc had liblo > .30, and pattern matching...
int general_handler(const char *path, const char *types, lo_arg ** argv, int argc, lo_message data, void *user_data) {
int loc, locID;
int OSC_type = -1;
int cliRootEnd = -1;
int incomingID = -1;
string imstr;
sPath.assign(path);

cliRootEnd = localBase->clientRootName.size();

// check for client string OSC path:
// OSC format is always: /<clientRootName>/<incomingID>/<cmd>
// where:
// 		<clientRootName> 	: is the name clients send (there's a default)
//		<incomingID>		: is the integer ID# of the destination (receiver/server -- this)
//		<cmd>			: is the OSC cmd sent by client
//
// Example: /P2Jcli/0/pp
//
loc = sPath.find(localBase->clientRootName);
// zero means client root string was found
if (loc == 0) {
	locID = sPath.find_first_of("/", cliRootEnd);
	// extract the ID#
	if (locID > cliRootEnd) {
		imstr = sPath.substr(cliRootEnd, locID - cliRootEnd);
		incomingID = std::stoi(imstr, nullptr);
		// check incoming ID with our own ID
		if (incomingID == localBase->serverID) {
			
			// extract the cmd
			imstr = sPath.substr(locID + 1);
			if (!imstr.compare("pp"))				// parameter pair -> patch
				OSC_type = OSC_PPAIR;
			else if (!imstr.compare("cmd"))		//  pd2jack
				OSC_type = OSC_ICMD;
			else if (!imstr.compare("P2JoR"))		// user data -> patch
				OSC_type = OSC_DIRECT;
			}
		}
	}	
	switch(OSC_type) {
	case OSC_PPAIR:
	processOSCpp(argv[0]->f, argv[1]->f);
	break;
	case OSC_ICMD:
	buildHstr(imstr, types, argv, argc);
	do_I_Mode (imstr, localBase);
	break;
	case OSC_DIRECT:
	//std::cout << "A direct cmd" << endl;
	
	if (argc > 0) {
		localBase->lpd.startMessage();
		for (int i = 1; i < argc; i++) {
		// p2j should only be handling floats or symbols. Try to convert similar types...
			switch (types[i]) {
				case 'f':
				localBase->lpd.addFloat( argv[i]->f);
				break;
				case 'i':
				localBase->lpd.addFloat((float)argv[i]->i);
				break;
				case 's':
				localBase->lpd.addSymbol( &argv[i]->s);
				break;
				case 'S':
				localBase->lpd.addSymbol(&argv[i]->S);
				break;
				}
			}
		localBase->lpd.finishList( &argv[0]->s);
		break;
		}
	}
return 0;
}
// -------------------------------------------------------------------------------------------------------------------------------
int setupOSC(p2jBase *base) {
int retval = 0;
char *serveraddr = NULL;
char *sendaddr = NULL;
loerror = 0;

	// save the base pointer for some callbacks
	localBase = base;
	// start a new server on port
	//	NULL pointer as default, instead of IP addr string probably works the best
	if (base->OSC_serverAddr.size())
		serveraddr = (char *)base->OSC_serverAddr.c_str();
	base->st = lo_server_thread_new_multicast(serveraddr, base->OSCport_In.c_str() , error);
	
	if (loerror == 0) {
		// store the URL
		char *inURL = lo_server_thread_get_url(base->st);
		if (inURL) {
			base->OSC_serv_url.assign(inURL);
			free(inURL);
		}

		lo_server_thread_add_method(base->st, NULL, NULL, general_handler, NULL);
		lo_server_thread_start(base->st);

		// set an IP addr for outgoing OSC data
		if (base->OSC_sendAddr.size())
			sendaddr =  (char *)base->OSC_sendAddr.c_str();
		
		// Outgoing
		// 	NULL pointer OK for local, multicast for external (mobmuplat, etc) control
		base->oscOut = lo_address_new(sendaddr, base->OSCport_Out.c_str());
		// save the URL
		char *outURL = lo_address_get_url(base->oscOut);
		if (outURL) {
			base->OSC_send_url.assign(outURL);
			free(outURL);
		}			
		if (loerror != 0) 
			retval = -1;
	}
	else retval = -1;

return(retval);
}
// -------------------------------------------------------------------------------------------
int closeOSC(p2jBase *base) {
int retval = -1;
	if (base->st)
		lo_server_thread_free(base->st);
	if (base->oscOut)
		lo_message_free(base->oscOut);
	base->st = NULL;
	base->oscOut = NULL;
	
return retval;
}