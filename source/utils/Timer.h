#pragma once
#include <chrono>

class Timer {
public:
	Timer();
	double GetMillisecondsElapsed();
	void Restart();
	bool Pause();
	bool Resume();
	bool Stop();
	bool Start();
	bool IsRunning();
private:
	bool isRunning = false;
	double elapsed{0};
#ifdef _WIN32
	std::chrono::time_point<std::chrono::steady_clock> start;
	std::chrono::time_point<std::chrono::steady_clock> stop;
#else
	std::chrono::time_point<std::chrono::system_clock> start;
	std::chrono::time_point<std::chrono::system_clock> stop;
#endif
};