/*
* Originial:
 * Copyright (c) 2012 Dan Wilcox <danomatika@gmail.com>
 *
 * BSD Simplified License.
 */
 
 // "pd2jack" by Doug Garmon
 
#include "PdObject.h"
#include <iostream>

using namespace std;
using namespace pd;

// Stubs - not currently implemented in pd2jack...

//--------------------------------------------------------------		
void PdObject::receiveBang(const std::string& dest) {
	cout << "P2J: bang " << dest << endl;
}

//void PdObject::receiveFloat(const std::string& dest, float num) {
//	cout << "CPP: float " << dest << ": " << num << endl;
//}

void PdObject::receiveSymbol(const std::string& dest, const std::string& symbol) {
	cout << "P2J: symbol " << dest << ": " << symbol << endl;
}

void PdObject::receiveList(const std::string& dest, const List& list) {
	cout << "P2J: list " << dest << ": " << list << endl;
}

//void PdObject::receiveMessage(const std::string& dest, const std::string& msg, const List& list) {
//	cout << "P2J: message " << dest << ": " << msg << " " << list.toString() << list.types() << endl;
//}

