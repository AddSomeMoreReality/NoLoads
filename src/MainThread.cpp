#include "MainThread.h"

namespace
{
	std::mutex                         g_lock;
	std::vector<std::function<void()>> g_tasks;
}

namespace MainThread
{
	void Post(std::function<void()> a_task)
	{
		const std::scoped_lock lock{ g_lock };
		g_tasks.push_back(std::move(a_task));
	}

	void Drain()
	{
		std::vector<std::function<void()>> tasks;
		{
			const std::scoped_lock lock{ g_lock };
			tasks.swap(g_tasks);
		}
		for (auto& task : tasks) {
			task();
		}
	}
}
