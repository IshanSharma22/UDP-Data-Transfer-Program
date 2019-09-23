#include "stdint.h"
#include <time.h>

#define PACKET_SIZE 1015
#define HEADER_SIZE 9
#define MAXS 1024 // Packet + Header size
#define WAITTIME 0.5  // In seconds (500 ms)

#define MAXSEQ 30720 // This is a value in bytes
#define WINDOW 5120  // This is a value in bytes

#define BILLION 1000000000L
#define RECEIVEWINDOW 25


class Packet {

public:
  struct PacHeader {
    uint16_t SeqNo = 0; // 2 bytes
    uint16_t AckNo = 0; // 2 bytes
    uint8_t ackFlagVal = 0; 
    uint8_t finFlagVal = 0;
    uint8_t synFlagVal = 0;
    uint16_t payloadLen = 0;       // 2 bytes
  } header;

  uint8_t payload[PACKET_SIZE] = {0}; // 1015 bytes for payload
  bool hasBeeenSent = false;
  bool acked = false;
  long long trueFileSeqNum = 0;
  struct timespec start;

  // constructor
  Packet() {
    memset((char *)&payload, 0, PACKET_SIZE);
    header.ackFlagVal = 0;
    header.finFlagVal = 0;
    header.synFlagVal = 0;
  }

  Packet &operator=(const Packet &other) { // operator overload to equate one packet with another
    if (this != &other) {
      hasBeeenSent = other.hasBeeenSent;
      acked = other.acked;
      header.SeqNo = other.header.SeqNo;
      header.AckNo = other.header.AckNo;
      header.payloadLen = other.header.payloadLen;
      header.ackFlagVal = other.header.ackFlagVal;
      header.synFlagVal = other.header.synFlagVal;
      header.finFlagVal = other.header.finFlagVal;
      start = other.start;
      int i = 0;
      while ( i < PACKET_SIZE) {
        payload[i] = other.payload[i];
        i++;
      }
    }
    return *this;
  }

  
  bool retHasSent() { return hasBeeenSent; }
  bool retHasBeenAcked() { return acked; }
  uint16_t retLengVal() { return header.payloadLen; }
  bool retAckFlag() { return header.ackFlagVal == 1; }
  bool retSynFLag() { return header.synFlagVal == 1; }
  bool retFinFlag() { return header.finFlagVal == 1; }
  uint16_t retSeqNo() { return header.SeqNo; }
  uint16_t retAckNo() { return header.AckNo; }
  void changeSeqNo(uint16_t seq) { header.SeqNo = seq; }
  void changeAckNo(uint16_t ack) { header.AckNo = ack; }

  void retPayload(uint8_t *buff);

  void replaceFlagVals(uint8_t a, uint8_t s, uint8_t f);

  void fillPayload(uint8_t *buff, int len);

  void emptyPayload();

  void initClockTimer();

  void changeSentBool(int i);

  void changeAckBool(int i);

  void BufferPacketTranslator(uint8_t* tempbuf, int det);
  bool timeOut(int numRTO);
};

bool Packet::timeOut(int numRTO) {
    struct timespec stop;
    if (clock_gettime(CLOCK_MONOTONIC, &stop) < 0) {
      perror("Error in clock gettime");
    }
    return ((long double)((stop.tv_sec - start.tv_sec) +
                          (long double)(stop.tv_nsec - start.tv_nsec) /
                              BILLION) > numRTO * WAITTIME);
 }


void Packet::BufferPacketTranslator(uint8_t* tempbuf, int det){
    // det = 0 => translate buffer to packet
    // det = 1 => translate packet to buffer
    if (det == 0){
      header.SeqNo = (tempbuf[1] << 8) | tempbuf[0];
      header.AckNo = (tempbuf[3] << 8) | tempbuf[2];
      header.ackFlagVal = tempbuf[4];
      header.synFlagVal = tempbuf[5];
      header.finFlagVal = tempbuf[6];
      header.payloadLen = (tempbuf[8] << 8) | tempbuf[7];

      int i = 0;
      while ( i < header.payloadLen) {
        payload[i] = tempbuf[9 + i];
        i++;
      }
    }

    else if ( det == 1){
      memset((char *)tempbuf, 0, MAXS);
      memcpy(tempbuf, &header.SeqNo, sizeof(uint16_t));
      memcpy(tempbuf + sizeof(uint16_t), &header.AckNo, sizeof(uint16_t));
      memcpy(tempbuf + 2 * sizeof(uint16_t), &header.ackFlagVal, sizeof(uint8_t));
      memcpy(tempbuf + 2 * sizeof(uint16_t) + sizeof(uint8_t), &header.synFlagVal,
             sizeof(uint8_t));
      memcpy(tempbuf + 2 * sizeof(uint16_t) + 2 * sizeof(uint8_t), &header.finFlagVal,
             sizeof(uint8_t));
      memcpy(tempbuf + 2 * sizeof(uint16_t) + 3 * sizeof(uint8_t), &header.payloadLen,
             sizeof(uint16_t));
      if (header.payloadLen >= 1) {
        memcpy(tempbuf + 3 * sizeof(uint16_t) + 3 * sizeof(uint8_t), &payload, header.payloadLen);
      }
    }

 }

void Packet::changeAckBool(int i) 
  { 
    if (i)
      acked = true;
    else
      acked = false;
  }

void Packet::changeSentBool(int i)
  { 
    if (i==1)
      hasBeeenSent = true;
    else
      hasBeeenSent = false;
  }

 void Packet:: fillPayload(uint8_t *buff, int len) {
    if (len > PACKET_SIZE) {
      return;
    } 
    else {
      int i = 0;
      while ( i < len) {
        payload[i] = buff[i];
        i++;
      }
      header.payloadLen = len;
    }
}

void Packet::initClockTimer() {
    if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
      perror("clock gettime");
    }
}

void Packet:: emptyPayload() {
    int i = 0;
    while ( i < PACKET_SIZE ){
      payload[i] = 0;
      i++;
    }
}

 void Packet:: replaceFlagVals(uint8_t a, uint8_t s, uint8_t f) {
    header.ackFlagVal = a;
    header.synFlagVal = s;
    header.finFlagVal = f;
  }

void Packet::retPayload(uint8_t *buff) {
    memset((char *)buff, 0, MAXS);
    for (int i = 0; i < header.payloadLen; i++)
      buff[i] = payload[i];
}
