#define SCHED_EXPORTS
#include "schedule.h"
#include "spinlock.h"
#include "sched_thread.h"

#include <stdio.h>
#include <Windows.h>

#pragma comment(lib, "winmm.lib")

#define LOG_MESSAGE_SIZE 256

struct args 
{
	struct thread_event* wait;
	sched_func func;
	void* ud;
	uint32_t update_interval;
	uint32_t preferred_cpu;
	int n;
	uint64_t logic_cost;
	uint64_t real_cost;
	uint64_t avg;
	uint32_t overrun;
	uint64_t tick_count;
};

struct sched_t
{
	struct spinlock lock;
	int current;
	int max;
	struct args* args;
	sched_exit exit_func;
	void* exit_ud;
	sched_log log;
	bool exit;
	bool profile;
};

struct sched_t SCHED;

static uint64_t
gettime_us() {
	uint64_t ret = 0;
	__int64 freq = 0;
	__int64 count = 0;

	if (QueryPerformanceFrequency((LARGE_INTEGER*)&freq)) {
		if (freq > 0) {
			QueryPerformanceCounter((LARGE_INTEGER*)&count);
			ret = (uint64_t)((double)count * 1000000 / (double)freq);
		}
	}
	return ret;
}

static void log_print(const char* format, ...)
{
	char tmp[LOG_MESSAGE_SIZE];
	char* data = NULL;

	va_list ap;

	va_start(ap, format);
	int len = vsnprintf(tmp, LOG_MESSAGE_SIZE, format, ap);
	va_end(ap);

	if (len >= 0 && len < LOG_MESSAGE_SIZE) 
	{
		data = _strdup(tmp);
	}
	else 
	{
		int max_size = LOG_MESSAGE_SIZE;
		for (;;) {
			max_size *= 2;
			data = malloc(max_size);
			va_start(ap, format);
			len = vsnprintf(data, max_size, format, ap);
			va_end(ap);
			if (len < max_size) {
				break;
			}
			free(data);
		}
	}
	if (len < 0)
	{
		free(data);
		perror("vsnprintf error :");
		return;
	}

	if (SCHED.log)
	{
		spinlock_lock(&SCHED.lock);
		SCHED.log(data);
		spinlock_unlock(&SCHED.lock);
		free(data);
	}
	else
	{
		spinlock_lock(&SCHED.lock);
		fprintf(stdout, data);
		fprintf(stdout, "\n");
		spinlock_unlock(&SCHED.lock);
		fflush(stdout);
		free(data);
	}
}

static void sched_thread(void* p)
{
	// caculate based on us
	struct args* args = p;
	uint64_t start;
	int64_t wait;
	uint64_t next;
	uint64_t logic_cost;
	uint64_t circle_cost = 0;
	const int32_t delay = args->update_interval * 1000;
	const uint32_t base_resolution = 1000; // us
	static uint64_t last_start = 0;

	HANDLE h = GetCurrentThread();

	if (args->preferred_cpu != DEFAULT_PREFER_CPU)
	{
		SetThreadIdealProcessor(h, args->preferred_cpu);
	}

	for (;;)
	{
		if (SCHED.exit) return;
		start = gettime_us();

		// Execute task here
		args->func(args->ud);
		
		logic_cost = gettime_us() - start;

		if (last_start == 0)
		{
			circle_cost = 0;
		}
		else
		{
			circle_cost = start - last_start;
		}
		last_start = start;

		if (SCHED.profile)
		{
			args->logic_cost += args->update_interval;
			args->real_cost += logic_cost;
			args->avg = (args->avg + logic_cost) / 2;
			log_print("circle spend time = %lld(us)\t\
Thread[%d]: logic cost=%llu ms; real cost=%llu ms; avg=%llu us; overrun times=%lu",
				circle_cost,
				args->n, 
				args->logic_cost, 
				args->real_cost / 1000, 
				args->avg, 
				args->overrun);
		}
		++args->tick_count;
		wait = delay - logic_cost;
		if (wait < 0)
		{
			++args->overrun;
			HANDLE h = GetCurrentThread();
			DWORD tid = GetCurrentThreadId();
			log_print("Thread[%d] overrun! Runtime: [%llu]us; Tick cout: [%llu]; Thread handle: [%lu]; Tid: [%lu]", 
				args->n,
				logic_cost,
				args->tick_count,
				h, tid);
			continue;
		}

		if (wait > base_resolution)
		{
			Sleep(
				(uint32_t)((wait - base_resolution) / 1000)
			);
		}

		next = start + delay;
		while(gettime_us() < next) {}
	}

}

static void exit_thread(void* p)
{
	sched_exit cb = SCHED.exit_func;
	void* ud= SCHED.exit_ud;

	for (;;) {
		if (cb(ud))
		{
			SCHED.exit = true;
			return;
		}
		Sleep(50);
	}
}

int sched_init(int max_sched, bool profile)
{
	SCHED.current = 0;
	SCHED.max = max_sched;
	SCHED.exit = false;
	SCHED.profile = profile;
	spinlock_init(&SCHED.lock);

	SCHED.args = malloc(sizeof(*SCHED.args) * max_sched);
	if (SCHED.args != NULL)
	{
		memset(SCHED.args, 0, sizeof(*SCHED.args) * max_sched);
		return SCHED_OK;
	}
	else
		return SCHED_MALLOC_FAIL;
}

int sched_add(sched_func fn, void* p, uint32_t update_interval, uint32_t preferred_cpu)
{
	int current;
	struct args* arg;
	if (SCHED.current < SCHED.max)
	{
		current = SCHED.current;
		arg = &SCHED.args[current];
		arg->func = fn;
		arg->ud = p;
		arg->update_interval = update_interval;
		arg->preferred_cpu = preferred_cpu;
		arg->logic_cost = 0;
		arg->real_cost = 0;
		arg->avg = 0;
		arg->overrun = 0;
		++SCHED.current;
		return SCHED_OK;
	}

	return SCHED_OUT_OF_RANGE;
}

int sched_start()
{
	struct thread* t;
	HANDLE myHandle = GetCurrentProcess();
	int count = SCHED.current;

	t = malloc(sizeof(*t) * (count + 1));
	if (t)
		memset(t, 0, sizeof(*t) * (count + 1));
	else
		return SCHED_MALLOC_FAIL;

	for (int i = 0; i < count; i++)
	{
		t[i].func = sched_thread;
		t[i].ud = &SCHED.args[i];
		SCHED.args[i].n = i;
	}

	if (SCHED.exit_func)
	{
		t[count].func = exit_thread;
		t[count].ud = SCHED.exit_ud;
	}

	timeBeginPeriod(1);
	SetPriorityClass(myHandle, HIGH_PRIORITY_CLASS);
	thread_join(t, count + 1);
	timeEndPeriod(1);

	free(t);
	free(SCHED.args);

	return SCHED_OK;
}

void sched_register_exit(sched_exit func, void* userdata)
{
	SCHED.exit_func = func;
	SCHED.exit_ud = userdata;
}

void sched_register_log(sched_log func)
{
	SCHED.log = func;
}