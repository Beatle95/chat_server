#pragma once
#ifndef GLOBAL
#define GLOBAL

#define FD_SETSIZE					65
#define SYNCHRO_FRAME_INTERVAL_SEC	60
#define CHECK_EXIT_INTERVAL_MS		1000
#define MAX_CLIENTS					65
#define FILL_CHARACTER				22
#define PACKET_WAIT_TIME_MS			6000	//6 sec

#include <windows.h>
#include <time.h>
#include <ctime>
#include <fileapi.h>
#include <stdio.h>
#include <string>
#include <fstream>
#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>

const int ID_SIZE = 60;
const int BUFFER_SIZE = 4096;

class TcpClient;

//Перечисления
enum class ClientStatus { READY, NEED_PROCESSING, READY_TO_SEND, SENDING };
enum class PacketTask { GetPacketSize, GetData };

struct Message
{
	std::mutex mutex;
	volatile TcpClient* client = nullptr;
	volatile char buf[BUFFER_SIZE] = "";
	volatile unsigned short len = 0;
};

struct Broadcast
{
	ClientStatus status = ClientStatus::READY;
	volatile char buf[BUFFER_SIZE] = "";
	volatile unsigned short len = 0;
	volatile TcpClient* except_client;
};

#endif
