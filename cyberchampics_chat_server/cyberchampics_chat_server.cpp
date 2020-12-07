//##################### Author: Tyshchenko Vladislav ######################
//Standalone version of Cyberchampics Chat server
#pragma warning(disable : 4996)
#pragma warning(disable : 6053)
#pragma warning(disable : 6387)

#include "cyberchampics_chat_server.h"

//TODO: Если пользователь длительное время не присылает свои данные, закрыть соединение?

std::vector<TcpClient*> clients_v;
unsigned int connected_clients_count = 0;

WSADATA wsaData;
std::ifstream exit_file;
bool server_socket_created = false, _ftp_run = true, ftp_server_socket_created = false;
volatile bool _run = true;
volatile bool synchronize_users = false;
char exit_stat_buf[2];

//Переменные броадкаста
Broadcast broadcasts_a[10];
int broadcast_add_ptr = 0;
int broadcast_get_ptr = 0;
//Переменные броадкаста

//Переменные закрытия соединения
ClientStatus closeStatus = ClientStatus::READY;
long int client_id = 0;
//Переменные закрытия соединения
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{    
	//check if files directory exists
	if (!DirectoryExists(FILES_DIR_NAME))
	{
		CreateDirectory(FILES_DIR_NAME, NULL);
	}

	messages = new Message[10];

	//Prepare exit file for working
	std::ofstream f(EXIT_FILE_NAME);
	if (!exit_file.good()) return 2;	//if cannot work with exit file, then exit app EXIT STATUS = 2
	f.clear();
	exit_stat_buf[0] = 0; exit_stat_buf[1] = 0;
	f << exit_stat_buf[0] << exit_stat_buf[1];
	f.close();
	exit_file.open(EXIT_FILE_NAME);
	if (!exit_file.good()) return 2;	//if cannot read exit file, then exit app EXIT STATUS = 2

	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0)
	{
		return 1;
	}
	//Чистим файлы
	clearOldFiles();

	std::thread long_processing_thread(long_processing);
	std::thread ftp_thread(FTP_server_start);
	int return_status = server_start();

	_ftp_run = false;
	ftp_thread.join();
	long_processing_thread.join();
	exit_file.close();
	WSACleanup();
	delete[]messages;
	return return_status;
}


//Функция основных обработчиков сообщений
int long_processing()
{
	SqlClient sql;
	sql.init(DB_NAME);
	while (_run)
	{
		//Если прошло SYNCHRO_FRAME_INTERVAL_SEC секунд отправляем синхронизирующий кадр активных пользователей
		if (synchronize_users) {
			std::lock_guard<std::mutex> g(client_mtx);
			sendSynchroFrame();
			synchronize_users = false;
		}
		//Если нужно отправить броадкаст о получении файла
		if (file_broadcast_status == ClientStatus::NEED_PROCESSING)
		{
			add_file_bradcast(file_broadcast.id, (const char*)user_name_file_broadcast, file_broadcast.name, file_broadcast.date);
			file_broadcast_status = ClientStatus::READY;
		}
		//Если нужно отпривать сообщение о закрытии соединения
		if (closeStatus == ClientStatus::NEED_PROCESSING)
		{
			User user = sql.getUser(client_id);
			std::string str = std::to_string(user.id);
			str += '|' + user.nickname;
			add_broadcast((char*)str.c_str(), (unsigned short)11, str.length());
			sendSynchroFrame();
			closeStatus = ClientStatus::READY;
		}
		//процесс обработки поступивших сообщений
		while (add_pointer != get_pointer)
		{
			processMessages(sql);
			++get_pointer;
			if (get_pointer > 9)
				get_pointer = 0;
		}
		std::unique_lock<std::mutex> lck(mtx);
		cv.wait(lck);
	}
	return 0;
}

int server_start()
{
	clock_t timestamp = clock();
	clock_t exit_check_timestamp = timestamp;
	SOCKET server_socket;
	struct sockaddr_in dest;
	dest.sin_family = AF_INET;
	dest.sin_port = htons((unsigned short)14141U);
	dest.sin_addr.s_addr = INADDR_ANY;

	//Получаем текущую метку времени
	timestamp = std::clock();

	server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (server_socket == INVALID_SOCKET)
	{
		return 1;
	}
	server_socket_created = true;
	if (bind(server_socket, (struct sockaddr*)&dest, sizeof(dest)) < 0)
	{
		return 1;
	}
	if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR)
	{
		return 1;
	}

	fd_set master;
	FD_ZERO(&master);
	FD_SET(server_socket, &master);

	while (_run)
	{
		//check exit status
		if (clock() - exit_check_timestamp > CHECK_EXIT_INTERVAL_MS)
		{
			exit_check_timestamp = clock();
			exit_file.seekg(0);
			exit_file.read(exit_stat_buf, 2);
			if (exit_stat_buf[0] || exit_stat_buf[1])
			{
				_run = false;
				cv.notify_one();
			}
		}
		//Если прошло SYNCHRO_FRAME_INTERVAL_SEC секунд отправляем синхронизирующий кадр активных пользователей
		if ((float(clock() - timestamp) / CLOCKS_PER_SEC) > SYNCHRO_FRAME_INTERVAL_SEC && connected_clients_count > 0)
		{
			synchronize_users = true;
			timestamp = clock();
			cv.notify_one();
		}
		fd_set copy = master;
		int socketCount = select(0, &copy, NULL, NULL, &_timeval);
		for (int socket_n = 0; socket_n < copy.fd_count; socket_n++)
		{
			SOCKET sock = copy.fd_array[socket_n];
			if (sock == server_socket)		//Means that we got new connection
			{
				//Accept connection
				SOCKET client = accept(server_socket, NULL, NULL);
				std::lock_guard<std::mutex> lock(client_mtx);
				//Если клиентов слишко много, отменяем соединение
				if (clients_v.size() > MAX_CLIENTS)
				{
					closesocket(client);
					continue;
				}
				u_long iMode = 0;
				int res = ioctlsocket(client, FIONBIO, &iMode);
				if (res != NO_ERROR)	//Произошла ошибка при попытке перевода сокета в неблокирующий режим
				{
					closesocket(client);
					continue;
				}
				//Add connection to list
				TcpClient* client_struct = new TcpClient;
				client_struct->sock = client;
				clients_v.push_back(client_struct);
				FD_SET(client, &master);
			}
			else		//Received new message or have to process some data by tcp connection
			{
				TcpClient* client = nullptr;
				int tcp_client_pointer;
				for (tcp_client_pointer = 0; tcp_client_pointer < clients_v.size(); tcp_client_pointer++)
				{
					if (clients_v[tcp_client_pointer]->sock == sock)
					{
						client = clients_v[tcp_client_pointer];	//Нашли нужного клиента
						break;
					}
				}

				if (!client)
				{
					CloseConnection(client, &master, tcp_client_pointer);
					continue;
				}
				//Делаем предварительную проверку на истечение срока получения сообщения
				if ((client->task == PacketTask::GetData) && (clock() - client->timestamp > PACKET_WAIT_TIME_MS))	//Если мы уже получаем данные пакета
				{
					//Если прошло достаточно времени, значит скорее всего предыдущий пакет утерян, начинаем принимать данные с нуля
					client->timestamp = clock();
					client->len = 0;
					client->offset = 0;
					client->task = PacketTask::GetPacketSize;
				}
				//Можем получить данные
				int bytes_received = 0;
				if (client->task == PacketTask::GetPacketSize)	//получаем длину пакета
				{
					bytes_received = recv(client->sock, client->len_buf + client->offset, 2 - client->offset, 0);
				}
				else			//Получаем данные
				{
					bytes_received = recv(client->sock, client->buf + client->offset, client->len - client->offset, 0);
				}

				if (bytes_received > 0)	//Если были получены каке-либо данные
				{
					client->offset += bytes_received;
					if (client->task == PacketTask::GetPacketSize)
					{
						if (client->offset == 2)
						{
							client->len = ((unsigned short)(unsigned char)client->len_buf[0] << 8) | (unsigned short)(unsigned char)client->len_buf[1];
							if (client->len > BUFFER_SIZE)
							{
								CloseConnection(client, &master, tcp_client_pointer);
								continue;
							}
							client->timestamp = clock();
							client->offset = 0;
							client->task = PacketTask::GetData;
						}
					}
					else
					{
						if (client->len == client->offset)
						{
							//получили весь пакет
							client->moveToProcessQueue((int*)&add_pointer);

							client->len = 0;
							client->offset = 0;
							client->task = PacketTask::GetPacketSize;
						}
					}
				}
				else			//Получили данных меньше 1
				{
					if (bytes_received == 0)
					{
						CloseConnection(client, &master, tcp_client_pointer);
						continue;
					}
					if (bytes_received == SOCKET_ERROR)
					{
						int error = WSAGetLastError();
						if (error != WSAEWOULDBLOCK)
						{
							CloseConnection(client, &master, tcp_client_pointer);
							continue;
						}
					}
				}
			}
		}

		//Обработка исходящих сообщений
		while (broadcast_get_ptr != broadcast_add_ptr)
		{
			if (broadcasts_a[broadcast_get_ptr].status == ClientStatus::READY_TO_SEND)
			{
				//Начинаем рассылку
				for (int i = 0; i < clients_v.size(); i++)
				{
					if (clients_v[i]->isConnected && clients_v[i] != broadcasts_a[broadcast_get_ptr].except_client)
					{
						sock_write(clients_v[i]->sock, (const char*)broadcasts_a[broadcast_get_ptr].buf, broadcasts_a[broadcast_get_ptr].len, 1000);
					}
				}
				broadcasts_a[broadcast_get_ptr].status = ClientStatus::READY;
			}
			++broadcast_get_ptr;
			if (broadcast_get_ptr > 9)
				broadcast_get_ptr = 0;
		}
		for (int i = 0; i < clients_v.size(); i++)
		{
			if (clients_v[i]->transmission_status == ClientStatus::READY_TO_SEND)
			{
				sock_write(clients_v[i]->sock, (const char*)clients_v[i]->send_buf, clients_v[i]->send_len, 1000);
				clients_v[i]->transmission_status = ClientStatus::READY;
			}
		}
	}
	client_mtx.lock();
	for (int i = 0; i < clients_v.size(); i++)
	{
		closesocket(clients_v[i]->sock);
		delete clients_v[i];
	}
	clients_v.clear();
	client_mtx.unlock();
	if (server_socket_created)
		closesocket(server_socket);
	return 0;
}

int FTP_server_start()
{
	int pending_uploads = 0;
	struct sockaddr_in dest;
	SOCKET ftp_server_socket;
	std::vector<FtpClient*> ftp_clients_v;
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = INADDR_ANY;
	dest.sin_port = htons((unsigned short)14140U);
	ftp_server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (ftp_server_socket == INVALID_SOCKET)
	{
		return 1;
	}
	ftp_server_socket_created = true;
	if (bind(ftp_server_socket, (struct sockaddr*)&dest, sizeof(dest)) < 0)
	{
		return 1;
	}
	if (listen(ftp_server_socket, SOMAXCONN) == SOCKET_ERROR)
	{
		return 1;
	}

	fd_set master;
	FD_ZERO(&master);
	FD_SET(ftp_server_socket, &master);
	while (_ftp_run)
	{
		fd_set copy = master;
		fd_set send_copy = master;
		int socketCount = select(0, &copy, &send_copy, NULL, &_timeval_ftp);
		for (int socket_n = 0; socket_n < copy.fd_count; socket_n++)	//Обрабатываем события
		{
			SOCKET sock = copy.fd_array[socket_n];
			if (sock == ftp_server_socket)	//New connection by ftp
			{
				//Accepting connection
				SOCKET client = accept(ftp_server_socket, NULL, NULL);
				u_long iMode = 0;
				int res = ioctlsocket(client, FIONBIO, &iMode);
				if (res != NO_ERROR)	//Произошла ошибка при попытке перевода сокета в неблокирующий режим
				{
					closesocket(client);
					continue;
				}
				//Add this connection to ftp connection list
				FtpClient* ftp_client = new FtpClient;
				ftp_client->sock = client;
				ftp_clients_v.push_back(ftp_client);
				FD_SET(client, &master);
			}
			else		//Получили новые данные
			{
				FtpClient* ftp_client = nullptr;
				int ftp_client_pointer;
				for (ftp_client_pointer = 0; ftp_client_pointer < ftp_clients_v.size(); ftp_client_pointer++)
				{
					if (ftp_clients_v[ftp_client_pointer]->sock == sock)
					{
						ftp_client = ftp_clients_v[ftp_client_pointer];	//Нашли нужного клиента
						break;
					}
				}

				if (!ftp_client)	//Пользователь не оказался в векторе
				{
					CloseConnection(ftp_client, ftp_clients_v, &master, ftp_client_pointer);
					continue;
				}
				//Делаем предварительную проверку на истечение срока получения сообщения
				if ((ftp_client->task == PacketTask::GetData) && (clock() - ftp_client->timestamp > PACKET_WAIT_TIME_MS))	//Если мы уже получаем данные пакета
				{
					//Если прошло достаточно времени, значит скорее всего предыдущий пакет утерян, начинаем принимать данные с нуля
					ftp_client->timestamp = clock();
					ftp_client->len = 0;
					ftp_client->offset = 0;
					ftp_client->task = PacketTask::GetPacketSize;
				}
				//Можем прочитать данные
				int bytes_received = 0;
				if (ftp_client->task == PacketTask::GetPacketSize)	//Получаем длину пакета
				{
					bytes_received = recv(ftp_client->sock, ftp_client->len_buf + ftp_client->offset, 2 - ftp_client->offset, 0);
				}
				else
				{
					bytes_received = recv(ftp_client->sock, ftp_client->buf + ftp_client->offset, ftp_client->len - ftp_client->offset, 0);
				}

				if (bytes_received > 0)	//Получили некоторое количество данных
				{
					ftp_client->offset += bytes_received;

					if (ftp_client->task == PacketTask::GetPacketSize)
					{
						//Определяем длинну
						if (ftp_client->offset == 2)
						{
							ftp_client->len = ((unsigned short)(unsigned char)ftp_client->len_buf[0] << 8) | (unsigned short)(unsigned char)ftp_client->len_buf[1];
							if (ftp_client->len > BUFFER_SIZE)
							{
								CloseConnection(ftp_client, ftp_clients_v, &master, ftp_client_pointer);
								continue;
							}
							ftp_client->timestamp = clock();
							ftp_client->offset = 0;
							ftp_client->task = PacketTask::GetData;
						}
					}
					else
					{
						if (ftp_client->offset == ftp_client->len)
						{
							//Получили весь пакет данных
							ProcessFtpPacket(ftp_client, &pending_uploads);

							ftp_client->len = 0;
							ftp_client->offset = 0;
							ftp_client->task = PacketTask::GetPacketSize;
						}
					}
				}
				else		//Получили данных меньше 1
				{
					if (bytes_received == 0)
					{
						CloseConnection(ftp_client, ftp_clients_v, &master, ftp_client_pointer);
						continue;
					}
					if (bytes_received == SOCKET_ERROR)
					{
						int error = WSAGetLastError();
						if (error != WSAEWOULDBLOCK)
						{
							CloseConnection(ftp_client, ftp_clients_v, &master, ftp_client_pointer);
							continue;
						}
					}
				}
			}
		}

		//Проверяем нужно ли отправлять данные
		if (pending_uploads < 1)
			continue;
		for (int client_n = 0; client_n < send_copy.fd_count; client_n++) //обработка сокетов отправки
		{
			if (ftp_clients_v[client_n]->isUpload)	//Нашли скачивающего клиента
			{
				if (ftp_clients_v[client_n]->sendTask == PacketTask::GetPacketSize)	//начинаем формировать новый пакет для отправки
				{
					int clients_sent = 0;
					ftp_clients_v[client_n]->send_len = 4002;
					ftp_clients_v[client_n]->send_buf[0] = (unsigned char)(ftp_clients_v[client_n]->send_len >> 8);
					ftp_clients_v[client_n]->send_buf[1] = (unsigned char)ftp_clients_v[client_n]->send_len;
					ftp_clients_v[client_n]->send_buf[2] = 0;
					ftp_clients_v[client_n]->send_buf[3] = 127;
					ftp_clients_v[client_n]->file.read(&ftp_clients_v[client_n]->send_buf[4], 4000);
					ftp_clients_v[client_n]->sendTask = PacketTask::GetData;
					if (ftp_clients_v[client_n]->file.eof())	//Дошли до конца файла - закругляемся
					{
						ftp_clients_v[client_n]->send_len = ftp_clients_v[client_n]->file.gcount() + 2;
						ftp_clients_v[client_n]->send_buf[0] = (unsigned char)(ftp_clients_v[client_n]->send_len >> 8);
						ftp_clients_v[client_n]->send_buf[1] = (unsigned char)ftp_clients_v[client_n]->send_len;
						ftp_clients_v[client_n]->file.close();
						ftp_clients_v[client_n]->isFileOpen = false;
					}
					ftp_clients_v[client_n]->send_len += 2;
				}
				else	//в процессе передачи
				{
					int bytes_sent = send(ftp_clients_v[client_n]->sock, ftp_clients_v[client_n]->send_buf, (ftp_clients_v[client_n]->send_len) - ftp_clients_v[client_n]->sendOffset, 0);
					if (bytes_sent == ftp_clients_v[client_n]->send_len)	//если все отправлено
					{
						ftp_clients_v[client_n]->sendOffset = 0;
						ftp_clients_v[client_n]->sendTask = PacketTask::GetPacketSize;
						if (!ftp_clients_v[client_n]->isFileOpen)
						{
							ftp_clients_v[client_n]->isUpload = false;
							--pending_uploads;
						}
					}
				}
			}
		}
	}
	for (int i = 0; i < ftp_clients_v.size(); i++)
	{
		closesocket(ftp_clients_v[i]->sock);
		delete ftp_clients_v[i];
	}
	ftp_clients_v.clear();
	if (ftp_server_socket_created)
		closesocket(ftp_server_socket);
	return 0;
}

void insert_name(char* buf, const char* name, unsigned short* len)
{
	const unsigned short name_size = strlen(name) + 4;
	int c = strlen(name) + 7;
	for (int i = 62; i < *len; i++)
	{
		buf[c] = buf[i];
		c++;
	}
	//перемещаем статус
	buf[3] = buf[1];
	buf[2] = buf[0];
	c = 0;
	for (int i = 4; i < name_size; i++)
	{
		buf[i] = name[c];
		c++;
	}
	buf[name_size] = ' ';
	buf[name_size + 1] = '>';
	buf[name_size + 2] = ' ';
	*len = *len + strlen(name) + 5 - 60;
	unsigned short in_message_len = *len - 2;
	buf[0] = (unsigned char)(in_message_len >> 8);
	buf[1] = (unsigned char)in_message_len;
}

void add_broadcast(char* buf, unsigned short status, unsigned short len, volatile TcpClient* _except_client)
{
	broadcasts_a[broadcast_add_ptr].status = ClientStatus::NEED_PROCESSING;
	if (len > BUFFER_SIZE - 5) return;
	for (int i = 0; i < len; i++)
	{
		broadcasts_a[broadcast_add_ptr].buf[i + 4] = buf[i];
	}
	len += 2;
	broadcasts_a[broadcast_add_ptr].buf[0] = (char)(len >> 8);
	broadcasts_a[broadcast_add_ptr].buf[1] = (char)len;
	len += 2;
	broadcasts_a[broadcast_add_ptr].buf[2] = (char)(status >> 8);
	broadcasts_a[broadcast_add_ptr].buf[3] = (char)status;
	broadcasts_a[broadcast_add_ptr].len = len;
	broadcasts_a[broadcast_add_ptr].except_client = _except_client;
	broadcasts_a[broadcast_add_ptr].status = ClientStatus::READY_TO_SEND;
	++broadcast_add_ptr;
	if (broadcast_add_ptr > 9)
		broadcast_add_ptr = 0;
}

void add_broadcast(char* buf, unsigned short len, volatile TcpClient* _except_client)
{
	broadcasts_a[broadcast_add_ptr].status = ClientStatus::NEED_PROCESSING;
	if (len > BUFFER_SIZE - 2) return;
	for (int i = 0; i < len; i++)
	{
		broadcasts_a[broadcast_add_ptr].buf[i] = buf[i];
	}
	broadcasts_a[broadcast_add_ptr].len = len;
	broadcasts_a[broadcast_add_ptr].except_client = _except_client;
	broadcasts_a[broadcast_add_ptr].status = ClientStatus::READY_TO_SEND;
	++broadcast_add_ptr;
	if (broadcast_add_ptr > 9)
		broadcast_add_ptr = 0;
}

void add_file_bradcast(long int id, const char* user_name, std::string file_name, std::string date)
{
	broadcasts_a[broadcast_add_ptr].status = ClientStatus::NEED_PROCESSING;
	unsigned short len, status = 115;
	ZeroMemory((char*)broadcasts_a[broadcast_add_ptr].buf, BUFFER_SIZE);
	sprintf((char*)&broadcasts_a[broadcast_add_ptr].buf[4], "%d|%s|%s|%s", id, user_name, file_name.c_str(), date.c_str());
	len = strlen((char*)&broadcasts_a[broadcast_add_ptr].buf[4]) + 2;
	broadcasts_a[broadcast_add_ptr].buf[0] = (char)(len >> 8);
	broadcasts_a[broadcast_add_ptr].buf[1] = (char)len;
	broadcasts_a[broadcast_add_ptr].buf[2] = (char)(status >> 8);
	broadcasts_a[broadcast_add_ptr].buf[3] = (char)status;
	broadcasts_a[broadcast_add_ptr].len = len + 2;
	broadcasts_a[broadcast_add_ptr].status = ClientStatus::READY_TO_SEND;
	++broadcast_add_ptr;
	if (broadcast_add_ptr > 9)
		broadcast_add_ptr = 0;
}

void sendSynchroFrame()
{
	char buf[BUFFER_SIZE];
	unsigned short len = 0;
	TcpClient* client;
	int pointer = 4;
	ZeroMemory(buf, BUFFER_SIZE);
	//формируем массив для отправки
	for (int i = 0; i < clients_v.size(); i++)
	{
		if (!clients_v[i]->isConnected)
			continue;
		if (clients_v[i]->id < 1)
			continue;
		SqlClient sql;
		sql.init(DB_NAME);
		User user = sql.getUser(clients_v[i]->id);	//Получаем данные пользователя
		if (user.id < 1)
			continue;
		//Формирование пакета отправки
		int c = 0;
		std::string str = std::to_string(user.id);
		str += '|' + user.nickname;
		for (c = 0; c < str.length(); c++)
		{
			buf[pointer] = str[c];
			++pointer;
		}
		buf[pointer] = '|';
		++pointer;
	}

	len = pointer - 2;
	buf[0] = (unsigned char)(len >> 8);
	buf[1] = (unsigned char)len;
	buf[2] = 0;
	buf[3] = 15;
	add_broadcast(buf, pointer);
}

void connection_closed(long int id)
{
	while (closeStatus != ClientStatus::READY) {}
	client_id = id;
	closeStatus = ClientStatus::NEED_PROCESSING;
	cv.notify_one();
}

void processFiles(FtpClient* client)
{
	std::string files_str;
	//Отправляем подтверждение
	char buf_affirmation[4] = { 0, 2, 0, 119 };
	sock_write(client->sock, buf_affirmation, 4, 1000);
	//Готовим список файлов
	SqlClient sql;
	sql.init(DB_NAME);
	sql.getFilesList(files_str);
	unsigned short len = files_str.size() - 2;
	files_str[0] = (unsigned char)(len >> 8);
	files_str[1] = (unsigned char)len;
	files_str[2] = 0;
	files_str[3] = 120;
	sock_write(client->sock, files_str.c_str(), len + 2, 1000);
}

void clearOldFiles()
{
	SqlClient sql;
	sql.init(DB_NAME);
	sql.clearUnusedFiles();
}

int sock_write(SOCKET sock, const char* buffer, int size, int mSecTimeout)
{
	int bytes_sent = 0;
	clock_t start = clock();
	while (clock() - start < mSecTimeout)
	{
		bytes_sent += send(sock, buffer, (size - bytes_sent), 0);
		if (bytes_sent == size)
			return 0;
	}
	return -1;
}

void CloseConnection(FtpClient* client, std::vector<FtpClient*>& clients_v, fd_set* set, int client_pointer)
{
	closesocket(client->sock);
	clients_v.erase(clients_v.begin() + client_pointer);
	if (client->isFileOpen)
		client->file.close();
	FD_CLR(client->sock, set);
	delete client;
}

void CloseConnection(TcpClient* client, fd_set* set, int client_pointer)
{
	closesocket(client->sock);
	std::lock_guard<std::mutex> lock_clients(client_mtx);
	clients_v.erase(clients_v.begin() + client_pointer);
	if (client->isConnected)
	{
		--connected_clients_count;
		//Отправляем сообщение другим пользователям
		connection_closed(client->id);
	}
	FD_CLR(client->sock, set);
	delete client;
}

int ProcessFtpPacket(FtpClient* client, int* pending_uploads)
{
	client->status = ((unsigned short)(unsigned char)client->buf[0] << 8) | (unsigned short)(unsigned char)client->buf[1];
	if (client->status == 110)//Начало передачи файла
	{
		if (client->len < 62 || client->len >(500 + 62))
		{
			return -1;
		}

		ZeroMemory(&client->fileName, 261);
		ZeroMemory(&client->id, ID_SIZE + 2);
		strncpy(client->id, &client->buf[2], 60);
		std::string file_name = &client->buf[62];
		long int id = atol(client->id);
		if (id < 1)
			return -1;
		//Нужно найти пользователя и занести все данные в БД
		time_t rawtime;
		struct tm* timeinfo;
		char buffer[80];

		time(&rawtime);
		timeinfo = localtime(&rawtime);
		strftime(buffer, sizeof(buffer), "%d-%m-%Y %H:%M:%S", timeinfo);
		SqlClient sql;
		sql.init(DB_NAME);
		User user = sql.getUser(id);
		if (user.id < 0)
			return -1;
		//Создание уникального имени для файла
		LPCSTR lpPathName = "files";
		LPCSTR lpPrefixString = "fls";
		char name[MAX_PATH];
		unsigned int r = GetTempFileNameA(lpPathName, lpPrefixString, 0, name);
		if (r == 0) {
			return -1;
		}
		client->file_to_save_sql.name = file_name; client->file_to_save_sql.user_id = user.id; client->file_to_save_sql.date = buffer; client->file_to_save_sql.server_name = name;
		strcpy(client->fileName, client->file_to_save_sql.server_name.c_str());
		client->file.open(client->fileName, std::ofstream::out | std::ofstream::binary | std::ofstream::trunc);
		client->isFileOpen = true;
		if (!client->file.good())
		{
			//TODO:Проблемы c файлом
			client->isFileOpen = false;
			return -1;
		}
	}
	else if (client->status == 111)	//Продолжение файла
	{
		if (client->isFileOpen)
		{
			for (unsigned int count = 2; count < client->len; count++)
			{
				client->file << client->buf[count];
			}
		}
	}
	else if (client->status == 112)//конец передачи файла
	{
		if (client->isFileOpen)
		{
			for (unsigned int count = 2; count < client->len; count++)
			{
				client->file << client->buf[count];
			}
			client->file.close();
			//Подготовка данных для формирования массива отправки
			while (file_broadcast_status != ClientStatus::READY) {}
			SqlClient sql;
			sql.init(DB_NAME);
			sql.insertFile(&client->file_to_save_sql);
			User user = sql.getUser(client->file_to_save_sql.user_id);
			file_broadcast = client->file_to_save_sql;
			strcpy((char*)user_name_file_broadcast, user.nickname.c_str());
			file_broadcast_status = ClientStatus::NEED_PROCESSING;
			cv.notify_one();
		}
	}
	else if (client->status == 117)
	{
		long int file_id = atol(&client->buf[2]);
		if (file_id == 0)	//Пользователь запросил список в первый раз
		{
			processFiles(client);
		}
		if (file_id < 1)
			return -1;
		SqlClient sql;
		sql.init(DB_NAME);
		long int last_file_id = sql.getLastFileId();
		sql.~SqlClient();
		if (last_file_id != file_id)	//id не совпадают, необходимо передать новый список
		{
			processFiles(client);
		}
		else		//id совпали передаем сообщение, о том что обновление не нужно
		{
			char buf[4] = { 0, 2, 0, 118 };
			sock_write(client->sock, buf, 4, 1000);
		}
	}
	else if (client->status == 125)
	{
		long int file_id = std::stol(&client->buf[2]);
		if (file_id < 1)
			return -1;
		SqlClient sql;
		sql.init(DB_NAME);
		File file = sql.getFile(file_id);
		if (file.id < 1)
		{
			char out_buf[4] = { 0, 2, 0, 255 };
			sock_write(client->sock, out_buf, 4, 1000);
			return -1;
		}
		else
		{
			std::string buf;
			char str_file_size[16];
			unsigned short len = 0, status = 0;
			unsigned long long int file_size = std::filesystem::file_size(file.server_name);
			buf.reserve(file.name.size() + file_size + 4);
			buf += "0000";
			buf += file.name + '|';
			sprintf(str_file_size, "%llu|", file_size);
			buf += str_file_size;
			len = buf.size() - 2;
			buf[0] = (unsigned char)(len >> 8);
			buf[1] = (unsigned char)len;
			buf[2] = 0;
			buf[3] = 126;
			sock_write(client->sock, buf.c_str(), len + 2, 1000);
			client->file.open(file.server_name, std::ofstream::in | std::ofstream::binary);
			if (!client->file.good())
				return -1;

			client->isFileOpen = true;
			client->isUpload = true;
			++* pending_uploads;
		}
	}
	else
	{
		return -1;
	}
	return 0;
}

void processMessages(SqlClient& sql)
{
	unsigned short status;
	char id_c[ID_SIZE + 1] = "";
	long int id = 0;
	std::lock_guard<std::mutex> lck(messages[get_pointer].mutex);

	//Получаем статус сервера
	status = ((unsigned short)(unsigned char)messages[get_pointer].buf[0] << 8) | (unsigned short)(unsigned char)messages[get_pointer].buf[1];
	if (status > 1)	//Получаем id
	{
		strncpy(id_c, (const char*)&messages[get_pointer].buf[2], ID_SIZE);
	}

	if (status == 1)	//Пользователь передал свои данные
	{
		char login[ID_SIZE] = "";
		char pass[ID_SIZE] = "";
		int c = 0, k = 2;
		for (k = 2; k < ID_SIZE + 2; k++)
		{
			login[c] = messages[get_pointer].buf[k];
			c++;
		}
		c = 0;
		for (; k < ID_SIZE * 2 + 2; k++)
		{
			pass[c] = messages[get_pointer].buf[k];
			c++;
		}
		char out_buf[125] = { 0, 123, 0, 2 };		//Данные были получены
		User user = sql.getUser(login, pass);
		if (user.id > 0)	//нашли пользователя
		{
			++connected_clients_count;
			sprintf(&out_buf[4], "%d", user.id);
			strncpy(&out_buf[64], user.nickname.c_str(), 60);
			if (user.boss > 0)
				out_buf[124] = 1;
			else
				out_buf[124] = 0;
			((TcpClient*)(messages[get_pointer].client))->moveToSendQueue(out_buf, 125);
		}
		else //Если пользователя не нашли
		{
			((TcpClient*)(messages[get_pointer].client))->moveToSendQueue(out_buf, 125);
		}
	}
	else if (status == 5)	//Регистрация пользователя
	{
		char login[ID_SIZE] = "";
		char pass[ID_SIZE] = "";
		char nickname[ID_SIZE] = "";
		int c = 0, k = 2;
		for (k = 2; k < ID_SIZE + 2; k++)
		{
			login[c] = messages[get_pointer].buf[k];
			c++;
		}
		c = 0;
		for (; k < ID_SIZE * 2 + 2; k++)
		{
			pass[c] = messages[get_pointer].buf[k];
			c++;
		}
		c = 0;
		for (; k < ID_SIZE * 3 + 2; k++)
		{
			nickname[c] = messages[get_pointer].buf[k];
			c++;
		}
		char out_buf[5] = { 0, 3, 0, 6, '1' };		//Данные были получены
		User user = { -1, nickname, login, pass, 0 };
		bool ret = sql.insertUser(user);
		if (ret)	//добавили нового пользователя
		{
			((TcpClient*)(messages[get_pointer].client))->moveToSendQueue(out_buf, 5);
		}
		else //Если пользователя не добавили
		{
			out_buf[4] = '0';
			((TcpClient*)(messages[get_pointer].client))->moveToSendQueue(out_buf, 5);
		}

	}
	else if (status == 8)
	{
		id = atol(id_c);
		if (id < 1)
			return;
		messages[get_pointer].client->id = id;
		messages[get_pointer].client->isConnected = true;
		messages[get_pointer].client->transmission_status = ClientStatus::READY;
		User user = sql.getUser(id);
		//Рассылаем сообщение о подключении
		std::string out_str = std::to_string(user.id);
		out_str += "|" + user.nickname;
		add_broadcast((char*)out_str.c_str(), (unsigned short)10, out_str.length(), messages[get_pointer].client);
		sendSynchroFrame();
	}
	else if (status == 12)
	{
		id = atol(id_c);
		if (id < 1)
			return;
		char nickname[ID_SIZE];
		ZeroMemory(nickname, ID_SIZE);
		if (messages[get_pointer].len - 62 > ID_SIZE)
			return;
		strncpy(nickname, (const char*)&messages[get_pointer].buf[62], messages[get_pointer].len - 62);
		sql.updateUserNickname(id, nickname);
		char out_buf[4] = { 0, 2, 0, 100 };		//Message been retranslated
		messages[get_pointer].client->moveToSendQueue(out_buf, 4);
	}
	else if (status == 13)
	{
		id = atol(id_c);
		if (id < 1)
			return;
		std::string oldPass;
		std::string newPass;
		oldPass.reserve(60);
		newPass.reserve(60);
		const char* c1 = strchr((const char*)&messages[get_pointer].buf[62], '|');
		const char* c2 = strchr(c1 + 1, '|');
		if (c1 == nullptr)
			return;
		if (c2 == nullptr)
			return;
		for (int i = 0;; i++)
		{
			if (i > 60)
				return;
			if (&messages[get_pointer].buf[62 + i] == c1)
				break;
			oldPass += messages[get_pointer].buf[62 + i];
		}
		int pos = c1 - messages[get_pointer].buf + 1;
		for (int i = 0;; i++)
		{
			if (i > 60)
				return;
			if (&messages[get_pointer].buf[pos + i] == c2)
				break;
			newPass += messages[get_pointer].buf[pos + i];
		}
		if (sql.updateUserPassword(id, oldPass, newPass))
		{	//Обновление прошло успешно
			char out_buf[4] = { 0, 2, 0, 14 };
			messages[get_pointer].client->moveToSendQueue(out_buf, 4);
		}
		else
		{	//Ошибка обновления
			char out_buf[4] = { 0, 2, 0, 255 };
			messages[get_pointer].client->moveToSendQueue(out_buf, 4);
		}
	}
	else if (status == 20)	//Передача количества активных задач пользователя
	{
		id = atol(id_c);
		if (id < 1)
			return;
		int count = sql.getActiveTasksCount(id);
		char num[16];
		sprintf(num, "%d", count);
		char out_buf[16] = { 0, (char)strlen(num) + 2, 0, 30 };
		strcpy(&out_buf[4], num);
		messages[get_pointer].client->moveToSendQueue(out_buf, strlen(num) + 4);
	}
	else if (status == 25)
	{
		char out_buf[4096];
		id = atol(id_c);
		if (id < 1)
			return;
		User user = sql.getUser(id);
		if (user.id < 1)
			return;
		if (user.boss < 1)
		{
			char out_buf[4] = { 0, 2, 0, 255 };		//error no rights
			messages[get_pointer].client->moveToSendQueue(out_buf, 4);
			return;
		}
		std::vector<User> users;
		sql.getAllUsers(users);
		if (users.empty())
		{
			char out_buf[4] = { 0, 2, 0, 27 };
			messages[get_pointer].client->moveToSendQueue(out_buf, 4);
			return;
		}
		ZeroMemory(out_buf, 4096);
		for (int i = 0; i < users.size(); i++)
		{
			char c_id[16];
			char c_boss[10];
			sprintf(c_id, "%ld", users[i].id);
			sprintf(c_boss, "%d", users[i].boss);
			if (strlen(c_id) + strlen(c_boss) + users[i].nickname.length() + strlen(&out_buf[4]) > 4000)
			{
				int len = strlen(&out_buf[4]) + 2;
				out_buf[0] = (unsigned char)(len >> 8);
				out_buf[1] = (unsigned char)len;
				out_buf[2] = 0;
				out_buf[3] = 26;
				messages[get_pointer].client->moveToSendQueue(out_buf, len + 2);
				ZeroMemory(out_buf, 4096);
			}
			else
			{
				sprintf(&out_buf[strlen(&out_buf[4]) + 4], "%s|%s|%s|", c_id, users[i].nickname.c_str(), c_boss);
				if (i == users.size() - 1)
				{
					int len = strlen(&out_buf[4]) + 2;
					out_buf[0] = (unsigned char)(len >> 8);
					out_buf[1] = (unsigned char)len;
					out_buf[2] = 0;
					out_buf[3] = 26;
					messages[get_pointer].client->moveToSendQueue(out_buf, len + 2);
				}
			}
		}
		out_buf[0] = 0;
		out_buf[1] = 2;
		out_buf[2] = 0;
		out_buf[3] = 27;
		messages[get_pointer].client->moveToSendQueue(out_buf, 4);
	}
	else if (status == 40 || status == 41)
	{
		id = atol(id_c);
		if (id < 1)
			return;
		if (id != 1)
		{
			//Только главный админ имеет на это право
			char out_buf[4] = { 0, 2, 0, 255 };
			messages[get_pointer].client->moveToSendQueue(out_buf, 4);
			return;
		}
		long int user_id = atol((const char*)&messages[get_pointer].buf[62]);
		if (user_id < 2)
			return;
		SqlClient sql;
		sql.init(DB_NAME);
		int rights = 0;
		if (status == 40)
			rights = 1;
		sql.updateUserRights(user_id, rights);
		char out_buf[4] = { 0, 2, 0, 42 };
		messages[get_pointer].client->moveToSendQueue(out_buf, 4);
	}
	else if (status == 50 || status == 53 || status == 23 || status == 21)	//Запрос на получение списка задач
	{
		id = atol(id_c);
		if (id < 1)
			return;
		User user = sql.getUser(id);
		if (user.boss == 0 && (status == 50 || status == 53))		//Если запрос админский (50, 53) - нет прав возвращаем ошибку
		{
			char out_buf[4] = { 0, 2, 0, 255 };
			messages[get_pointer].client->moveToSendQueue(out_buf, 4);
			return;
		}
		long int user_id;
		std::vector<SendTask> tasks;

		if (status == 50)
			sql.getActiveTasks(tasks);
		else if (status == 53)
			sql.getDoneTasks(tasks);
		else if (status == 23)
		{
			user_id = atol((char*)&messages[get_pointer].buf[62]);
			if (user_id < 1)
				return;
			sql.getAllUserTasks(user_id, tasks);
		}
		else if (status == 21)	//Активные задачи текущего пользователя
		{
			sql.getActiveUserTasks(id, tasks);
		}

		if (tasks.empty())
		{
			char out_buf[4] = { 0, 2, 0, 52 };
			messages[get_pointer].client->moveToSendQueue(out_buf, 4);
			return;
		}
		char out_buf[BUFFER_SIZE];
		for (int i = 0; i < tasks.size(); i++)
		{
			ZeroMemory(out_buf, BUFFER_SIZE);
			sprintf(out_buf, "0000%ld|%ld|%s|%ld|%s|%s|%s|%ld|%s|%s|%d", tasks[i].ID, tasks[i].user_id, tasks[i].user_name.c_str(), tasks[i].admin_id, tasks[i].admin_name.c_str(),
				tasks[i].title.c_str(), tasks[i].text.c_str(), tasks[i].file_id, tasks[i].file_name.c_str(), tasks[i].date.c_str(), tasks[i].done);
			unsigned short len = strlen(&out_buf[4]) + 2;
			out_buf[0] = (unsigned char)(len >> 8);
			out_buf[1] = (unsigned char)len;
			out_buf[2] = 0;
			out_buf[3] = 51;
			messages[get_pointer].client->moveToSendQueue(out_buf, len + 2);
		}
		out_buf[0] = 0;
		out_buf[1] = 2;
		out_buf[2] = 0;
		out_buf[3] = 52;
		messages[get_pointer].client->moveToSendQueue(out_buf, 4);
	}
	else if (status == 70)
	{
		id = atol(id_c);
		if (id < 1)
			return;
		long int task_id = atol((char*)&messages[get_pointer].buf[62]);
		if (task_id < 1)
			return;
		User user = sql.getUser(id);
		Task task = sql.getTask(task_id);
		if (task.user_id != user.id)
		{
			if (user.boss < 1)
			{
				char out_buf[4] = { 0, 2, 0, 255 };
				messages[get_pointer].client->moveToSendQueue(out_buf, 4);
				return;
			}
		}
		if (!sql.markTaskAsDone(task.id))
			return;
		char out_buf[20];
		sprintf(out_buf, "0000%ld", task_id);
		unsigned short len = strlen(&out_buf[4]) + 2;
		out_buf[0] = (unsigned char)(len >> 8);
		out_buf[1] = (unsigned char)len;
		out_buf[2] = 0;
		out_buf[3] = 71;
		messages[get_pointer].client->moveToSendQueue(out_buf, len + 2);
	}
	else if (status == 75)
	{
		id = atol(id_c);
		if (id < 1)
			return;
		User user = sql.getUser(id);
		if (user.id < 1)
			return;
		if (user.boss < 1)
		{
			char out_buf[4] = { 0, 2, 0, 255 };
			messages[get_pointer].client->moveToSendQueue(out_buf, 4);
		}
		sql.removeAllDoneTasks();
		char out_buf[4] = { 0, 2, 0, 76 };
		messages[get_pointer].client->moveToSendQueue(out_buf, 4);
	}
	else if (status == 80)
	{
		id = atol(id_c);
		if (id < 1)
			return;
		const char* c1 = strchr((char*)&messages[get_pointer].buf[62], '|');
		if (c1 == nullptr)
			nullptr;
		const char* c2 = strchr(c1 + 1, '|');
		if (c2 == nullptr)
			return;
		const char* c3 = strchr(c2 + 1, '|');
		if (c3 == nullptr)
			return;
		char user_id[16];
		char title[256];
		char text[2001];
		char file_id[16];
		ZeroMemory(user_id, 16);
		ZeroMemory(title, 256);
		ZeroMemory(text, 2001);
		ZeroMemory(file_id, 16);
		//Копируем нужные нам строки, предварительно убеждаясь, что все граничные условия соблюдены
		if (c1 + 1 - &messages[get_pointer].buf[62] > 15)
			return;
		if (c2 + 1 - c1 > 255)
			return;
		if (c3 + 1 - c2 > 2000)
			return;
		if (strlen(c3 + 1) > 15)
			return;
		strncpy(user_id, (char*)&messages[get_pointer].buf[62], c1 - &messages[get_pointer].buf[62]);
		strncpy(title, c1 + 1, c2 - c1 - 1);
		strncpy(text, c2 + 1, c3 - c2 - 1);
		strcpy(file_id, c3 + 1);
		long int l_user_id = atol(user_id);		//Находим нужные величины в формате long и убеждаемся что они верны
		long int l_file_id = atol(file_id);
		if (l_user_id < 1)
			return;
		if (l_file_id < 0)
			return;
		Task task;
		task.user_id = l_user_id; task.admin_id = id; task.title = title; task.text = text; task.file_id = l_file_id;
		//Получаем строку времени
		time_t rawtime;
		struct tm* timeinfo;
		char time_buf[30];
		time(&rawtime);
		timeinfo = localtime(&rawtime);
		strftime(time_buf, sizeof(time_buf), "%d-%m-%Y %H:%M:%S", timeinfo);
		task.date = time_buf;
		task.done = 0;
		bool result = sql.insertTask(task);
		if (!result)
		{
			char out_buf[4] = { 0, 2, 0, 255 };		//Error
			messages[get_pointer].client->moveToSendQueue(out_buf, 4);
			return;
		}
		char out_buf[4] = { 0, 2, 0, 81 };		//Success
		messages[get_pointer].client->moveToSendQueue(out_buf, 4);

		std::lock_guard<std::mutex> lck(client_mtx);
		for (int i = 0; i < clients_v.size(); i++)
		{
			if (l_user_id == clients_v[i]->id)
			{
				char out_buf[4] = { 0, 2, 0, 82 };		//user got new task
				clients_v[i]->moveToSendQueue(out_buf, 4);
				break;
			}
		}
	}
	else if (status == 101)			//Пользователь передал сообщение
	{
		id = atol(id_c);
		if (id < 1)
			return;
		messages[get_pointer].client->id = id;
		messages[get_pointer].client->isConnected = true;
		User user = sql.getUser(id);
		insert_name((char*)(messages[get_pointer].buf), user.nickname.c_str(), (unsigned short*)&messages[get_pointer].len);
		add_broadcast((char*)messages[get_pointer].buf, messages[get_pointer].len, messages[get_pointer].client);
		char out_buf[4] = { 0, 2, 0, 100 };		//Message been retranslated
		messages[get_pointer].client->moveToSendQueue(out_buf, 4);
	}
}

//Функция очищает содержимое папки
void clear_folder(const char* folder)
{
	std::string path(folder);
	for (const auto& entry : std::filesystem::directory_iterator(path))
	{
		std::filesystem::remove_all(entry.path());
	}
}

inline bool DirectoryExists(LPCTSTR szPath)
{
	DWORD dwAttrib = GetFileAttributes(szPath);

	return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
		(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

inline bool FileExists(const char* name) {
	struct stat buffer;
	return (stat(name, &buffer) == 0);
}


