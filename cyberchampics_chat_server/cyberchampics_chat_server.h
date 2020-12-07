#pragma once
#ifndef MAIN
#define MAIN

#include "global.h"
#include "sql_client.h"
#include "tcp_client.h"
#include "ftp_client.h"


//CONSTANTS
const std::string EXIT_FILE_NAME = "exit";
LPCTSTR FILES_DIR_NAME = L"files";
const char DB_NAME[] = "database.db";
const timeval _timeval = { 0L, 100L };
const timeval _timeval_ftp = { 0L, 20L };

//GLOBALS
std::condition_variable cv;
std::mutex mtx;
volatile int add_pointer = 0, get_pointer = 0;
Message* messages;
std::mutex client_mtx;
//Variables for file broadcst
File file_broadcast;
volatile char user_name_file_broadcast[ID_SIZE];
volatile ClientStatus file_broadcast_status = ClientStatus::READY;

/*
	������� ������� ����
*/
int server_start();
/*
	������� ������� �������� ������
*/
int FTP_server_start();
/*
	������� ��������� ��������� ���������
*/
int long_processing();

inline bool DirectoryExists(LPCTSTR szPath);

//�� ������������, �� ����� �����������######
inline bool FileExists(const char* name);
void clear_folder(const char* folder);
//�� ������������, �� ����� �����������######

void insert_name(char* buf, const char* name, unsigned short* len);
/*
* ��������� � ������� �������� ����������������� �����, len ����������� ��� ����� ����� ����� � ����� �������
* ���� except_client == nullptr, ����� ������������ ���� �������������, ����� - ����, ����� ���������� ������������
*/
void add_broadcast(char* buf, unsigned short status, unsigned short len, volatile TcpClient* _except_client = nullptr);
/*
* ��������� � ������� �������� ����������������� �����, ����� len ��� ��������� �� ����� ����� ������
* ���� except_client == nullptr, ����� ������������ ���� �������������, ����� - ����, ����� ���������� ������������
*/
void add_broadcast(char* buf, unsigned short len, volatile TcpClient* _except_client = nullptr);
void sendSynchroFrame();
/*
	��������� � ��������� �����
*/
void add_file_bradcast(long int id, const char* user_name, std::string file_name, std::string date);

//������� ��� ������ � ������� ��������� ���������
void processMessages(SqlClient& sql);

/*
	��������� ������� �������� ���������� � ��� ����������� �������
*/
void connection_closed(long int id);

/*
	������� ��������� ��������� ������ � �������� �� �������
*/
void processFiles(FtpClient* client);

/*
	������� ���� �� ������ ������
*/
void clearOldFiles();

/*
	�������� ��������� ������ ����� ��������� ������������� ����� � ������� ���������� ��������� �������
	��� ������ ���������� 0
*/
int sock_write(SOCKET sock, const char* buffer, int size, int mSecTimeout);


/*
	��������� ���������� � ��������� ftp ��������
*/
void CloseConnection(FtpClient* client, std::vector<FtpClient*>& clients_v, fd_set* set, int client_pointer);

/*
	��������� ���������� � ��������� tcp ��������
*/
void CloseConnection(TcpClient* client, fd_set* set, int client_pointer);

/*
	��������� ����������� ftp ������
*/
int ProcessFtpPacket(FtpClient* client, int* pending_uploads);

#endif // !MAIN
