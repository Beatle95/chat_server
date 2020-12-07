#include "tcp_client.h"

TcpClient::TcpClient()
{
	task = PacketTask::GetPacketSize;
	transmission_status = ClientStatus::READY;
	isConnected = false;
	offset = 0;
	len = 0;
	send_len = 0;
	id = 0;
	timestamp = 0;
}

void TcpClient::moveToProcessQueue(int* add_pointer) volatile
{
	if (transmission_status != ClientStatus::READY)
		return;
	std::lock_guard<std::mutex> lock(messages[*add_pointer].mutex);
	for (int i = 0; i < BUFFER_SIZE; i++)
	{
		messages[*add_pointer].buf[i] = buf[i];
	}
	messages[*add_pointer].len = len;
	messages[*add_pointer].client = this;
	transmission_status = ClientStatus::NEED_PROCESSING;
	++*add_pointer;
	if (*add_pointer > 9) *add_pointer = 0;
	cv.notify_one();
}

bool TcpClient::moveToSendQueue(char *buf, unsigned short len) volatile
{
	clock_t start = clock();
	while (transmission_status == ClientStatus::READY_TO_SEND)
	{
		if ((clock() - start) / CLOCKS_PER_SEC > 3)
			return false;
	}
	if (len > BUFFER_SIZE)
	{
		transmission_status = ClientStatus::READY;
		return false;
	}
	for (int i = 0; i < len; i++)
	{
		send_buf[i] = buf[i];
	}
	send_len = len;
	transmission_status = ClientStatus::READY_TO_SEND;
	return true;
}
