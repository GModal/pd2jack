/*
* Originial:
 * Copyright (c) 2012 Dan Wilcox <danomatika@gmail.com>
 *
 * BSD Simplified License.
 */
 
 // "pd2jack" by Doug Garmon
 
#include "PdObject.hpp"
#include <iostream>

using namespace std;
using namespace pd;

// Stubs - not currently implemented in pd2jack...

//--------------------------------------------------------------		
void PdObject::receiveBang(const std::string& dest) {
	cout << "P2J: bang " << dest << endl;
}

void PdObject::receiveSymbol(const std::string& dest, const std::string& symbol) {
	cout << "P2J: symbol " << dest << ": " << symbol << endl;
}
