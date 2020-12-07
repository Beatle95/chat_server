#pragma once
#ifndef TCP_CLIENT
#define TCP_CLIENT

#include "global.h"

//GLOBALS
extern std::condition_variable cv;
extern std::mutex mtx;
extern Message* messages;

volatile class TcpClient
{
public:
	unsigned short len;
	volatile ClientStatus transmission_status = ClientStatus::READY;
	PacketTask task;
	SOCKET sock = 0;
	int offset = 0;
	long int id = 0;
	bool isConnected = false;
	char buf[BUFFER_SIZE] = "";
	char len_buf[2] = "";
	clock_t timestamp = 0;

	volatile unsigned short send_len = 0;
	volatile char send_buf[BUFFER_SIZE] = "";

	TcpClient();
	void moveToProcessQueue(int* add_pointer) volatile;
	bool moveToSendQueue(char* buf, unsigned short len) volatile;
};


#endif // !TCP_CLIENT
