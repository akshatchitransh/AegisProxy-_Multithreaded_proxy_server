#include <iostream>
#include <winsock2.h>
#include <windows.h>
#include <cstring>
#include <queue>

using namespace std;


// =====================================================
// CONFIGURATION
// =====================================================

const int WORKER_COUNT = 3;
const int MAX_QUEUE_SIZE = 5;


// =====================================================
// SHARED RESOURCES
// =====================================================

// Shared queue containing accepted client sockets
queue<SOCKET> clientQueue;


// Mutex protects the queue
HANDLE queueMutex;


// Semaphore:
// Number of clients currently available in queue
HANDLE clientAvailable;


// Semaphore:
// Number of empty slots available in queue
HANDLE emptySlots;


// =====================================================
// HANDLE CLIENT
// =====================================================

void handleClient(SOCKET clientSocket)
{
    DWORD threadId = GetCurrentThreadId();

    cout << "\n=================================" << endl;
    cout << "[WORKER " << threadId << "] Handling client" << endl;
    cout << "=================================" << endl;


    // ---------------------------------------------
    // RECEIVE REQUEST
    // ---------------------------------------------

    char buffer[4096];

    int bytesReceived = recv(
        clientSocket,
        buffer,
        sizeof(buffer) - 1,
        0
    );


    if (bytesReceived > 0)
    {
        buffer[bytesReceived] = '\0';

        cout << "\n[WORKER " << threadId
             << "] Request received" << endl;

        cout << "-----------------" << endl;

        cout << buffer << endl;


        // ---------------------------------------------
        // ARTIFICIAL DELAY
        // To visibly demonstrate concurrency
        // ---------------------------------------------

        cout << "[WORKER " << threadId
             << "] STARTED processing" << endl;

        Sleep(5000);

        cout << "[WORKER " << threadId
             << "] FINISHED processing" << endl;


        // ---------------------------------------------
        // HTTP RESPONSE
        // ---------------------------------------------

        const char* response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 21\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Hello from AegisProxy";


        // ---------------------------------------------
        // SEND RESPONSE
        // ---------------------------------------------

        int bytesSent = send(
            clientSocket,
            response,
            strlen(response),
            0
        );


        if (bytesSent == SOCKET_ERROR)
        {
            cout << "[WORKER " << threadId
                 << "] Send failed" << endl;
        }
        else
        {
            cout << "[WORKER " << threadId
                 << "] Response sent successfully" << endl;
        }
    }
    else if (bytesReceived == 0)
    {
        cout << "[WORKER " << threadId
             << "] Client disconnected" << endl;
    }
    else
    {
        cout << "[WORKER " << threadId
             << "] recv() failed" << endl;
    }


    // ---------------------------------------------
    // CLOSE CLIENT CONNECTION
    // ---------------------------------------------

    closesocket(clientSocket);

    cout << "[WORKER " << threadId
         << "] Client connection closed" << endl;

    cout << "[WORKER " << threadId
         << "] Finished task and returning to pool"
         << endl;
}


// =====================================================
// WORKER THREAD FUNCTION
// =====================================================

DWORD WINAPI workerFunction(LPVOID lpParam)
{
    DWORD threadId = GetCurrentThreadId();

    cout << "[WORKER " << threadId
         << "] Started and waiting for tasks"
         << endl;


    // Worker lives forever
    while (true)
    {
        // =============================================
        // STEP 1:
        // WAIT FOR A CLIENT TO BECOME AVAILABLE
        //
        // If queue has 0 clients:
        // Worker sleeps efficiently here
        // =============================================

        WaitForSingleObject(
            clientAvailable,
            INFINITE
        );


        // =============================================
        // STEP 2:
        // LOCK QUEUE
        // =============================================

        WaitForSingleObject(
            queueMutex,
            INFINITE
        );


        // =============================================
        // STEP 3:
        // TAKE CLIENT FROM QUEUE
        // =============================================

        SOCKET clientSocket = clientQueue.front();

        clientQueue.pop();

        cout << "[WORKER " << threadId
             << "] Took client from queue"
             << endl;

        cout << "[WORKER " << threadId
             << "] Queue size after pop: "
             << clientQueue.size()
             << endl;


        // =============================================
        // STEP 4:
        // UNLOCK QUEUE
        // =============================================

        ReleaseMutex(queueMutex);


        // =============================================
        // STEP 5:
        // INFORM PRODUCER THAT ONE SLOT IS NOW EMPTY
        // =============================================

        ReleaseSemaphore(
            emptySlots,
            1,
            nullptr
        );


        // =============================================
        // STEP 6:
        // HANDLE CLIENT
        //
        // IMPORTANT:
        // This happens OUTSIDE THE MUTEX
        //
        // Other workers can access queue meanwhile
        // =============================================

        handleClient(clientSocket);


        // After handling:
        //
        // Worker DOES NOT DIE.
        //
        // It loops back and waits for another task.
    }


    return 0;
}


// =====================================================
// MAIN
// =====================================================

int main()
{
    // =================================================
    // CREATE QUEUE MUTEX
    // =================================================

    queueMutex = CreateMutex(
        nullptr,
        FALSE,
        nullptr
    );


    if (queueMutex == nullptr)
    {
        cout << "Failed to create queue mutex" << endl;
        return 1;
    }


    // =================================================
    // CREATE CLIENT AVAILABLE SEMAPHORE
    //
    // Initial value = 0
    //
    // Initially queue is empty.
    // =================================================

    clientAvailable = CreateSemaphore(
        nullptr,
        0,
        MAX_QUEUE_SIZE,
        nullptr
    );


    if (clientAvailable == nullptr)
    {
        cout << "Failed to create clientAvailable semaphore"
             << endl;

        CloseHandle(queueMutex);

        return 1;
    }


    // =================================================
    // CREATE EMPTY SLOTS SEMAPHORE
    //
    // Initially:
    //
    // Queue is completely empty
    //
    // Therefore:
    //
    // Empty slots = MAX_QUEUE_SIZE
    // =================================================

    emptySlots = CreateSemaphore(
        nullptr,
        MAX_QUEUE_SIZE,
        MAX_QUEUE_SIZE,
        nullptr
    );


    if (emptySlots == nullptr)
    {
        cout << "Failed to create emptySlots semaphore"
             << endl;

        CloseHandle(queueMutex);
        CloseHandle(clientAvailable);

        return 1;
    }


    // =================================================
    // CREATE WORKER THREADS
    //
    // THESE ARE CREATED BEFORE ANY CLIENT ARRIVES
    //
    // This is the Thread Pool
    // =================================================

    cout << "\nCreating Thread Pool..." << endl;


    for (int i = 0; i < WORKER_COUNT; i++)
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
            cout << "Worker "
                 << i + 1
                 << " created successfully"
                 << endl;


            // We don't need to join these threads.
            //
            // Closing the HANDLE does NOT kill thread.
            //
            // Thread continues running.
            CloseHandle(workerThread);
        }
    }


    // =================================================
    // INITIALIZE WINSOCK
    // =================================================

    WSADATA wsaData;


    int result = WSAStartup(
        MAKEWORD(2, 2),
        &wsaData
    );


    if (result != 0)
    {
        cout << "WSAStartup failed" << endl;
        return 1;
    }


    cout << "\nWinsock initialized successfully"
         << endl;


    // =================================================
    // CREATE SERVER SOCKET
    // =================================================

    SOCKET serverSocket = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP
    );


    if (serverSocket == INVALID_SOCKET)
    {
        cout << "Socket creation failed" << endl;

        WSACleanup();

        return 1;
    }


    cout << "Server socket created successfully"
         << endl;


    // =================================================
    // CONFIGURE SERVER ADDRESS
    // =================================================

    sockaddr_in serverAddress;

    serverAddress.sin_family = AF_INET;

    serverAddress.sin_addr.s_addr = INADDR_ANY;

    serverAddress.sin_port = htons(8080);


    // =================================================
    // BIND
    // =================================================

    result = bind(
        serverSocket,
        reinterpret_cast<sockaddr*>(&serverAddress),
        sizeof(serverAddress)
    );


    if (result == SOCKET_ERROR)
    {
        cout << "Bind failed" << endl;

        closesocket(serverSocket);

        WSACleanup();

        return 1;
    }


    cout << "Socket bound to port 8080"
         << endl;


    // =================================================
    // LISTEN
    // =================================================

    result = listen(
        serverSocket,
        SOMAXCONN
    );


    if (result == SOCKET_ERROR)
    {
        cout << "Listen failed" << endl;

        closesocket(serverSocket);

        WSACleanup();

        return 1;
    }


    cout << "Server listening on port 8080"
         << endl;


    // =================================================
    // MAIN THREAD = PRODUCER
    //
    // Workers = CONSUMERS
    // =================================================

    while (true)
    {
        cout << "\n[MAIN] Waiting for client..."
             << endl;


        // ---------------------------------------------
        // ACCEPT CLIENT
        // ---------------------------------------------

        SOCKET clientSocket = accept(
            serverSocket,
            nullptr,
            nullptr
        );


        if (clientSocket == INVALID_SOCKET)
        {
            cout << "[MAIN] Accept failed"
                 << endl;

            continue;
        }


        cout << "[MAIN] Client accepted"
             << endl;


        // =============================================
        // WAIT FOR EMPTY SLOT
        //
        // If queue is full:
        //
        // Main thread blocks efficiently here.
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
        // ADD CLIENT TO QUEUE
        // =============================================

        clientQueue.push(clientSocket);


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
        // INFORM WORKER:
        //
        // One client is available.
        // =============================================

        ReleaseSemaphore(
            clientAvailable,
            1,
            nullptr
        );


        cout << "[MAIN] One worker notified"
             << endl;
    }


    // =================================================
    // CLEANUP
    //
    // Currently unreachable because of while(true)
    // =================================================

    closesocket(serverSocket);

    WSACleanup();

    CloseHandle(queueMutex);

    CloseHandle(clientAvailable);

    CloseHandle(emptySlots);


    return 0;
}