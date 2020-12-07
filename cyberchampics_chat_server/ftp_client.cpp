#include "ftp_client.h"

FtpClient::FtpClient() {
	isFileOpen = false;
	isUpload = false;
	offset = 0;
	task = PacketTask::GetPacketSize;
	sendTask = PacketTask::GetPacketSize;
	send_len = 0;
}
