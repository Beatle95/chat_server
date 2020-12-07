#pragma warning(disable : 4996)
#include "sql_client.h"

bool SqlClient::init(const char *path)
{
	bool users = false, tasks = false, files = false;
	std::string str;
	sqlite3_stmt* stmt;
	sqlite3_open(path, &db);
	
	if (db == NULL)
		return false;

	sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'", -1, &stmt, NULL);

	int num_cols = sqlite3_column_count(stmt);
	while (true)		//Проверяем структуру БД
	{
		int result = sqlite3_step(stmt);
		if (result == SQLITE_DONE || result == SQLITE_MISUSE)
			break;

		for (int i = 0; i < num_cols; i++)
		{
			switch (sqlite3_column_type(stmt, i))
			{
			case (SQLITE3_TEXT):
				str = (char*)sqlite3_column_text(stmt, i);
				if (str == "files")
					files = true;
				else if (str == "tasks")
					tasks = true;
				else if (str == "users")
					users = true;
				break;
			default:
				break;
			}
		}
	}
	if (!users || !tasks || !files)	//База повреждена или неверна, удалить и создать новую
	{
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		std::filesystem::remove(path);
		sqlite3_open(path, &db);
		if (db == NULL)
			return false;

		sqlite3_prepare_v2(db, "CREATE TABLE users(id INTEGER PRIMARY KEY, nickname TEXT NOT NULL, login TEXT NOT NULL UNIQUE, password TEXT NOT NULL, boss INTEGER DEFAULT 0);", -1, &stmt, NULL);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		sqlite3_prepare_v2(db, "CREATE TABLE files(id INTEGER PRIMARY KEY, user_id INTEGER NOT NULL, name TEXT NOT NULL, server_name TEXT, date TEXT NOT NULL);", -1, &stmt, NULL);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		sqlite3_prepare_v2(db, "CREATE TABLE tasks(id INTEGER PRIMARY KEY, user_id INTEGER NOT NULL, admin_id INTEGER NOT NULL, title TEXT, text TEXT, file_id INTEGER DEFAULT 0, date TEXT NOT NULL, done INTEGER DEFAULT 0);", -1, &stmt, NULL);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);

		User user = { -1, "admin", "admin", "admin", 1 };
		insertUser(user);
	}
	else 
	{
		sqlite3_finalize(stmt);
	}
	sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
	return true;
}

bool SqlClient::insertUser(User user)
{
	char sql[1024];
	char *err = 0;
	escapeChars(&user.nickname);
	escapeChars(&user.login);
	escapeChars(&user.password);
	sprintf(sql, "INSERT INTO users VALUES(NULL, '%s', '%s', '%s', %d);", user.nickname.c_str(), user.login.c_str(), user.password.c_str(), user.boss);
	sqlite3_exec(db, sql, 0, 0, &err);
	if (err != NULL)
	{
		return false;
	}
	return true;
}

bool SqlClient::insertFile(File* file)
{
	sqlite3_stmt* stmt;
	char sql[1024];
	char* err = 0;
	escapeChars(&file->name);
	escapeChars(&file->server_name);
	escapeChars(&file->date);
	sprintf(sql, "INSERT INTO files VALUES(NULL, '%d', '%s', '%s', '%s');", file->user_id, file->name.c_str(), file->server_name.c_str(), file->date.c_str());
	sqlite3_exec(db, sql, 0, 0, &err);
	//Добываем ID последней добавленной записи
	sqlite3_prepare_v2(db, "SELECT last_insert_rowid();", -1, &stmt, NULL);
	sqlite3_step(stmt);
	file->id = sqlite3_column_int64(stmt, 0);
	sqlite3_finalize(stmt);
	if (err != NULL)
	{
		return false;
	}
	return true;
}

bool SqlClient::insertTask(Task task)
{
	char sql[1024];
	char* err = 0;
	escapeChars(&task.text);
	if(task.file_id == 0)
		sprintf(sql, "INSERT INTO tasks VALUES(NULL, %d, %d, '%s', '%s', NULL, '%s', %d);", task.user_id, task.admin_id, task.title.c_str(), task.text.c_str(), task.date.c_str(), task.done);
	else
		sprintf(sql, "INSERT INTO tasks VALUES(NULL, %d, %d, '%s', '%s', %ld, '%s', %d);", task.user_id, task.admin_id, task.title.c_str(), task.text.c_str(), task.file_id, task.date.c_str(), task.done);
	sqlite3_exec(db, sql, 0, 0, &err);
	if (err != NULL)
	{
		return false;
	}
	return true;
}

void SqlClient::escapeChars(std::string* str)
{
	std::string new_str = "";
	bool needChanges = false;
	char escapeSymb[3] = { '\'', '|', ',' };

	for (int i = 0; i < str->size(); i++)
	{
		bool curCharOk = true;
		for (int j = 0; j < 3; j++)
		{
			if (str->c_str()[i] == escapeSymb[j])
			{
				curCharOk = false;
				needChanges = true;
				break;
			}
		}
		if(curCharOk)
			new_str += str->c_str()[i];
	}

	if (needChanges)
	{
		*str = new_str;
	}
}

void SqlClient::escapeChars(char* str)
{
	std::string new_str = "";
	bool needChanges = false;
	char escapeSymb[3] = { '\'', '|', ',' };

	for (int i = 0; i < strlen(str); i++)
	{
		bool curCharOk = true;
		for (int j = 0; j < 3; j++)
		{
			if (str[i] == escapeSymb[j])
			{
				curCharOk = false;
				needChanges = true;
				break;
			}
		}
		if (curCharOk)
			new_str += str[i];
	}

	if (needChanges)
	{
		memset(str, 0, strlen(str));
		strcpy(str, new_str.c_str());
	}
}

bool SqlClient::updateUserNickname(long int id, std::string nickname)
{
	char sql[1024];
	char* err = 0;
	escapeChars(&nickname);
	sprintf(sql, "UPDATE users SET nickname = '%s' WHERE id = %ld", nickname.c_str(), id);
	sqlite3_exec(db, sql, 0, 0, &err);
	return true;
}

bool SqlClient::updateUserPassword(long int id, std::string oldPassword, std::string newPassword)
{
	sqlite3_stmt* stmt;
	char sql[1024];
	char password[61];
	char* err = 0;
	escapeChars(&oldPassword);
	escapeChars(&newPassword);
	sprintf(sql, "SELECT password FROM users WHERE id = %ld", id);
	sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	int result = sqlite3_step(stmt);
	if (result != SQLITE_ROW)
		return false;
	strcpy(password, (char*)sqlite3_column_text(stmt, 0));
	sqlite3_finalize(stmt);
	if (strcmp(password, oldPassword.c_str()) != 0)
		return false;
	sprintf(sql, "UPDATE users SET password = '%s' WHERE id = %ld", newPassword.c_str(), id);
	sqlite3_exec(db, sql, 0, 0, &err);
	return true;
}

bool SqlClient::updateUserRights(long int id, int boss)
{
	char sql[1024];
	char* err = 0;
	sprintf(sql, "UPDATE users SET boss = %d WHERE id = %ld", boss, id);
	sqlite3_exec(db, sql, 0, 0, &err);
	return true;
	return true;
}

long int SqlClient::getLastFileId()
{
	long int ret;
	sqlite3_stmt* stmt;
	char sql[1024];
	sprintf(sql, "SELECT * FROM files ORDER BY id DESC LIMIT 1;");
	sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	int result = sqlite3_step(stmt);
	if (result != SQLITE_ROW)
	{
		sqlite3_finalize(stmt);
		return 0;
	}
	ret = sqlite3_column_int64(stmt, 0);
	sqlite3_finalize(stmt);
	return ret;
}

void SqlClient::clearUnusedFiles()
{
	long int lastID = getLastFileId();
	lastID -= 100;
	if (lastID < 1)
		return;
	sqlite3_stmt* stmt;
	std::string ids;
	char* err = 0;
	ids.reserve(4000);
	char sql[4096];
	sprintf(sql, "SELECT id, server_name FROM files WHERE id < %ld;", lastID);
	sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	for (long int i = 0;; i++)
	{
		int result = sqlite3_step(stmt);
		if (result == SQLITE_DONE)
			break;
		if (result == SQLITE_ROW)
		{
			char c_id[16];
			char server_name[100];
			long int id = sqlite3_column_int64(stmt, 0);
			if(i == 0)
				sprintf(c_id, "%d", id);
			else
				sprintf(c_id, ",%d", id);
			strcpy(server_name, (char*)sqlite3_column_text(stmt, 1));
			std::remove(server_name);
			ids += c_id;
			//Проверка на ограничение длины строки
			if (ids.size() > 3900)	//Если заполнили строку, прерываемся
				break;
		}
	}
	sqlite3_finalize(stmt);
	sprintf(sql, "DELETE FROM files WHERE id IN (%s);", ids.c_str());
	sqlite3_exec(db, sql, 0, 0, &err);
}

void SqlClient::getFilesList(std::string& ret_str)
{
	ret_str.clear();
	ret_str.reserve(BUFFER_SIZE-96);
	ret_str += "0000";
	sqlite3_stmt* stmt;
	char sql[1024];
	sprintf(sql, "SELECT files.id, users.nickname, files.name, files.date FROM files INNER JOIN users ON users.id = files.user_id ORDER BY files.id DESC LIMIT 100;");
	sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	for (;;)
	{
		int result = sqlite3_step(stmt);
		if (result == SQLITE_DONE)
			break;
		if (result == SQLITE_ROW)
		{
			char c_id[16];
			char nickname[60];
			char file_name[60];
			char date[60];
			char str[196];
			long int id = sqlite3_column_int64(stmt, 0);
			strcpy(nickname, (char*)sqlite3_column_text(stmt, 1));
			strcpy(file_name, (char*)sqlite3_column_text(stmt, 2));
			strcpy(date, (char*)sqlite3_column_text(stmt, 3));
			sprintf(c_id, "%d", id);
			sprintf(str, "%s|%s|%s|%s|", c_id, nickname, file_name, date);
			int length = strlen(str);
			if ((ret_str.size() + length) > (BUFFER_SIZE - 96))	//Если заполнили строку, прерываемся
				break;

			ret_str += str;
		}
	}
	sqlite3_finalize(stmt);
}

User SqlClient::getUser(char* login, char* password)
{
	User user;
	sqlite3_stmt* stmt;
	char sql[1024];
	user.id = 0;
	escapeChars(login);
	escapeChars(password);
	sprintf(sql, "SELECT * FROM users WHERE login='%s' AND password='%s';", login, password);
	sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	int result = sqlite3_step(stmt);
	if (result != SQLITE_ROW)
	{
		sqlite3_finalize(stmt);
		return user;
	}
	user.id = sqlite3_column_int64(stmt, 0);
	user.nickname = (char*)sqlite3_column_text(stmt, 1);
	user.login = (char*)sqlite3_column_text(stmt, 2);
	user.password = (char*)sqlite3_column_text(stmt, 3);
	user.boss = sqlite3_column_int(stmt, 4);
	sqlite3_finalize(stmt);
	return user;
}

User SqlClient::getUser(long int id)
{
	User user;
	sqlite3_stmt* stmt;
	char sql[1024];
	user.id = 0;
	sprintf(sql, "SELECT * FROM users WHERE id=%ld;", id);
	sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	int result = sqlite3_step(stmt);
	if (result != SQLITE_ROW)
	{
		sqlite3_finalize(stmt);
		return user;
	}
	user.id = sqlite3_column_int64(stmt, 0);
	user.nickname = (char*)sqlite3_column_text(stmt, 1);
	user.login = (char*)sqlite3_column_text(stmt, 2);
	user.password = (char*)sqlite3_column_text(stmt, 3);
	user.boss = sqlite3_column_int(stmt, 4);
	sqlite3_finalize(stmt);
	return user;
}

bool SqlClient::getAllUsers(std::vector<User> &users)
{
	users.clear();
	users.reserve(65);
	sqlite3_stmt* stmt;
	char sql[1024];
	sprintf(sql, "SELECT id, nickname, boss FROM users;");
	sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	for (;;)
	{
		int result = sqlite3_step(stmt);
		if (result == SQLITE_DONE)
			break;
		if (result == SQLITE_ROW)
		{
			char c_id[16];
			char nickname[60];
			int boss;
			long int id = sqlite3_column_int64(stmt, 0);
			strcpy(nickname, (char*)sqlite3_column_text(stmt, 1));
			boss = sqlite3_column_int(stmt, 2);
			User user;
			user.id = id;
			user.nickname = nickname;
			user.boss = boss;
			if(user.id != 1)
				users.push_back(user);
		}
	}
	sqlite3_finalize(stmt);
	return true;
}

File SqlClient::getFile(long int id)
{
	File file;
	sqlite3_stmt* stmt;
	char sql[1024];
	file.id = 0;
	sprintf(sql, "SELECT * FROM files WHERE id=%ld;", id);
	sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	int result = sqlite3_step(stmt);
	if (result != SQLITE_ROW)
	{
		sqlite3_finalize(stmt);
		return file;
	}
	file.id = sqlite3_column_int64(stmt, 0);
	file.user_id = sqlite3_column_int64(stmt, 1);
	file.name = (char*)sqlite3_column_text(stmt, 2);
	file.server_name = (char*)sqlite3_column_text(stmt, 3);
	file.date = (char*)sqlite3_column_text(stmt, 4);
	sqlite3_finalize(stmt);
	return file;
}

Task SqlClient::getTask(long int id)
{
	Task task;
	sqlite3_stmt* stmt;
	char sql[1024];
	task.id = 0;
	sprintf(sql, "SELECT * FROM tasks WHERE id=%ld;", id);
	sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	int result = sqlite3_step(stmt);
	if (result != SQLITE_ROW)
	{
		sqlite3_finalize(stmt);
		return task;
	}
	task.id = sqlite3_column_int64(stmt, 0);
	task.user_id = sqlite3_column_int64(stmt, 1);
	task.admin_id = sqlite3_column_int64(stmt, 2);
	task.title = (char*)sqlite3_column_text(stmt, 3);
	task.text = (char*)sqlite3_column_text(stmt, 4);
	task.file_id = sqlite3_column_int64(stmt, 5);
	task.date = (char*)sqlite3_column_text(stmt, 6);
	task.done = sqlite3_column_int(stmt, 7);
	sqlite3_finalize(stmt);
	return task;
}

void SqlClient::getActiveTasks(std::vector<SendTask> &tasks)
{
	tasks.clear();
	sqlite3_stmt* stmt;
	char sql[1024];
	sprintf(sql, "SELECT tasks.id, tasks.user_id, users_u.nickname as user_name, tasks.admin_id, users_a.nickname, tasks.title, tasks.text, tasks.file_id, files.name as file_name, tasks.date, tasks.done FROM tasks LEFT JOIN users users_u ON users_u.id = tasks.user_id LEFT JOIN users users_a ON users_a.id = tasks.admin_id LEFT JOIN files ON files.id = tasks.file_id WHERE done = 0 ORDER BY tasks.id DESC LIMIT 100;");
	int r = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	for (;;)
	{
		int result = sqlite3_step(stmt);
		if (result == SQLITE_DONE)
			break;
		else if (result == SQLITE_ROW)
		{
			SendTask task;
			task.ID = sqlite3_column_int64(stmt, 0);
			task.user_id = sqlite3_column_int64(stmt, 1);
			task.user_name = (char *)sqlite3_column_text(stmt, 2);
			task.admin_id = sqlite3_column_int64(stmt, 3);
			task.admin_name = (char*)sqlite3_column_text(stmt, 4);
			task.title = (char*)sqlite3_column_text(stmt, 5);
			task.text = (char*)sqlite3_column_text(stmt, 6);
			task.file_id = sqlite3_column_int64(stmt, 7);
			if (task.file_id != 0)
				task.file_name = (char*)sqlite3_column_text(stmt, 8);
			else
				task.file_name = "";
			task.date = (char*)sqlite3_column_text(stmt, 9);
			task.done = sqlite3_column_int(stmt, 10);
			tasks.push_back(task);
		}
		else 
		{	//Ошибка!
			break;
		}
	}
	sqlite3_finalize(stmt);
}

void SqlClient::getActiveUserTasks(long int id, std::vector<SendTask>& tasks)
{
	tasks.clear();
	sqlite3_stmt* stmt;
	char sql[1024];
	sprintf(sql, "SELECT tasks.id, tasks.user_id, users_u.nickname as user_name, tasks.admin_id, users_a.nickname, tasks.title, tasks.text, tasks.file_id, files.name as file_name, tasks.date, tasks.done FROM tasks LEFT JOIN users users_u ON users_u.id = tasks.user_id LEFT JOIN users users_a ON users_a.id = tasks.admin_id LEFT JOIN files ON files.id = tasks.file_id WHERE tasks.user_id = %ld AND tasks.done = 0 ORDER BY tasks.id DESC LIMIT 100;", id);
	int r = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	for (;;)
	{
		int result = sqlite3_step(stmt);
		if (result == SQLITE_DONE)
			break;
		else if (result == SQLITE_ROW)
		{
			SendTask task;
			task.ID = sqlite3_column_int64(stmt, 0);
			task.user_id = sqlite3_column_int64(stmt, 1);
			task.user_name = (char*)sqlite3_column_text(stmt, 2);
			task.admin_id = sqlite3_column_int64(stmt, 3);
			task.admin_name = (char*)sqlite3_column_text(stmt, 4);
			task.title = (char*)sqlite3_column_text(stmt, 5);
			task.text = (char*)sqlite3_column_text(stmt, 6);
			task.file_id = sqlite3_column_int64(stmt, 7);
			if (task.file_id != 0)
				task.file_name = (char*)sqlite3_column_text(stmt, 8);
			else
				task.file_name = "";
			task.date = (char*)sqlite3_column_text(stmt, 9);
			task.done = sqlite3_column_int(stmt, 10);
			tasks.push_back(task);
		}
		else
		{	//Ошибка!
			break;
		}
	}
	sqlite3_finalize(stmt);
}

void SqlClient::getDoneTasks(std::vector<SendTask>& tasks)
{
	tasks.clear();
	sqlite3_stmt* stmt;
	char sql[1024];
	sprintf(sql, "SELECT tasks.id, tasks.user_id, users_u.nickname as user_name, tasks.admin_id, users_a.nickname, tasks.title, tasks.text, tasks.file_id, files.name as file_name, tasks.date, tasks.done FROM tasks LEFT JOIN users users_u ON users_u.id = tasks.user_id LEFT JOIN users users_a ON users_a.id = tasks.admin_id LEFT JOIN files ON files.id = tasks.file_id WHERE done > 0 ORDER BY tasks.id DESC LIMIT 100;");
	int r = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	for (;;)
	{
		int result = sqlite3_step(stmt);
		if (result == SQLITE_DONE)
			break;
		else if (result == SQLITE_ROW)
		{
			SendTask task;
			task.ID = sqlite3_column_int64(stmt, 0);
			task.user_id = sqlite3_column_int64(stmt, 1);
			task.user_name = (char*)sqlite3_column_text(stmt, 2);
			task.admin_id = sqlite3_column_int64(stmt, 3);
			task.admin_name = (char*)sqlite3_column_text(stmt, 4);
			task.title = (char*)sqlite3_column_text(stmt, 5);
			task.text = (char*)sqlite3_column_text(stmt, 6);
			task.file_id = sqlite3_column_int64(stmt, 7);
			if (task.file_id != 0)
				task.file_name = (char*)sqlite3_column_text(stmt, 8);
			else
				task.file_name = "";
			task.date = (char*)sqlite3_column_text(stmt, 9);
			task.done = sqlite3_column_int(stmt, 10);
			tasks.push_back(task);
		}
		else
		{	//Ошибка!
			break;
		}
	}
	sqlite3_finalize(stmt);
}

bool SqlClient::markTaskAsDone(long int id)
{
	char sql[1024];
	char* err = 0;
	sprintf(sql, "UPDATE tasks SET done = 1 WHERE id = %ld", id);
	int res = sqlite3_exec(db, sql, 0, 0, &err);
	if (res != SQLITE_OK)
		return false;
	return true;
}

void SqlClient::removeAllDoneTasks()
{
	char sql[1024];
	char* err = 0;
	sprintf(sql, "DELETE FROM tasks WHERE done = 1");
	sqlite3_exec(db, sql, 0, 0, &err);
}

void SqlClient::getAllUserTasks(long int id, std::vector<SendTask>& tasks)
{
	tasks.clear();
	sqlite3_stmt* stmt;
	char sql[1024];
	sprintf(sql, "SELECT tasks.id, tasks.user_id, users_u.nickname as user_name, tasks.admin_id, users_a.nickname, tasks.title, tasks.text, tasks.file_id, files.name as file_name, tasks.date, tasks.done FROM tasks LEFT JOIN users users_u ON users_u.id = tasks.user_id LEFT JOIN users users_a ON users_a.id = tasks.admin_id LEFT JOIN files ON files.id = tasks.file_id WHERE tasks.user_id = %ld ORDER BY tasks.id DESC LIMIT 100;", id);
	int r = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	for (;;)
	{
		int result = sqlite3_step(stmt);
		int columns = sqlite3_column_count(stmt);
		if (result == SQLITE_DONE)
			break;
		else if (result == SQLITE_ROW)
		{
			SendTask task;
			task.ID = sqlite3_column_int64(stmt, 0);
			task.user_id = sqlite3_column_int64(stmt, 1);
			task.user_name = (char*)sqlite3_column_text(stmt, 2);
			task.admin_id = sqlite3_column_int64(stmt, 3);
			task.admin_name = (char*)sqlite3_column_text(stmt, 4);
			task.title = (char*)sqlite3_column_text(stmt, 5);
			task.text = (char*)sqlite3_column_text(stmt, 6);
			task.file_id = sqlite3_column_int64(stmt, 7);
			if (task.file_id != 0)
				task.file_name = (char*)sqlite3_column_text(stmt, 8);
			else
				task.file_name = "";
			task.date = (char*)sqlite3_column_text(stmt, 9);
			task.done = sqlite3_column_int(stmt, 10);
			tasks.push_back(task);
		}
		else
		{	//Ошибка!
			break;
		}
	}
	sqlite3_finalize(stmt);
}

int SqlClient::getActiveTasksCount(long int id)
{
	int count = 0;
	char sql[1024];
	sqlite3_stmt* stmt;
	sprintf(sql, "SELECT COUNT(*) FROM tasks WHERE user_id=%ld AND done=0;", id);
	sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	int result = sqlite3_step(stmt);
	if (result != SQLITE_ROW)
	{
		sqlite3_finalize(stmt);
		return 0;
	}
	count = sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);
	return count;
}

SqlClient::~SqlClient() 
{
	sqlite3_close(db);
}