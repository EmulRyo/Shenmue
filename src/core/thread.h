#ifndef __THREAD_H__
#define __THREAD_H__

#include <stdbool.h>

// Threads and atomic counters, on Win32 or POSIX threads

typedef struct ThreadInstance* Thread;
typedef void (*ThreadFunc)(void* arg);

// Starts running func(arg) in a new thread. NULL if it can't be created
Thread Thread_Create(ThreadFunc func, void* arg);
// Waits for the thread to end and frees it
void   Thread_Join(Thread thread);

typedef struct { volatile long value; } AtomicInt;

long Atomic_Load(AtomicInt* atomic);
void Atomic_Store(AtomicInt* atomic, long value);
// Adds 1 and returns the value before
long Atomic_FetchIncrement(AtomicInt* atomic);
// Adds value and returns the value before
long Atomic_FetchAdd(AtomicInt* atomic, long value);

#endif
