#pragma once
#include "global.h"
#include "sql_client.h"

class FtpClient
{
public:
	File file_to_save_sql;
	unsigned short len = 0;
	unsigned short status = 0;

	char len_buf[2] = "";
	char buf[BUFFER_SIZE] = "";
	char send_buf[BUFFER_SIZE] = "";
	char id[ID_SIZE + 2] = "";
	char fileName[261] = "";

	PacketTask task;
	int offset = 0;
	PacketTask sendTask;
	int sendOffset = 0;
	unsigned short send_len = 0;
	SOCKET sock = 0;
	std::fstream file;
	long int file_id = 0;
	bool isFileOpen = false;
	bool isUpload = false;

	clock_t timestamp = 0;

	FtpClient();
};

