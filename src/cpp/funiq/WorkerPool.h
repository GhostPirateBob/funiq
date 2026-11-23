#ifndef FUNIQ_WORKERPOOL_H
#define FUNIQ_WORKERPOOL_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// A fixed set of threads that run one job at a time, all of them together.
// Started once: Matcher::addBatch used to start and join its threads for every
// 256-line batch, and on log input, where most lines are memo hits, that cost
// more than the batch itself (-j 12 ran twice as long as -j 1).
class WorkerPool {
public:
	// `size` threads in total, counting the thread that calls run().
	explicit WorkerPool(unsigned size);
	~WorkerPool();
	unsigned size() const { return (unsigned)threads.size() + 1; }
	// Calls job(i) once for every i in [0, size()) at the same time, job(0) on
	// the calling thread, and returns when all of them have returned.
	void run(const std::function<void(unsigned)>& job);
private:
	std::vector<std::thread> threads;
	std::mutex mutex;
	std::condition_variable wake, finished;
	const std::function<void(unsigned)>* job = nullptr;
	unsigned long generation = 0; // bumped once per run()
	unsigned busy = 0;            // pool threads still inside the current job
	bool stopping = false;
	void loop(unsigned index);
	WorkerPool(const WorkerPool&) = delete;
	WorkerPool& operator=(const WorkerPool&) = delete;
};

inline WorkerPool::WorkerPool(unsigned size) {
	for(unsigned i = 1; i < size; i++) threads.emplace_back(&WorkerPool::loop, this, i);
}

inline WorkerPool::~WorkerPool() {
	{
		std::lock_guard<std::mutex> lock(mutex);
		stopping = true;
	}
	wake.notify_all();
	for(auto& t : threads) t.join();
}

inline void WorkerPool::run(const std::function<void(unsigned)>& f) {
	if(threads.empty()) { f(0); return; }
	{
		std::lock_guard<std::mutex> lock(mutex);
		job = &f;
		busy = (unsigned)threads.size();
		generation++;
	}
	wake.notify_all();
	f(0);
	std::unique_lock<std::mutex> lock(mutex);
	finished.wait(lock, [this] { return busy == 0; });
}

inline void WorkerPool::loop(unsigned index) {
	unsigned long seen = 0;
	for(;;) {
		const std::function<void(unsigned)>* f;
		{
			std::unique_lock<std::mutex> lock(mutex);
			wake.wait(lock, [&] { return stopping || generation != seen; });
			if(stopping) return;
			seen = generation;
			f = job;
		}
		(*f)(index);
		std::lock_guard<std::mutex> lock(mutex);
		if(--busy == 0) finished.notify_one();
	}
}

#endif // FUNIQ_WORKERPOOL_H
