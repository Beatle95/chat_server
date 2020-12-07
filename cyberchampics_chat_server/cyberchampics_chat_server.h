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
	Функция сервера чата
*/
int server_start();
/*
	Функция сервера передачи файлов
*/
int FTP_server_start();
/*
	Функция обработки пришедших сообщений
*/
int long_processing();

inline bool DirectoryExists(LPCTSTR szPath);

//не используется, но может пригодиться######
inline bool FileExists(const char* name);
void clear_folder(const char* folder);
//не используется, но может пригодиться######

void insert_name(char* buf, const char* name, unsigned short* len);
/*
* Добавляет в очередь отправки широковещательный пакет, len указывается без учета байта длины и байта статуса
* Если except_client == nullptr, пакет отправляется всем пользователям, иначе - всем, кроме указанного пользователя
*/
void add_broadcast(char* buf, unsigned short status, unsigned short len, volatile TcpClient* _except_client = nullptr);
/*
* Добавляет в очередь отправки широковещательный пакет, здесь len уже указывает на общую длину пакета
* Если except_client == nullptr, пакет отправляется всем пользователям, иначе - всем, кроме указанного пользователя
*/
void add_broadcast(char* buf, unsigned short len, volatile TcpClient* _except_client = nullptr);
void sendSynchroFrame();
/*
	Броадкаст о получении файла
*/
void add_file_bradcast(long int id, const char* user_name, std::string file_name, std::string date);

//Функции для работы с буфером обработки сообщений
void processMessages(SqlClient& sql);

/*
	добавляет событие закрытия соединения в пул обработчика событий
*/
void connection_closed(long int id);

/*
	Функция получения коллекции файлов и отправки ее клиенту
*/
void processFiles(FtpClient* client);

/*
	Очистка базы от старых файлов
*/
void clearOldFiles();

/*
	Пытается отправить данные через указанный неблокирующий сокет в течении указанного интервала времени
	При успехе возвращает 0
*/
int sock_write(SOCKET sock, const char* buffer, int size, int mSecTimeout);


/*
	Закрывает соединение с указанным ftp клиентом
*/
void CloseConnection(FtpClient* client, std::vector<FtpClient*>& clients_v, fd_set* set, int client_pointer);

/*
	Закрывает соединение с указанным tcp клиентом
*/
void CloseConnection(TcpClient* client, fd_set* set, int client_pointer);

/*
	Обработка полученного ftp пакета
*/
int ProcessFtpPacket(FtpClient* client, int* pending_uploads);

#endif // !MAIN
