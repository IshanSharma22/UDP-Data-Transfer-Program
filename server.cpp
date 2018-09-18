#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <locale>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <cstdlib>
#include <vector>
#include "packet.h"

#define HEADERLEN 9 // Header size is 9 Bytes
#define MAXS 1024 // Packet + Header size
#define PACLEN (MAXS - HEADERLEN) // MAX Size minus header size

#define MAXSEQNO 30720 // This is a value in bytes, both these values specified by spec
#define WINDOW 5120  // This is a value in bytes


//http://man7.org/linux/man-pages/man2/recvmsg.2.html

using namespace std;

int port;
long long servSeqIndex = 0;
long long cliSeqIndex = 0;

Packet startWin[2];
vector<Packet> pacVecWindow;


string startConnect(int sockfd, struct sockaddr_in &destAddr) { //function manages the SYN and ACK with client, returns file name stirng

  int recievedLen;

  uint8_t buf[MAXS + 1];		//uint8_t = 1 Byte

  socklen_t sin_size = sizeof(struct sockaddr_in);

  Packet p;
  Packet pacSendBack;
  uint8_t bufferToClient[MAXS];

  string fName = ""; //file name

  srand(time(NULL)); //needed

  for (;;) {

    recievedLen = recvfrom(sockfd, buf, MAXS, 0 | MSG_DONTWAIT, (struct sockaddr *)&destAddr, &sin_size); //http://man7.org/linux/man-pages/man2/recvmsg.2.html
    
    if (recievedLen >= 1) {
      buf[recievedLen] = 0;

      p.BufferPacketTranslator(buf, 0);
      cout << "Receiving packet " << p.retSeqNo() << endl;

      if (p.retSynFLag()) { // have gotten a SYN from client

        if (!startWin[0].retHasSent()) {

          cliSeqIndex = p.retSeqNo();
          pacSendBack.replaceFlagVals(1, 1, 0);
          servSeqIndex = rand() % 10000;
          pacSendBack.changeSeqNo(servSeqIndex % MAXSEQNO);
          pacSendBack.changeAckNo(cliSeqIndex);
          pacSendBack.BufferPacketTranslator(bufferToClient, 1); //pac to buffer
          cout << "Sending packet " << servSeqIndex << " " << WINDOW << " SYN" << endl;
          if (sendto(sockfd, &bufferToClient, MAXS, 0, (struct sockaddr *)&destAddr,sizeof(destAddr)) <= -1) {
              perror("Error, unable to send to client.");
              exit(1);
          }
          pacSendBack.initClockTimer();
          pacSendBack.changeSentBool(1);
          startWin[0] = pacSendBack;
        }

        else { // trigger retransmit, restart timer
          startWin[0].BufferPacketTranslator(bufferToClient, 1);
          cout << "Sending packet " << servSeqIndex << " " << WINDOW << " Retransmission "<< " SYN" << endl;
          if (sendto(sockfd, &bufferToClient, MAXS, 0, (struct sockaddr *)&destAddr, sizeof(destAddr)) <= -1) {
              perror("Error, unable to send to client.");
              exit(1);
          }
          startWin[0].initClockTimer();
        }
      } 

      else if (p.retAckFlag() && startWin[0].retHasSent() && p.retAckNo() == startWin[0].retSeqNo()) { // are getting ack for the syn we sent along with file name
        startWin[0].changeAckBool(1);
        cliSeqIndex = p.retSeqNo();
        servSeqIndex ++;
        pacSendBack.replaceFlagVals(1, 0, 0);
        pacSendBack.changeSeqNo(servSeqIndex % MAXSEQNO);
        pacSendBack.changeAckNo(cliSeqIndex);
        int i = 0;
        while ( i <p.retLengVal() ) {
          fName += (char)p.payload[i];
          i++;
        }

        pacSendBack.BufferPacketTranslator(bufferToClient, 1);
        cout << "Sending packet " << servSeqIndex << " " << WINDOW << endl;
        if (sendto(sockfd, &bufferToClient, MAXS, 0, (struct sockaddr *)&destAddr, sizeof(destAddr)) <= -1) {
            perror("Error, unable to send to client.");
            exit(1);
        }
        pacSendBack.changeSentBool(1);
        pacSendBack.initClockTimer();
        startWin[1] = pacSendBack;
        return fName;
      }

      if (startWin[0].retHasSent() && !startWin[0].retHasBeenAcked() && startWin[0].timeOut(1)) {
        
        startWin[0].BufferPacketTranslator(bufferToClient, 1);

        cout << "Sending packet " << servSeqIndex << " " << WINDOW << endl;
        if (sendto(sockfd, &bufferToClient, MAXS, 0, (struct sockaddr *)&destAddr,
                   sizeof(destAddr)) <= -1) {
          perror("Error, unable to send to client.");
          exit(1);
        }
        startWin[0].initClockTimer();

      }

      memset((char *)&buf, 0, MAXS + 1);
    }
  }
}


void delieverFileChunks(int sockfd, struct sockaddr_in &destAddr,long long fileSize, streampos endFile, char *fileContentBuf) {
  long noPacs = 0;
  uint8_t bufferToClient[MAXS];
  uint8_t buf[MAXS + 1];
  int recievedLen;
  Packet p;
  Packet ack;
  noPacs = endFile / PACLEN + 1;
  socklen_t sin_size = sizeof(struct sockaddr_in);
  servSeqIndex++;

  long i = 0;
  
  for(;;) {

    if ( (i <= noPacs - 1) && (pacVecWindow.size() < WINDOW / MAXS)) {
      p.replaceFlagVals(0, 0, 0);
      if (i == noPacs - 1) {
        p.fillPayload((uint8_t *)(fileContentBuf + i * PACLEN), (int)(fileSize - PACLEN * i));
      } 
      else {
        p.fillPayload((uint8_t *)(fileContentBuf + i * PACLEN), PACLEN);
      }

      p.changeSeqNo(servSeqIndex % MAXSEQNO);
      p.changeAckNo(cliSeqIndex);
      p.BufferPacketTranslator(bufferToClient, 1);
      pacVecWindow.push_back(p); // add to the end of vector

      cout << "Sending packet " << p.retSeqNo() << " " << WINDOW << endl;
      if (sendto(sockfd, &bufferToClient, MAXS, 0, (struct sockaddr *)&destAddr, sizeof(destAddr)) <= -1){
        perror("Error, unable to send to client.");
        exit(1);
      }

      servSeqIndex += MAXS;
      pacVecWindow.back().initClockTimer();
      i++;
    }

    recievedLen = recvfrom(sockfd, buf, MAXS, 0 | MSG_DONTWAIT,(struct sockaddr *)&destAddr, &sin_size);
    if (recievedLen >= 1) {
      buf[recievedLen] = 0;
      ack.BufferPacketTranslator(buf, 0);

      cout << "Receiving packet " << ack.retAckNo() << endl;
      if (ack.retSeqNo() == startWin[1].retAckNo()) {
        startWin[1].BufferPacketTranslator(bufferToClient, 1);
        cout << "Sending packet " << startWin[1].retSeqNo() << " "<< WINDOW << " Retransmission" << endl;
        if (sendto(sockfd, &bufferToClient, MAXS, 0, (struct sockaddr *)&destAddr,
                   sizeof(destAddr)) <= -1){
          perror("Error, unable to send to client.");
          exit(1);
        }
      } 
      else {
        cliSeqIndex = ack.retSeqNo();
        unsigned long j = 0;
        while ( j < pacVecWindow.size() ){
          if (pacVecWindow[j].retSeqNo() == ack.retAckNo())
            pacVecWindow[j].changeAckBool(1);
          j++;
    	}

        for(;;) {
          if (pacVecWindow.size() >= 1 && pacVecWindow[0].retHasBeenAcked()) {
            if (pacVecWindow.size() > 1) {
              unsigned long k = 0;
              while (k < pacVecWindow.size() - 1 ) {
                pacVecWindow[k] = pacVecWindow[k + 1];
                k++;
              }
            }
            pacVecWindow.pop_back();
            if (pacVecWindow.size() == 0 && i >= noPacs)
              return;
          } else
            break;
        }
      }
    }

    unsigned long j = 0;
    while (j < pacVecWindow.size() ){

      if (pacVecWindow[j].timeOut(1)) {
        pacVecWindow[j].BufferPacketTranslator(bufferToClient, 1);
        cout << "Sending packet " << pacVecWindow[j].retSeqNo() << " "
             << WINDOW << " Retransmission" << endl;
        if (sendto(sockfd, &bufferToClient, MAXS, 0, (struct sockaddr *)&destAddr, sizeof(destAddr)) <= -1){
          perror("Error, unable to send to client.");
          exit(1);
        }

        pacVecWindow[j].initClockTimer();
      }
      j++;
    }
  }
}

int main(int argc, char *argv[]) {

  if (argc < 2 || argc > 2) {
    perror("Incorrect number of arguments, correct usage as follows: ./server [portnumebr]");
    exit(1);
  }
  port = atoi(argv[1]);

  int sockfd;
  struct sockaddr_in my_addr;
  struct sockaddr_in destAddr;
  socklen_t sin_size;

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) <= -1){
    perror("Error with socket.");
    exit(1);
  }

  memset((char *)&my_addr, 0, sizeof(my_addr));
  my_addr.sin_family = AF_INET;
  my_addr.sin_port = htons(port);
  my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (::bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) <= -1){
    perror("Error, bind.");
    exit(1);
  }
  sin_size = sizeof(struct sockaddr_in);

  // Initiate connection and get the fName
  string fName = startConnect(sockfd, destAddr);

  char *fileContentBuf = nullptr;
  long long fileSize = 0;
  ifstream inFile;

  struct stat buffer;
  int fileExists = 0;
  fileExists = stat(fName.c_str(), &buffer);

  if (!fileExists) {
    inFile.open(fName.c_str(), ios::in | ios::binary | ios::ate);
    streampos endFile;

    if (inFile.is_open() != 0) {
      endFile = inFile.tellg();
      fileSize = (long long)(endFile);
      fileContentBuf = new char[(long long)(endFile) + 1];
      inFile.seekg(0, ios::beg);
      inFile.read(fileContentBuf, endFile);
      inFile.close();
    }
    delieverFileChunks(sockfd, destAddr, fileSize, endFile, fileContentBuf); //send file
    delete fileContentBuf;
  }

  // Start termination processes
  int recievedLen = 0;
  int finCount = 0;
  uint8_t buf[MAXS + 1];

  sin_size = sizeof(struct sockaddr_in);
  Packet pacACK;
  Packet pacReciev;
  Packet pacFin;

  uint8_t bufferToClient[MAXS];
  pacFin.replaceFlagVals(0, 0, 1);
  servSeqIndex++;
  pacFin.changeSeqNo(servSeqIndex % MAXSEQNO);
  pacFin.changeAckNo(cliSeqIndex);
  pacFin.BufferPacketTranslator(bufferToClient, 1);
  cout << "Sending packet " << pacFin.retSeqNo() << " " << WINDOW << " FIN" << endl;
  if (sendto(sockfd, &bufferToClient, MAXS, 0, (struct sockaddr *)&destAddr, sizeof(destAddr)) <= -1){
    perror("Error, unable to send to client.");
    exit(1);
  }
  pacFin.initClockTimer();
  pacACK.initClockTimer();

  bool alreadySent = false;
  for(;;) {
    recievedLen = recvfrom(sockfd, buf, MAXS, 0 | MSG_DONTWAIT, (struct sockaddr *)&destAddr, &sin_size);
    if (recievedLen >= 1) {
      buf[recievedLen] = 0;
      pacReciev.BufferPacketTranslator(buf, 0);
      cout << "Receiving packet " << pacReciev.retAckNo() << endl;
      if (pacReciev.retAckNo() == pacFin.retSeqNo()) {
        pacFin.changeAckBool(1);
      } else if (pacReciev.retFinFlag()) {
        // We received the client fin, so now we send back an ack and wait
        // for 2 RTO before quitting
        servSeqIndex++;
        pacACK.changeSeqNo(servSeqIndex % MAXSEQNO);
        pacACK.changeAckNo(pacReciev.retSeqNo());
        pacACK.replaceFlagVals(1, 0, 0);
        cout << "Sending packet " << pacACK.retSeqNo() << " " << WINDOW << endl;
        pacACK.BufferPacketTranslator(bufferToClient, 1);
        if (sendto(sockfd, &bufferToClient, MAXS, 0, (struct sockaddr *)&destAddr,
                   sizeof(destAddr)) <= -1) {
          perror("Error, unable to send.");
          exit(1);
        }
        pacACK.changeSentBool(1);
        if (!alreadySent) {
          pacACK.initClockTimer(); // only start the timer once
          alreadySent = true;
        }
      }
    }
    if (pacFin.timeOut(1) && !pacFin.retHasBeenAcked() && finCount < 90) {		// idk how many times can retransmit so just chose 90
      pacFin.BufferPacketTranslator(bufferToClient, 1);
      cout << "Sending packet " << pacFin.retSeqNo() << " " << WINDOW << " Retransmission FIN" << endl;
      if (sendto(sockfd, &bufferToClient, MAXS, 0, (struct sockaddr *)&destAddr,
                 sizeof(destAddr)) <= -1){
        perror("Error, unable to send.");
        exit(1);
      }
      pacFin.initClockTimer();
      finCount++;
    } 
    else if ((pacFin.retHasBeenAcked() && pacACK.timeOut(2)) ||
               (pacACK.retHasSent() && pacACK.timeOut(2)) ||
               (finCount >= 90)) {
      //server done closing
      break;
    }
  }

  close(sockfd); //goodbye
  return 0;
}