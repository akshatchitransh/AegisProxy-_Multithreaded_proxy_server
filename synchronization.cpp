#define _WIN32_WINNT 0x0600

#include "synchronization.h"
#include "proxy.h"

#include <winsock2.h>
#include <windows.h>
#include <iostream>

using namespace std;


// =====================================================
// SHARED RESOURCES
// =====================================================

// Shared bounded queue containing accepted client sockets
queue<SOCKET> clientQueue;


// Mutex protects clientQueue and everything directly
// related to accessing/modifying it.
HANDLE queueMutex;


// Counts how many clients are available in the queue.
//
// Initial value = 0
//
// Producer (main thread):
//      ReleaseSemaphore(clientAvailable)
//
// Consumer (worker):
//      WaitForSingleObject(clientAvailable)

HANDLE clientAvailable;


// Counts how many empty slots are available in the queue.
//
// Initial value = MAX_QUEUE_SIZE
//
// Producer waits before inserting.
// Consumer releases one slot after removing.

HANDLE emptySlots;


// =====================================================
// WORKER THREAD FUNCTION
//
// THESE THREADS ARE CREATED ONCE.
//
// Then they repeatedly:
//
// 1. Sleep waiting for work
// 2. Wake when clientAvailable > 0
// 3. Take one client from queue
// 4. Free one queue slot
// 5. Handle client
// 6. Return to waiting
//
// This is our basic fixed thread pool.
// =====================================================

DWORD WINAPI workerFunction(LPVOID lpParam)
{
    DWORD threadId =
        GetCurrentThreadId();


    cout << "[WORKER " << threadId
         << "] Started and waiting for tasks"
         << endl;


    while (true)
    {
        // =============================================
        // WAIT FOR AVAILABLE CLIENT
        //
        // If count == 0:
        // worker sleeps efficiently.
        //
        // No busy waiting.
        // =============================================

        WaitForSingleObject(
            clientAvailable,
            INFINITE
        );


        // =============================================
        // LOCK QUEUE
        // =============================================

        WaitForSingleObject(
            queueMutex,
            INFINITE
        );


        // =============================================
        // REMOVE ONE CLIENT FROM QUEUE
        // =============================================

        SOCKET clientSocket =
            clientQueue.front();


        clientQueue.pop();


        cout << "[WORKER " << threadId
             << "] Took client from queue"
             << endl;


        cout << "[WORKER " << threadId
             << "] Queue size after pop: "
             << clientQueue.size()
             << endl;


        // =============================================
        // UNLOCK QUEUE
        //
        // Queue is no longer being accessed.
        // =============================================

        ReleaseMutex(
            queueMutex
        );


        // =============================================
        // ONE QUEUE SLOT IS NOW FREE
        // =============================================

        ReleaseSemaphore(
            emptySlots,
            1,
            nullptr
        );


        // =============================================
        // HANDLE CLIENT OUTSIDE MUTEX
        //
        // Other workers can access queue while this
        // worker performs networking.
        // =============================================

        handleClient(
            clientSocket
        );


        cout << "[WORKER " << threadId
             << "] Finished task, returning to pool"
             << endl;
    }


    return 0;
}