#pragma once
#include "sqlite3.h"
#include <string>
#include <filesystem>
#include "global.h"

//Entitys
struct User
{
	long int id = 0;
	std::string nickname;
	std::string login;
	std::string password;
	int boss = 0;
};

struct File
{
	long int id = 0;
	long int user_id = 0;
	std::string name;
	std::string server_name;
	std::string date;
};

struct Task
{
	long int id = 0;
	long int user_id = 0;
	long int admin_id = 0;
	std::string title;
	std::string text;
	long int file_id = 0;
	std::string date;
	bool done = 0;
};

struct SendTask
{
	long int ID = 0;
	long int user_id = 0;
	std::string user_name;
	long int admin_id = 0;
	std::string admin_name;
	std::string title;
	std::string text;
	long int file_id = 0;
	std::string file_name;
	std::string date;
	int done = 0;
};

class SqlClient
{
private:
	sqlite3 *db;

public:
	~SqlClient();
	bool init(const char* path);
	User getUser(char *login, char *password);
	User getUser(long int id);
	bool getAllUsers(std::vector<User> &users);
	File getFile(long int id);
	Task getTask(long int id);
	void getActiveTasks(std::vector<SendTask> &tasks);
	void getActiveUserTasks(long int id, std::vector<SendTask>& tasks);
	void getDoneTasks(std::vector<SendTask>& tasks);
	bool markTaskAsDone(long int id);
	void removeAllDoneTasks();
	void getAllUserTasks(long int id, std::vector<SendTask> &tasks);
	int getActiveTasksCount(long int id);
	bool insertUser(User user);
	bool insertFile(File* file);
	bool insertTask(Task task);
	void escapeChars(std::string* str);
	void escapeChars(char* str);
	bool updateUserNickname(long int id, std::string nickname);
	bool updateUserPassword(long int id, std::string oldPassword, std::string newPassword);
	bool updateUserRights(long int id, int boss);
	long int getLastFileId();
	void clearUnusedFiles();
	/*
	* Возвращает последние 100 (или меньше, в зависимости от длин названий) файлов в виде строки для отправки
	*/
	void getFilesList(std::string &ret_str);
};

