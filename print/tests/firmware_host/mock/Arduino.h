#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <algorithm>
#include <cstdarg>
using std::min; using std::max; using std::isnan;
#define IRAM_ATTR
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define RISING 1
#define ADC_11db 3
#define F(x) x
inline void pinMode(int,int){} inline void digitalWrite(int,int){} inline int digitalRead(int){return 0;}
inline void delayMicroseconds(int){} inline void delay(int){} inline unsigned long millis(){return 0;} inline unsigned long micros(){return 0;}
inline int analogRead(int){return 0;} inline void analogReadResolution(int){} inline void analogSetPinAttenuation(int,int){}
inline int digitalPinToInterrupt(int p){return p;} inline void attachInterrupt(int,void(*)(),int){}
inline void noInterrupts(){} inline void interrupts(){}
#define REG_READ(x) 0
#define GPIO_IN_REG 0
inline long lroundf(float x){return std::lround(x);}
class String { public: std::string s; String(){} String(const char*c):s(c){} String(std::string x):s(x){}
 void trim(){size_t a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");s=a==std::string::npos?"":s.substr(a,b-a+1);}
 int indexOf(char c)const{size_t p=s.find(c);return p==std::string::npos?-1:(int)p;}
 String substring(int a,int b=-1)const{if(a<0)a=0;if((size_t)a>s.size())return String("");return String(b<0?s.substr(a):s.substr(a,b-a));}
 long toInt()const{return atol(s.c_str());} float toFloat()const{return atof(s.c_str());}
 void toLowerCase(){for(auto&c:s)c=tolower(c);} size_t length()const{return s.size();}
 bool operator==(const char*c)const{return s==c;} String& operator+=(char c){s+=c;return *this;} };
#define SERIAL_8N1 0x800001c
// SerialT: simulated serial port. Writes to standard output (toStdout) and/or to a
// capturable buffer (outbuf). Can be fed input via inbuf (serial protocol test).
struct SerialT{
 std::string outbuf; bool toStdout=true;
 std::string inbuf; size_t inpos=0;
 void begin(long){} void begin(long,int,int,int){} void setRxBufferSize(int){}
 int available(){return inpos<inbuf.size()?(int)(inbuf.size()-inpos):0;}
 int read(){return inpos<inbuf.size()?(unsigned char)inbuf[inpos++]:-1;}
 void feed(const char*s){inbuf+=s;}                 // adds bytes to read
 std::string drain(){std::string o=outbuf;outbuf.clear();return o;}   // gets and clears the output
 void emit(const std::string&s){outbuf+=s;if(toStdout)fputs(s.c_str(),stdout);}
 void print(const char*c){emit(c);} void print(long v){char b[32];snprintf(b,sizeof b,"%ld",v);emit(b);}
 void println(const char*c=""){emit(std::string(c)+"\n");}
 void printf(const char*f,...){char b[256];va_list a;va_start(a,f);vsnprintf(b,sizeof b,f,a);va_end(a);emit(b);} };
extern SerialT Serial;
extern SerialT Serial2;
