// This code was taken and adapted from: https://github.com/mgk/arduino-xmodem
// (https://code.google.com/archive/p/arduino-xmodem)
// which was released under GPL V3:
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
// -----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "terminal.h"
#include "xmodem.h"

typedef enum {
  Crc,
  ChkSum	
} transfer_t;

static const unsigned char NACK = 21;
static const unsigned char ACK =  6;

static const unsigned char SOH =  1;
static const unsigned char EOT =  4;
static const unsigned char CAN =  0x18;

static const int receiveDelay=7000;
static const int rcvRetryLimit = 10;

//delay when receive bytes in frame - 7 secs
static const int receiveDelay;

//retry limit when receiving
static const int rcvRetryLimit;

//holds readed byte (due to dataAvail())
static int byte;

//expected block number
static unsigned char blockNo;

//extended block number, send to dataHandler()
static unsigned long blockNoExt;

//retry counter for NACK
static int retries;

//buffer
static char buffer[133];

//repeated block flag
static bool repeatedBlock, canceled;


static int  (*recvChar)(int);
static void (*sendData)(const char *data, int len);
static bool (*dataHandler)(unsigned long number, char *buffer, int len);


static bool dataAvail(int delay)
{
  if (byte != -1)
    return true;
  else 
    {
      byte = recvChar(delay);
      if( byte==-2 ) canceled = true;
      return byte>=0;
    }
}


static int dataRead(int delay)
{
  int b;
  if(byte != -1)
    {
      b = byte;
      byte = -1;
    }
  else
    {
      b = recvChar(delay);
      if( b==-2 ) { canceled = true; b = -1; }
    }
  
  return b;
}


static void dataWrite(char symbol)
{
  sendData(&symbol, 1);
}


static bool receiveFrameNo()
{
  unsigned char num = 
    (unsigned char)dataRead(receiveDelay);
  unsigned char invnum = 
    (unsigned char)dataRead(receiveDelay);
  repeatedBlock = false;
  //check for repeated block
  if (invnum == (255-num) && num == blockNo-1) {
    repeatedBlock = true;
    return true;	
  }
  
  if(num !=  blockNo || invnum != (255-num))
    return false;
  else
    return true;
}


static bool receiveData()
{
  for(int i = 0; i < 128; i++) {
    int byte = dataRead(receiveDelay);
    if(byte != -1)
      buffer[i] = (unsigned char)byte;
    else
      return false;
  }
  return true;	
}


static unsigned short crc16_ccitt(char *buf, int size)
{
  unsigned short crc = 0;
  while (--size >= 0) {
    int i;
    crc ^= (unsigned short) *buf++ << 8;
    for (i = 0; i < 8; i++)
      if (crc & 0x8000)
        crc = crc << 1 ^ 0x1021;
      else
        crc <<= 1;
  }
  return crc;
}


static bool checkCrc()
{
  unsigned short frame_crc = ((unsigned char) dataRead(receiveDelay)) << 8;

  frame_crc |= (unsigned char)dataRead(receiveDelay);
  unsigned short crc = crc16_ccitt(buffer, 128);

  return frame_crc == crc;
}


static bool checkChkSum()
{
  unsigned char frame_chksum = (unsigned char) dataRead(receiveDelay);

  unsigned char chksum = 0;
  for(int i = 0; i< 128; i++)
    chksum += buffer[i];

  return frame_chksum == chksum;
}


static bool sendNack()
{
  dataWrite(NACK);	
  retries++;
  return retries < rcvRetryLimit;
}


static bool receiveFrames(transfer_t transfer)
{
  blockNo = 1;
  blockNoExt = 1;
  retries = 0;
  while( !canceled ) 
    {
      char cmd = dataRead(1000);
      switch(cmd)
        {
        case SOH:
          if (!receiveFrameNo()) 
            {
              if (sendNack())
                break;
              else
                return false;
            }
          if (!receiveData()) 
            {	
              if (sendNack())
                break;
              else
                return false;
            };
          if (transfer == Crc) {
            if (!checkCrc()) {
              if (sendNack())
                break;
              else
                return false;
            }
          } else {
            if(!checkChkSum()) {
              if (sendNack())
                break;
              else
                return false;
            }
          }
          //callback
          if(dataHandler != NULL && 
             repeatedBlock == false)
            if(!dataHandler(blockNoExt, buffer, 128)) {
              return false;
            }
          //ack
          dataWrite(ACK);
          if(repeatedBlock == false)
            {
              blockNo++;
              blockNoExt++;
            }
          retries = 0;
          break;
        case EOT:
          dataWrite(ACK);
          return true;
        case CAN:
          //wait second CAN
          if( dataRead(receiveDelay) == CAN) {
            dataWrite(ACK);
            //flushInput();
            return false;
          }
          //something wrong
          dataWrite(CAN);
          dataWrite(CAN);
          dataWrite(CAN);
          return false;
        default:
          //something wrong
          dataWrite(CAN);
          dataWrite(CAN);
          dataWrite(CAN);
          return false;
        }
    }

  return false;
}


static void init()
{
  //set preread byte  	
  byte = -1;
}


static unsigned char generateChkSum(const char *buf, int len)
{
  //calculate chksum
  unsigned char chksum = 0;
  for(int i = 0; i< len; i++) {
    chksum += buf[i];
  }
  return chksum;
}


bool transmitFrames(transfer_t transfer)
{
  blockNo = 1;
  blockNoExt = 1;
  // use this only in unit tetsing
  //memset(buffer, 'A', 128);
  while( !canceled )
    {
      //get data
      if (dataHandler != NULL)
        {
          if( !dataHandler(blockNoExt, buffer+3, 128) )
            {
              //end of transfer
              dataWrite(EOT);
              //wait ACK
              return (dataRead(receiveDelay) == ACK);
            }			
          
        }
      else
        {
          //cancel transfer - send CAN twice
          dataWrite(CAN);
          dataWrite(CAN);
          //wait ACK
          return (dataRead(receiveDelay) == ACK);
        }
      //SOH
      buffer[0] = SOH;
      //frame number
      buffer[1] = blockNo;
      //inv frame number
      buffer[2] = (unsigned char)(255-(blockNo));
      //(data is already in buffer starting at byte 3)
      //checksum or crc
      if (transfer == ChkSum) {
        buffer[3+128] = generateChkSum(buffer+3, 128);
        sendData(buffer, 3+128+1);
      } else {
        unsigned short crc;
        crc = crc16_ccitt(buffer+3, 128);
        buffer[3+128+0] = (unsigned char)(crc >> 8);
        buffer[3+128+1] = (unsigned char)(crc);;
        sendData(buffer, 3+128+2);
      }

      //TO DO - wait NACK or CAN or ACK
      int ret = dataRead(receiveDelay);
      switch(ret)
        {
        case ACK: //data is ok - go to next chunk
          blockNo++;
          blockNoExt++;
          continue;
        case NACK: //resend data
          continue;
        case CAN: //abort transmision
          return false;
        }
	
    }

  return false;
}


bool xmodem_receive(int (*recvCharFn)(int), 
                    void (*sendDataFn)(const char *data, int len), 
                    bool (*dataHandlerFn)(unsigned long, char*, int))
{
  init();

  sendData = sendDataFn;
  recvChar = recvCharFn;
  dataHandler = dataHandlerFn;
  
  canceled = false;
  for (int i =0; (i <  128) && !canceled; i++)
    {
      dataWrite('C');	
      if (dataAvail(1000)) 
        return receiveFrames(Crc);
      
    }
  for (int i =0; (i <  128) && !canceled; i++)
    {
      dataWrite(NACK);	
      if (dataAvail(1000)) 
        return receiveFrames(ChkSum);
    }
  return false;
}


bool xmodem_transmit(int (*recvCharFn)(int), 
                     void (*sendDataFn)(const char *data, int len), 
                     bool (*dataHandlerFn)(unsigned long, char*, int))
{
  int retry = 0;
  int sym;
  init();

  sendData = sendDataFn;
  recvChar = recvCharFn;
  dataHandler = dataHandlerFn;
  
  //wait for CRC transfer
  canceled = false;
  while( (retry < 256) && !canceled )
    {
      if(dataAvail(1000))
        {
          sym = dataRead(1); //data is here - no delay
          if(sym == 'C')	
            return transmitFrames(Crc);
          if(sym == NACK)
            return transmitFrames(ChkSum);
        }
      retry++;
    }	
  return false;
}
