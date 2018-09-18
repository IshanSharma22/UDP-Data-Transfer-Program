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

#define MAXS 1024 // Packet + Header size

#define MAXSEQNO 30720 //bytes
#define WINDOW 5120

#define CLIENTWINDOW 25 // slidin window to receive packets

using namespace std;

Packet startWin[2];
Packet slidinPacWindow[CLIENTWINDOW];
int initialWindSeq;
int finalWinSeq;
bool hasPacketBeenWritten;
bool ignoreFile;
int port;
uint16_t cliSeqIndex;
long long actualFileSeqIndex;
int finSeqNum;
vector<uint8_t> pacVecWindow;


void startCon(int sockfd, struct sockaddr_in addr, string fileName) {
  Packet p;								// send SYN
  socklen_t sin_size;			
  srand(time(NULL));
  cliSeqIndex = rand() % 10000; // always keep seq and ack numbers moduloed by 30720 to ensure within range

  p.changeSeqNo(cliSeqIndex % MAXSEQNO); //^^
  p.replaceFlagVals(0, 1, 0);
  uint8_t packet[MAXS];

  p.BufferPacketTranslator(packet, 1);
  cout << "Sending packet SYN " << endl;
  if (sendto(sockfd, &packet, MAXS, 0, (struct sockaddr *)&addr, sizeof(addr)) <= -1) {
    perror("Error, unable to send to server");
  	exit(1);
  }

  p.initClockTimer();
  p.changeSentBool(1);
  startWin[0] = p;

  uint8_t buf[MAXS + 1];
  int recieveLen;


  for(;;) {
    recieveLen = recvfrom(sockfd, buf, MAXS, 0 | MSG_DONTWAIT, (struct sockaddr *)&addr, &sin_size); //http://man7.org/linux/man-pages/man2/recvmsg.2.html
    if (recieveLen >= 1) {
      buf[recieveLen] = 0;

      Packet rec;
      rec.BufferPacketTranslator(buf, 0);
      cout << "Receiving packet " << rec.retSeqNo() << endl;
      if (rec.retAckFlag() && rec.retSynFLag() && rec.retAckNo() == startWin[0].retSeqNo()) { // 

        startWin[0].changeAckBool(1);

        if (!startWin[1].retHasSent() ) {	// send ack wih along with file request
          Packet sendFilename;
          sendFilename.fillPayload((uint8_t *)fileName.c_str(), fileName.length());
          sendFilename.replaceFlagVals(1, 0, 0);
          sendFilename.changeAckNo(rec.retSeqNo() % MAXSEQNO);
          cliSeqIndex++;
          sendFilename.changeSeqNo(cliSeqIndex % MAXSEQNO);
          cliSeqIndex++;
          sendFilename.BufferPacketTranslator(packet, 1);
          cout << "Sending packet " << sendFilename.retSeqNo() << endl;
          if (sendto(sockfd, &packet, MAXS, 0, (struct sockaddr *)&addr, sizeof(addr)) <= -1) {
            perror("Error, unable to send to server");
  			exit(1);
          }
          sendFilename.initClockTimer();
          sendFilename.changeSentBool(1);
          startWin[1] = sendFilename;
        } else {
          startWin[1].BufferPacketTranslator(packet, 1); // receiveing syn again so just restart timer
          cout << "Sending packet " << startWin[1].retSeqNo() << endl;
          if (sendto(sockfd, &packet, MAXS, 0, (struct sockaddr *)&addr, sizeof(addr)) <= -1) {
            perror("Error, unable to send to server");
  			exit(1);
          }
          startWin[1].initClockTimer();
        }
      } 
      else if ((rec.retAckFlag() || rec.retFinFlag()) && rec.retAckNo() == startWin[1].retSeqNo()) {
        // Ack for the filename packet
        startWin[1].changeAckBool(1);
        initialWindSeq = rec.retSeqNo() + 1;
        if (rec.retFinFlag()) {
          ignoreFile = true;
          finSeqNum = rec.retSeqNo();
        }
      }

    }

    int counter = 0;
    int i = 0;
    while ( i < 2 ) {	// check startWin to see if need to retrans 
      if (startWin[i].retHasSent() && startWin[i].retHasBeenAcked()) {
        counter++;
      } 
      else if (startWin[i].retHasSent() && startWin[i].timeOut(1)) {
        startWin[i].BufferPacketTranslator(packet, 1);
        cout << "Sending packet ";
        if (!i) {
          cout << "SYN";
        } 
        else {
          cout << startWin[i].retSeqNo();
        }
        cout << " Retransmission " << endl;
        if (sendto(sockfd, &packet, MAXS, 0, (struct sockaddr *)&addr, sizeof(addr)) <= -1) {
          perror("Error, unable to send to server");
  		  exit(1);
        }
        startWin[i].initClockTimer();
      }
      i++;
    }

    if (counter >= 2) { // two packets have been acked chillin
      break;
    }
    memset((char *)&buf, 0, MAXS + 1);
  }
}

void intializeGlobals(){
	initialWindSeq = 0;
	finalWinSeq = 0;

	hasPacketBeenWritten = false;
	ignoreFile = false;

	cliSeqIndex = 0;
	actualFileSeqIndex = 0;
	finSeqNum = 0;
}

void processFileAuxFunc( int ackduplicate, int acknum){
	if (!ackduplicate){
		cout << "Sending packet " << acknum << endl;
	}
	else{
		cout << "Sending packet " << acknum << " Retransmission"<< endl;
	}
}

void processFile(int sockfd, struct sockaddr_in addr) {
  socklen_t sin_size;
  uint8_t buf[MAXS + 1];
  uint8_t payloData[MAXS];
  uint8_t packet[MAXS];
  int recieveLen;
  Packet ack;
  int ackduplicate = 0;
  for(;;) {
    recieveLen = recvfrom(sockfd, buf, MAXS, 0 | MSG_DONTWAIT,
                       (struct sockaddr *)&addr, &sin_size);
    if (recieveLen >= 1) {
      ackduplicate = 0;
      buf[recieveLen] = 0;
      Packet rec;

      rec.BufferPacketTranslator(buf, 0);
      cout << "Receiving packet " << rec.retSeqNo() << endl;
      if (rec.retFinFlag()) {
        int i = 0;
        while ( i < CLIENTWINDOW) {
          if (slidinPacWindow[i].retHasBeenAcked()) {	// write file
            hasPacketBeenWritten = true;
            slidinPacWindow[i].retPayload(payloData);
            int j = 0;
            while ( j < slidinPacWindow[i].retLengVal() ) {
              pacVecWindow.push_back(payloData[j]);
              j++;
            }
          }
          i++;
        }
        finSeqNum = rec.retSeqNo();
        break;
      }

      ack.changeAckNo(rec.retSeqNo() % MAXSEQNO);
      ack.changeSeqNo(cliSeqIndex % MAXSEQNO);
      ack.replaceFlagVals(1, 0, 0);
      ack.BufferPacketTranslator(packet, 1);

      int currSeqNum = rec.retSeqNo();
      int numPacketsToWriteToFile = 0;

      int i = 1;
      while ( i < 6){
        if (currSeqNum == ((finalWinSeq + i * MAXS) % MAXSEQNO)) {
          numPacketsToWriteToFile = i;
          break;
        }
        i++;
      }

      if (!numPacketsToWriteToFile) {
      	int i = 0;
      	while ( i <= (CLIENTWINDOW -1) ){
          if (slidinPacWindow[i].retSeqNo() == rec.retSeqNo() &&
              slidinPacWindow[i].retHasBeenAcked()) {
            ackduplicate = 1;
            break;
          } 
          else if (slidinPacWindow[i].retSeqNo() == rec.retSeqNo() &&
                     !slidinPacWindow[i].retHasBeenAcked()) {
            rec.retPayload(payloData);
            slidinPacWindow[i].fillPayload(payloData, rec.retLengVal());
            slidinPacWindow[i].changeAckBool(1);
            break;
          }
          i++;
        }
      }

      processFileAuxFunc(ackduplicate,ack.retAckNo());

      if (sendto(sockfd, &packet, MAXS, 0, (struct sockaddr *)&addr,
                 sizeof(addr)) <= -1) {
        perror("Error, unable to send to server");
  		exit(1);
      }
      if (!ackduplicate) {

        if (numPacketsToWriteToFile >= 1) {
        	int i =0;
        	while (i < numPacketsToWriteToFile) {
	            if (slidinPacWindow[i].retHasBeenAcked()) {
	              slidinPacWindow[i].retPayload(payloData);
	              hasPacketBeenWritten = true;
	              int j = 0;
	              while ( j < slidinPacWindow[i].retLengVal()) {
	                pacVecWindow.push_back(payloData[j]);
	                j++;
	              }
	            }
            	i++;
          }

          int k = 0;
          while ( k < (CLIENTWINDOW - numPacketsToWriteToFile)) { //adjust window
            slidinPacWindow[k] = slidinPacWindow[k + numPacketsToWriteToFile];
            k++;
          }
          int counter = 1;
          int a = (CLIENTWINDOW - numPacketsToWriteToFile);
          while ( a < CLIENTWINDOW) {	// fix rest of window
            slidinPacWindow[a].changeSeqNo((finalWinSeq + counter * MAXS) %
                                           MAXSEQNO);
            slidinPacWindow[a].changeAckBool(0);
            slidinPacWindow[a].emptyPayload();
            counter++;
            a++;
          }
          
          rec.retPayload(payloData);
          slidinPacWindow[CLIENTWINDOW - 1].fillPayload(payloData, rec.retLengVal());
          slidinPacWindow[CLIENTWINDOW - 1].changeAckBool(1);
          finalWinSeq = slidinPacWindow[CLIENTWINDOW - 1].retSeqNo();
        }
      }
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc < 4 || argc > 4) {
    perror("Incorrect number of arguments, correct usage: ./client localhost [portnumber] [filename] ");
  	exit(1);
  }

  intializeGlobals();

  string hostName(argv[1]);
  string fileName(argv[3]);

  port = atoi(argv[2]);

  int sockfd;
  struct hostent *server;
  struct sockaddr_in addr;

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) <= -1){
  	perror("Error, socket.");
  	exit(1);
  }
  server = gethostbyname(hostName.c_str());

  if (server == NULL){
  	perror("Error, unable to find server");
  	exit(1);
  }

  memset((char *)&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  memcpy((char *)&addr.sin_addr.s_addr, (char *)server->h_addr,
         server->h_length);
  addr.sin_port = htons(port);

  startCon(sockfd, addr, fileName);
  if (ignoreFile==0) {
     int i = 0;
  	 while ( i < CLIENTWINDOW){
     slidinPacWindow[i].changeSeqNo((initialWindSeq + i * MAXS) % MAXSEQNO);
     i++;
  }

  finalWinSeq = slidinPacWindow[CLIENTWINDOW - 1].retSeqNo();
    processFile(sockfd, addr);
  }
  // Start Closing process followed by file assembly
  Packet pacFin;
  Packet pacAck;
  socklen_t sin_size;
  uint8_t buf[MAXS + 1];
  cliSeqIndex++;

  pacAck.changeSeqNo(cliSeqIndex % MAXSEQNO);		
  pacAck.changeAckNo(finSeqNum % MAXSEQNO);
  pacAck.replaceFlagVals(1, 0, 0);
  uint8_t packet[MAXS];
  bool hasBeenReSent = false;
  pacAck.BufferPacketTranslator(packet, 1);
  cout << "Sending packet " << pacAck.retAckNo() << endl;

  if (sendto(sockfd, &packet, MAXS, 0, (struct sockaddr *)&addr, sizeof(addr)) <= -1) {
    perror("Error, unable to send to server");
  	exit(1);
  }
  pacAck.changeSentBool(1);

  cliSeqIndex++;
  pacFin.changeSeqNo(cliSeqIndex % MAXSEQNO);
  pacFin.changeAckNo((finSeqNum + 1) % MAXSEQNO);
  pacFin.replaceFlagVals(0, 0, 1);
  pacFin.BufferPacketTranslator(packet, 1);
  cout << "Sending packet " << pacFin.retAckNo() << " FIN " << endl;

  if (sendto(sockfd, &packet, MAXS, 0, (struct sockaddr *)&addr, sizeof(addr)) <= -1) {
    perror("Error, unable to send to server");
  	exit(1);
  }
  pacFin.changeSentBool(1);
  pacFin.initClockTimer();

  int recieveLen;

  for(;;) {
    recieveLen = recvfrom(sockfd, buf, MAXS, 0 | MSG_DONTWAIT,
                       (struct sockaddr *)&addr, &sin_size);
    if (recieveLen >= 1) {
      buf[recieveLen] = 0;
      Packet recPacket;
      recPacket.BufferPacketTranslator(buf, 0);

      cout << "Receiving packet " << recPacket.retSeqNo() << endl;
      if (recPacket.retFinFlag() && recPacket.retSeqNo() == finSeqNum) {
        pacAck.BufferPacketTranslator(packet, 1)
        ;
        cout << "Sending packet " << pacAck.retAckNo()
             << " Retransmission " << endl;
        if (sendto(sockfd, &packet, MAXS, 0, (struct sockaddr *)&addr,
                   sizeof(addr)) <= -1) {
          perror("Error, unable to send to server");
  		  exit(1);
        }
      } else if (recPacket.retAckFlag() &&
                 recPacket.retAckNo() == pacFin.retSeqNo()) {
        break;
      }
    }
    if (pacFin.timeOut(1) && !pacFin.timeOut(2) &&
        !hasBeenReSent) {
      cout << "Sending packet " << pacFin.retAckNo() << " Retransmission "<< " FIN " << endl;
      if (sendto(sockfd, &packet, MAXS, 0, (struct sockaddr *)&addr,
                 sizeof(addr)) <= -1) {
        perror("Error, unable to send to server");
  		exit(1);
      }
      hasBeenReSent = true;
    } else if (pacFin.timeOut(2) && hasBeenReSent) {
      break;
    }
  }

  // file assembly
  if (hasPacketBeenWritten) {
	  uint8_t *fileBuffer;
	  fileBuffer = new uint8_t[pacVecWindow.size() + 1];
	  unsigned long i = 0;
	  while ( i < pacVecWindow.size() ) {
	    fileBuffer[i] = pacVecWindow[i];
	    i++;
	  }

	  fileBuffer[pacVecWindow.size()] = 0;
	  ofstream outFile;
	  outFile.open("received.data", ios::out | ios::binary);
	  if (outFile.is_open()) {
	    outFile.write((const char *)fileBuffer, (streamsize)(pacVecWindow.size()));
	    outFile.close();
	  }

	  delete fileBuffer;
  } 
  else {
    cout << "Error packet not found."<< endl;
  }
  close(sockfd);
  return 0;
}