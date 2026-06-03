#include "Timer.h"

Timer::Timer()
{
	start = std::chrono::high_resolution_clock::now();
	stop = std::chrono::high_resolution_clock::now();
}

double Timer::GetMillisecondsElapsed()
{
	if (isRunning)
	{
		return this->elapsed + std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start).count();
	}
	else
	{
		return this->elapsed;
	}
}

void Timer::Restart()
{
	isRunning = true;
	start = std::chrono::high_resolution_clock::now();
}

bool Timer::Pause()
{
	if (!isRunning) {
		return false;
	}
	isRunning = false;
	this->elapsed += std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start).count();

	return true;
}
bool Timer::Resume()
{
	if (isRunning) {
		return false;
	}
	isRunning = true;
	start = std::chrono::high_resolution_clock::now();
	return true;
}

bool Timer::Stop()
{
	stop = std::chrono::high_resolution_clock::now();
	this->elapsed = 0;
	isRunning = false;
	return true;
}

bool Timer::Start()
{
	if (isRunning)
	{
		return false;
	}
	else
	{
		start = std::chrono::high_resolution_clock::now();
		isRunning = true;
		return true;
	}
}

bool Timer::IsRunning()
{
	return this->isRunning;
}
