// hash.cpp
// "pd2jack"
// by Doug Garmon

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream> 
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>

#include "hash.hpp"
#include "interactive.hpp"

using namespace std;

// Build hash table from these strings
const char * specialT[] = {
"s",				// synonym for sendSysex
"n",				// synonym for sendNote
"c",				// synonym for sendCC
"b",				// synonym for sendPBend
"openpatch",
"closepatch",
"closelastpatch",
"quit",
"dsp",
"dspoff",
"bypass",
"bypassoff",
"sendbang",
"sendfloat",
"sendsymbol",
"sendnote",
"sendcc",
"sendprogc",
"sendpbend",
"sendaftert",
"sendpolyat",
"sendmidib",
"sendsysex",
"sendsysrt",
"startmsg",
"addfloat",
"addsymbol",
"endmsg",
"endlist",
"ppname",
"imode",
"oscmode",
"sleeptime",
"setid",
"prompt",
"setprompt",
"-v",
"-e"
};

const int SPECIALT_SIZE = sizeof(specialT)/sizeof(char*);
size_t hashTable [SPECIALT_SIZE];

// -----------------------------------------------------------------------------------------------
// "djb2" classic hash function by Dan Bernstein
size_t hashDB(unsigned char *str)
{
    unsigned long hash = 5381;
    int c;
    while (c = *str++)
        hash = ((hash << 5) + hash) + c; 
    return hash;
}

// -----------------------------------------------------------------------------------------------
void buildHashTable() {
	for(int i = 0; i < SPECIALT_SIZE; i++) {
		hashTable[i] = hashDB((unsigned char *)specialT[i]);
	}
}
//------------------------------------------------------------------------------------------------
int searchTable(char *s) {
	int retval = -1;
	// s+1 : skip the first char, '@'
	size_t hval = hashDB((unsigned char *)s+1);
	
	for(int i = 0; i < SPECIALT_SIZE; i++) {
		if (hval == hashTable[i])
			retval = i;
	}
return(retval);
}
