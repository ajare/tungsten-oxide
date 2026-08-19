#pragma once

#include <string>

struct DisplayMessage
{
	enum struct Level
	{
		Debug,
		Game
	};

	double time;
	Level level;
	std::string text;
};
