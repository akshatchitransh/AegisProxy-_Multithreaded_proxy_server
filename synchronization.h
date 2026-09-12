#pragma once

#include <winsock2.h>
#include <windows.h>
#include <queue>

const int WORKER_COUNT =  50;
const int MAX_QUEUE_SIZE = 100;

extern std::queue<SOCKET> clientQueue;

extern HANDLE queueMutex;
extern HANDLE clientAvailable;
extern HANDLE emptySlots;

DWORD WINAPI workerFunction(LPVOID lpParam);