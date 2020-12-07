// stop_chat_server.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//

#include <iostream>
#include <fstream>

const std::string EXIT_FILE_NAME = "exit";

std::ifstream exit_file;
char exit_stat_buf[2];

int main()
{
	//Prepare exit file for working
	std::ofstream f(EXIT_FILE_NAME);
	if (!exit_file.good())
	{
		printf("Cannot open stop file\n");
		system("pause");
		return -1;	//if cannot work with exit file, then exit app EXIT STATUS = 2
	}
	f.clear();
	exit_stat_buf[0] = 0; exit_stat_buf[1] = 1;
	f << exit_stat_buf[0] << exit_stat_buf[1];
	f.close();
	return 0;
}