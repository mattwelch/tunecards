#include "HttpClient.h"

/*
  RedisClient.cpp - A simple client library for Redis.
  Original Code - Thomas Lohmüller
  https://github.com/tht/RedisClient

  Adapted for Spark Core by Chris Howard - chris@kitard.com

  See http://redis.io/topics/protocol for details of the redis protocol


  Changes
  - Added gcc pragam to avoid warnings throwing errors (deprecated conversion from string constant to 'char*')
  - Obvious includes commented out / removed
  - Using Spark TCPClient instead of Arduino EthernetClient
  - Updated connect function

  ToDo
  - Investigate Subscribe capability - add callback
  - Move declarations back to .h once Spark IDE fixed

*/

#pragma GCC diagnostic ignored "-Wwrite-strings"


#define ARDUINO_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>


enum RedisResult {
    RedisResult_NONE,
    RedisResult_NOTRECEIVED,
    RedisResult_SINGLELINE,
    RedisResult_ERROR,
    RedisResult_INTEGER,
    RedisResult_BULK,
    RedisResult_MULTIBULK
};


class RedisClient {
private:
    TCPClient* _client;
    uint8_t *ip;
    char* domain;
    uint16_t port;
    uint8_t _results_waiting;
    RedisResult _resType;

    // internal methods for string manipulation
    inline uint8_t addChar(char* buffer, uint8_t offset, char chr);
    inline uint8_t addData(char *buffer, uint8_t offset, char* str, uint8_t len);
    inline uint8_t addString(char *buffer, uint8_t offset, char* str);
    inline uint8_t addInt(char *buffer, uint8_t offset, int num);
    inline uint8_t addNewline(char *buffer, uint8_t offset);

    // internal methods for construction redis packets in Ethernet Chip's memory
    void startCmd(uint8_t num_args);
    void sendHashArg(char* key, uint16_t index);

    // internal commands to parse redis results
    uint16_t readSingleline(char *buffer);
    uint16_t readInt();
    void flushResult();
    bool connected();


public:
    RedisClient();
    RedisClient(uint8_t *, uint16_t, TCPClient &);
    RedisClient(char *, uint16_t, TCPClient &);

    bool connect();

    // simple commands not needing additional arguments
    uint16_t INCR(char* key);  // returns incremented number
    uint8_t  LTRIM(char* list, int16_t start, int16_t stop); // returns 1 on success
    uint8_t  GET(char* key);   // returns 1 on success, get value using resultBulk(buffer, buflen)
    uint8_t setName(char* clientName);

    // commands needing arguments using sendArg(...) and ending using end*
    uint8_t startPUBLISH(char* channel); // needs one argument
    uint8_t startHSET(char* key);        // needs TWO arguments
    uint8_t startHSET(char* key, uint16_t index); // needs TWO arguments
    uint8_t startRPUSH(char* list);      // needs one argument

    // ... to send arguments for commands above
    void sendArg(char* arg); // to send string which is \0 terminated
    void sendArg(uint8_t* arg, uint8_t len);
    void sendArg(int arg);
    void sendArgRFMData(uint8_t header, uint8_t *data, uint8_t data_len); // format RFM12B packet

    // end commands started using start*
    // all return 1 on success, 0 on failure
    uint8_t endPUBLISH(uint16_t *subscribers);
    uint8_t endPUBLISH();
    uint8_t endHSET();
    uint8_t endRPUSH(uint16_t *listitems);
    uint8_t endRPUSH();

    // read back results
    RedisResult resultType();

    uint16_t resultInt();
    uint16_t resultStatus(char *buffer);
    uint16_t resultError(char *buffer);
    uint16_t resultBulk(char *buffer, uint16_t buffer_size);
};

extern char* itoa(int a, char* buffer, unsigned char radix);

// Constructor
RedisClient::RedisClient() {
   this->_client = NULL;
}

RedisClient::RedisClient(uint8_t *ip, uint16_t port, TCPClient& client) {
   this->_client = &client;
   this->ip = ip;
   this->port = port;
   this->domain = NULL;
}

RedisClient::RedisClient(char* domain, uint16_t port, TCPClient& client) {
   this->_client = &client;
   this->domain = domain;
   this->port = port;
}


// CONNECT
bool RedisClient::connect() {

   if (!connected()) {
      int result = 0;

      if (domain != NULL) {
        result = _client->connect(this->domain, this->port);
      } else {
        result = _client->connect(this->ip, this->port);
      }

      if (!result) {
          _client->stop();
          return false;
      }

      return true;

   } else {
      flushResult();
   }

   return false;
}


uint16_t RedisClient::INCR(char* key) {
    connect();

    _resType = RedisResult_NOTRECEIVED;
    startCmd(2);
    sendArg("INCR");
    sendArg(key);

    return resultInt();
}


uint8_t RedisClient::LTRIM(char* list, int16_t start, int16_t stop) {
    connect();

    _resType = RedisResult_NOTRECEIVED;
    startCmd(4);
    sendArg("LTRIM");
    sendArg(list);
    sendArg(start);
    sendArg(stop);

    return resultType() == RedisResult_SINGLELINE;
}


uint8_t RedisClient::GET(char* key) {
    connect();

    _resType = RedisResult_NOTRECEIVED;
    startCmd(2);
    sendArg("GET");
    sendArg(key);

    return resultType() == RedisResult_BULK;
}


uint8_t RedisClient::setName(char* clientName) {
    connect();

    _resType = RedisResult_NOTRECEIVED;
    startCmd(3);
    sendArg("client");
    sendArg("setname");
    sendArg(clientName);

    return resultType() == RedisResult_BULK;
}


uint8_t RedisClient::startPUBLISH(char* channel) {
    connect();

    _resType = RedisResult_NOTRECEIVED;
    startCmd(3);
    sendArg("PUBLISH");
    sendArg(channel);
}


uint8_t RedisClient::startHSET(char* key) {
    connect();

    _resType = RedisResult_NOTRECEIVED;
    startCmd(4);
    sendArg("HSET");
    sendArg(key);
}


uint8_t RedisClient::endPUBLISH(uint16_t *subscribers) {
    if (resultType() == RedisResult_INTEGER) {
        *subscribers = resultInt();
        return 1;
    } else {
        return 0;
    }
}


uint8_t RedisClient::endPUBLISH() {
    uint16_t subscribers;
    return endPUBLISH(&subscribers);
}


uint8_t RedisClient::endHSET() {
    if (resultType() == RedisResult_INTEGER) {
        resultInt(); // throw away result
        return 1;
    } else {
        return 0;
    }
}


uint8_t RedisClient::endRPUSH(uint16_t *listitems) {
    if (resultType() == RedisResult_INTEGER) {
        *listitems = resultInt();
        return 1;
    } else {
        return 0;
    }
}

uint8_t RedisClient::endRPUSH() {
    uint16_t listitems;
    return endPUBLISH(&listitems);
}


uint8_t  RedisClient::startHSET(char* key, uint16_t index) {
    char buffer[30];
    uint8_t offset = addString(buffer, 0, key);
    offset = addChar(buffer, offset, ':');
    offset = addInt(buffer, offset, index);
    offset = addChar(buffer, offset, '\0');
    startHSET(buffer);
}


uint8_t RedisClient::startRPUSH(char* list) {
    connect();

    _resType = RedisResult_NOTRECEIVED;
    startCmd(3);
    sendArg("RPUSH");
    sendArg(list);
}


RedisResult RedisClient::resultType() {
    if (_resType == RedisResult_NOTRECEIVED) {
        while(! _client->available() )
            delay(1);

        switch( _client->read() ) {
            case  '+': _resType = RedisResult_SINGLELINE; break;
            case  '-': _resType = RedisResult_ERROR; break;
            case  ':': _resType = RedisResult_INTEGER; break;
            case  '$': _resType = RedisResult_BULK; break;
            case  '*': _resType = RedisResult_MULTIBULK; break;
        }
    }

    if (_resType > RedisResult_NOTRECEIVED)
        return _resType;

    return RedisResult_NONE;
}


uint16_t RedisClient::readInt() {
    uint16_t res = 0;
    char chr;

    while(1) {
        while(! _client->available() )
            delay(1);

        chr = _client->read();
        if (chr >= '0' && chr <= '9') {
            res *= 10;
            res += chr - '0';
        } else if (chr == '\r') {
            // ignore
        } else if (chr == '\n') {
            _resType = RedisResult_NONE;
            return res; // returning result
        }
    }
}


uint16_t RedisClient::resultInt() {
    if (resultType() != RedisResult_INTEGER)
        return 0;

    return readInt();
}


uint16_t RedisClient::readSingleline(char *buffer) {
    char chr;
    uint8_t offset = 0;

    while(1) {
        while(! _client->available() )
            delay(1);

        chr = _client->read();
        if (chr >= 32 && chr <= 126) {
            buffer[offset++] = chr;
        } else if (chr == '\r') {
            // ignore
        } else if (chr == '\n') {
            _resType = RedisResult_NONE;
            buffer[offset++] = '\0';
            return offset; // returning length of string
        }
    }
}


uint16_t RedisClient::resultStatus(char *buffer) {
    if (resultType() != RedisResult_SINGLELINE)
        return 0;

    return readSingleline(buffer);
}


uint16_t RedisClient::resultError(char *buffer) {
    if (resultType() != RedisResult_ERROR)
        return 0;

    return readSingleline(buffer);
}


// Note: buffer_size has to be bigger than result as result there'll be always a \0 appendet
uint16_t RedisClient::resultBulk(char *buffer, uint16_t buffer_size) {
    if (resultType() != RedisResult_BULK)
        return 0;


    uint16_t result_size = readInt();

    while(_client->available() < result_size+(result_size == 1?-1:2))
        delay(1);

    _client->read((uint8_t*)buffer, result_size);
    buffer[result_size==1?0:result_size] = '\0';

    // throw away newline
    _client->read(); _client->read();

    return result_size;
}


void RedisClient::flushResult() {
    switch (resultType()) {
        case RedisResult_SINGLELINE:
        case RedisResult_ERROR:
            char buffer[200];
            readSingleline(buffer);
            break;

        case RedisResult_INTEGER:
            readInt();
            break;
    }
}


inline uint8_t RedisClient::addChar(char* buffer, uint8_t offset, char chr) {
    buffer[offset] = chr;
    return offset+1;
}

inline uint8_t RedisClient::addData(char *buffer, uint8_t offset, char* str, uint8_t len) {
    memcpy( buffer+offset, str, len);
    return offset + len;
}

inline uint8_t RedisClient::addString(char *buffer, uint8_t offset, char* str) {
    return addData(buffer, offset, str, strlen(str));
}

inline uint8_t RedisClient::addInt(char *buffer, uint8_t offset, int num) {
    itoa(num, buffer+offset, 10);
    return offset + strlen(buffer+offset);
}

inline uint8_t RedisClient::addNewline(char *buffer, uint8_t offset) {
    buffer[offset++] = '\r';
    buffer[offset++] = '\n';
    return offset;
}


void RedisClient::sendArg(uint8_t* arg, uint8_t len) {
    char buffer[20];
    uint8_t offset = 0;
    offset = addChar(buffer, offset, '$');
    offset = addInt(buffer, offset, len);
    offset = addNewline(buffer, offset);
    _client->write((uint8_t*)buffer, offset);

    _client->write(arg, len);

    offset = 0;
    offset = addNewline(buffer, offset);
    _client->write((uint8_t*)buffer, offset);
}


void RedisClient::sendHashArg(char* key, uint16_t index) {
    char buffer[30];
    uint8_t offset = addString(buffer, 0, key);
    offset = addChar(buffer, offset, ':');
    offset = addInt(buffer, offset, index);
    sendArg((uint8_t*)buffer, offset);
}


void RedisClient::sendArg(char* arg) {
    sendArg((uint8_t*)arg, strlen(arg));
}


void RedisClient::sendArg(int arg) {
    char buffer[20];
    uint8_t offset = addInt(buffer, 0, arg);
    sendArg((uint8_t*)buffer, offset);
}


void RedisClient::startCmd(uint8_t num_args) {
    char buffer[20];
    uint8_t offset = 0;

    // preparing argument count
    offset = addChar(buffer, offset, '*');
    offset = addInt(buffer, offset, num_args);
    offset = addNewline(buffer, offset);
    _client->write((uint8_t*)buffer, offset);
}


void RedisClient::sendArgRFMData(uint8_t header, uint8_t *data, uint8_t data_len) {
    char buffer[200];
    uint8_t offset = 0;

    offset = addString(buffer, offset, "OK ");
    offset = addInt(buffer, offset, header);
    for (int i=0; i<data_len; i++) {
        offset = addChar(buffer, offset, ' ');
        offset = addInt(buffer, offset, data[i]);
    }

    sendArg((uint8_t*)buffer, offset);
}


bool RedisClient::connected() {
   bool rc;
   if (_client == NULL ) {
      rc = false;
   } else {
      rc = (int)_client->connected();
      if (!rc) _client->stop();
   }
   return rc;
}

#include "RFID.h"

/* Define the pins used for the SS (SDA) and RST (reset) pins for BOTH hardware and software SPI */
/* Change as required */
#define SS_PIN      A2      // Same pin used as hardware SPI (SS)
#define RST_PIN     D2

/* Define the pins used for the DATA OUT (MOSI), DATA IN (MISO) and CLOCK (SCK) pins for SOFTWARE SPI ONLY */
/* Change as required and may be same as hardware SPI as listed in comments */
#define MOSI_PIN    D3      // hardware SPI: A5
#define MISO_PIN    D4      //     "     " : A4
#define SCK_PIN     D5      //     "     " : A3

/* Create an instance of the RFID library */
#if defined(_USE_SOFT_SPI)
    RFID(int chipSelectPin, int NRSTPD, uint8_t mosiPin, uint8_t misoPin, uint8_t clockPin);    // Software SPI
#else
    RFID RC522(SS_PIN, RST_PIN);    // Hardware SPI
#endif


HttpClient http;

http_header_t headers[] = {
    { "Accept" , "*/*"},
    { NULL, NULL }
};

IPAddress sonosServer(192, 168, 0, 100);

http_request_t request;
http_response_t response;

const char sonosRoom[] = "family%20room";

TCPClient tcpClient;
byte ip[] = { 192, 168, 0, 100 };
uint16_t port = 6379;
RedisClient client(ip, port, tcpClient);
bool gotCard = false;
uint8_t noCardCounter = 0;
char uid[9];
SYSTEM_MODE(AUTOMATIC); //Only connect when we tell it to

unsigned int nextTime = 0;    // Next time to contact the server

void setup() {
Spark.variable("uidval", &uid, STRING);
  Serial.begin(9600);

  Serial.println("setup");
  request.ip = sonosServer;
  request.port = 5005;


  /* Enable the SPI interface */
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  SPI.setClockDivider(SPI_CLOCK_DIV8);
  SPI.begin();

  /* Initialise the RFID reader */
  RC522.init();
}

void loop() {
  if (nextTime > millis()) {
      return;
  }


  /* Has a card been detected? */
  if (RC522.isCard())
  {
    Serial.println("Card detected:");
    noCardCounter = 0;
    if (gotCard) {
      return;
    }
    gotCard=true;
    char buffer[100];

    /* If so then get its serial number */
    RC522.readCardSerial();


    char a[11];
    /* Output the serial number to the UART */
    for(byte i = 0; i < sizeof(RC522.serNum); i++)
    {
      char aa[3];
      itoa(RC522.serNum[i],aa,16);
      if (RC522.serNum[i]<0x10) {
        strcat(a,"0");
      }
      strcat(a,aa);
    }
    strcpy(uid,a);
    uint8_t g = client.GET(a);
    if (g == 1) {
      uint16_t resultSize = client.resultBulk(buffer, 100);
      if (resultSize < 5) {
        return;
      }
      char finalPath[128];
      sprintf(finalPath,"/%s%s",sonosRoom,buffer);
      request.path = finalPath;

      http.get(request, response,headers);
      Serial.print("Application>\tResponse status: ");
      Serial.println(response.status);

      Serial.print("Application>\tHTTP Response Body: ");
      Serial.println(response.body);

    }
  }
  else {
    Serial.println("no card");
    noCardCounter++;
    if (noCardCounter == 2 && gotCard) {
      gotCard=false;
      char pauseURL[50];
      sprintf(pauseURL,"/%s/pause",sonosRoom);
      request.path = pauseURL;

      http.get(request, response,headers);
      Serial.print("Application>\tResponse status: ");
      Serial.println(response.status);

      Serial.print("Application>\tHTTP Response Body: ");
      Serial.println(response.body);

    }
  }

nextTime = millis() + 1000;
}
