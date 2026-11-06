#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif
#include "core/thread.h"

struct ThreadInstance {
#ifdef _WIN32
    HANDLE      handle;
#else
    pthread_t   handle;
#endif
    ThreadFunc  func;
    void*       arg;
};

#ifdef _WIN32
static DWORD WINAPI Run(LPVOID param) {
    struct ThreadInstance* thread = param;
    thread->func(thread->arg);
    return 0;
}
#else
static void* Run(void* param) {
    struct ThreadInstance* thread = param;
    thread->func(thread->arg);
    return NULL;
}
#endif

Thread Thread_Create(ThreadFunc func, void* arg) {
    struct ThreadInstance* thread = malloc(sizeof(struct ThreadInstance));
    if (thread == NULL)
        return NULL;
    thread->func = func;
    thread->arg = arg;
#ifdef _WIN32
    thread->handle = CreateThread(NULL, 0, Run, thread, 0, NULL);
    if (thread->handle == NULL) {
#else
    if (pthread_create(&thread->handle, NULL, Run, thread) != 0) {
#endif
        free(thread);
        return NULL;
    }
    return thread;
}

void Thread_Join(Thread thread) {
    if (thread == NULL)
        return;
#ifdef _WIN32
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
#else
    pthread_join(thread->handle, NULL);
#endif
    free(thread);
}

long Atomic_Load(AtomicInt* atomic) {
#ifdef _WIN32
    return InterlockedCompareExchange(&atomic->value, 0, 0);
#else
    return __atomic_load_n(&atomic->value, __ATOMIC_SEQ_CST);
#endif
}

void Atomic_Store(AtomicInt* atomic, long value) {
#ifdef _WIN32
    InterlockedExchange(&atomic->value, value);
#else
    __atomic_store_n(&atomic->value, value, __ATOMIC_SEQ_CST);
#endif
}

long Atomic_FetchAdd(AtomicInt* atomic, long value) {
#ifdef _WIN32
    return InterlockedExchangeAdd(&atomic->value, value);
#else
    return __atomic_fetch_add(&atomic->value, value, __ATOMIC_SEQ_CST);
#endif
}

long Atomic_FetchIncrement(AtomicInt* atomic) {
#ifdef _WIN32
    return InterlockedIncrement(&atomic->value) - 1;
#else
    return __atomic_fetch_add(&atomic->value, 1, __ATOMIC_SEQ_CST);
#endif
}
