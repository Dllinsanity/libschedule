#pragma once

#if defined(_MSC_VER)
#include <stdint.h>
#include <stdbool.h>

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifdef SCHED_EXPORTS
#define SCHED_EXPORTS_API __declspec(dllexport)
#else
#ifdef __cplusplus
#define SCHED_EXPORTS_API extern "C" __declspec(dllimport)
#else
#define SCHED_EXPORTS_API __declspec(dllimport)
#endif
#endif

#else
#define SCHED_EXPORTS_API
#endif

enum sched_errcode
{
	SCHED_OK = 0,
	SCHED_OUT_OF_RANGE = 0x20231109,
	SCHED_MALLOC_FAIL,
};

#define DEFAULT_PREFER_CPU 0x00000001

typedef void (*sched_func)(void*);
typedef bool (*sched_exit)(void*);
typedef void (*sched_log)(const char* format, ...);

SCHED_EXPORTS_API 
int sched_init(int max_sched, bool profile);

SCHED_EXPORTS_API 
int sched_add(sched_func func, void *userdata, uint32_t update_interval, uint32_t preferred_cpu);

SCHED_EXPORTS_API 
int sched_start();

SCHED_EXPORTS_API
void sched_register_exit(sched_exit func, void* userdata);

SCHED_EXPORTS_API
void sched_register_log(sched_log func);