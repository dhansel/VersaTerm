#ifndef XMODEM_H
#define XMODEM_H

bool xmodem_receive(int (*recvChar)(int), 
                    void (*sendData)(const char *data, int len), 
                    bool (*dataHandler)(unsigned long, char*, int));

bool xmodem_transmit(int (*recvChar)(int), 
                     void (*sendData)(const char *data, int len), 
                     bool (*dataHandler)(unsigned long, char*, int));

#endif
