#include "schedule.h"
#include <stdio.h>
#include <Windows.h>

struct data
{
	int n;
};

static bool exit_func(void* p)
{
	// if return true, the scheduler will terminate working threads.
	// this func will be called every 100ms
	return false;
}

static void run(void* p)
{
	data* d = (data*)p;
	// do sth
	d->n++;
}

int main(int argc, char* argv[])
{
	data d;

	sched_init(10, true);
	sched_add(run, &d,30, DEFAULT_PREFER_CPU);
	sched_register_exit(exit_func, NULL);
	sched_start();

	return 0;
}