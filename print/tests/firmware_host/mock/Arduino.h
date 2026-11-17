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
struct SerialT{ void begin(long){} void setRxBufferSize(int){} int available(){return 0;} int read(){return -1;}
 void print(const char*c){fputs(c,stdout);} void print(long v){printf("%ld",v);} void println(const char*c=""){puts(c);}
 void printf(const char*f,...){va_list a;va_start(a,f);vprintf(f,a);va_end(a);} };
extern SerialT Serial;
