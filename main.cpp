#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <iostream>

#include "server.h"
#include "synchronization.h"
#include "server.h"

using namespace std;


int main()
{
    // =================================================
    // INITIALIZE WINSOCK FIRST
    // =================================================

    WSADATA wsaData;


    int result = WSAStartup(
        MAKEWORD(2, 2),
        &wsaData
    );


    if (result != 0)
    {
        cout << "WSAStartup failed"
             << endl;

        return 1;
    }


    cout << "Winsock initialized successfully"
         << endl;


    // =================================================
    // CREATE MUTEX
    // =================================================

    queueMutex = CreateMutex(
        nullptr,
        FALSE,
        nullptr
    );


    if (queueMutex == nullptr)
    {
        cout << "Failed to create queue mutex"
             << endl;

        WSACleanup();

        return 1;
    }


    // =================================================
    // CREATE clientAvailable SEMAPHORE
    //
    // Initially queue has 0 clients.
    // =================================================

    clientAvailable = CreateSemaphore(
        nullptr,
        0,
        MAX_QUEUE_SIZE,
        nullptr
    );


    if (clientAvailable == nullptr)
    {
        cout << "Failed to create client semaphore"
             << endl;

        CloseHandle(queueMutex);

        WSACleanup();

        return 1;
    }


    // =================================================
    // CREATE emptySlots SEMAPHORE
    //
    // Initially entire queue is empty.
    //
    // Therefore:
    //
    // emptySlots = MAX_QUEUE_SIZE
    // =================================================

    emptySlots = CreateSemaphore(
        nullptr,
        MAX_QUEUE_SIZE,
        MAX_QUEUE_SIZE,
        nullptr
    );


    if (emptySlots == nullptr)
    {
        cout << "Failed to create empty slots semaphore"
             << endl;

        CloseHandle(clientAvailable);
        CloseHandle(queueMutex);

        WSACleanup();

        return 1;
    }


    // =================================================
    // CREATE SERVER
    // =================================================

    SOCKET serverSocket =
        createServerSocket();


    if (serverSocket == INVALID_SOCKET)
    {
        CloseHandle(emptySlots);
        CloseHandle(clientAvailable);
        CloseHandle(queueMutex);

        WSACleanup();

        return 1;
    }


    // =================================================
    // CREATE FIXED WORKER THREAD POOL
    //
    // Threads are created BEFORE clients arrive.
    //
    // They start immediately and sleep on
    // clientAvailable semaphore until work arrives.
    // =================================================

    for (
        int i = 0;
        i < WORKER_COUNT;
        i++
    )
    {
        HANDLE workerThread = CreateThread(
            nullptr,
            0,
            workerFunction,
            nullptr,
            0,
            nullptr
        );


        if (workerThread == nullptr)
        {
            cout << "Failed to create worker "
                 << i + 1
                 << endl;
        }
        else
        {
            cout << "[MAIN] Worker "
                 << i + 1
                 << " created"
                 << endl;


            // Worker continues running even after
            // its HANDLE is closed.
            //
            // We simply don't need the handle because
            // worker runs forever.

            CloseHandle(
                workerThread
            );
        }
    }


    // =================================================
    // MAIN THREAD = PRODUCER
    //
    // Accept client
    //        ↓
    // Wait for empty queue slot
    //        ↓
    // Lock queue
    //        ↓
    // Push client
    //        ↓
    // Unlock queue
    //        ↓
    // Signal available client
    // =================================================

    while (true)
    {
        cout << "\n[MAIN] Waiting for client..."
             << endl;


        SOCKET clientSocket = accept(
            serverSocket,
            nullptr,
            nullptr
        );


        if (clientSocket == INVALID_SOCKET)
        {
            cout << "[MAIN] accept() failed"
                 << endl;

            continue;
        }


        cout << "[MAIN] Client accepted"
             << endl;


        // =============================================
        // WAIT FOR EMPTY QUEUE SLOT
        //
        // If queue is full:
        // main thread sleeps here.
        //
        // No busy waiting.
        // =============================================

        WaitForSingleObject(
            emptySlots,
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
        // PUSH CLIENT INTO QUEUE
        // =============================================

        clientQueue.push(
            clientSocket
        );


        cout << "[MAIN] Client added to queue"
             << endl;


        cout << "[MAIN] Queue size: "
             << clientQueue.size()
             << endl;


        // =============================================
        // UNLOCK QUEUE
        // =============================================

        ReleaseMutex(
            queueMutex
        );


        // =============================================
        // SIGNAL THAT ONE CLIENT IS AVAILABLE
        //
        // This wakes one sleeping worker.
        // =============================================

        ReleaseSemaphore(
            clientAvailable,
            1,
            nullptr
        );


        cout << "[MAIN] One worker notified"
             << endl;
    }


    // Normally unreachable because server runs forever.

    closesocket(
        serverSocket
    );


    CloseHandle(
        emptySlots
    );


    CloseHandle(
        clientAvailable
    );


    CloseHandle(
        queueMutex
    );


    WSACleanup();


    return 0;
}