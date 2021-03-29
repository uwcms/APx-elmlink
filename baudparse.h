#ifndef BAUDPARSE_H
#define BAUDPARSE_H

#include <list>
#include <string>

class BaudRate {
public:
	std::string str;
	int rate;
	int flag;
	BaudRate() : str(""), rate(0), flag(0){};
	BaudRate(std::string str, int rate, int flag) : str(str), rate(rate), flag(flag){};

	static const std::list<BaudRate> baud_settings;
	static BaudRate find_setting(std::string str);
};

#endif
