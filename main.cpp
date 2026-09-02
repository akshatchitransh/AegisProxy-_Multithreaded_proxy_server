#include <iostream>
#include <winsock2.h>
#include <windows.h>
#include <cstring>
#include <queue>

using namespace std;


// =====================================================
// SHARED RESOURCES
// =====================================================

// Queue shared by main thread and worker threads
queue<SOCKET> clientQueue;

// Mutex protects the queue
HANDLE queueMutex;

// Semaphore tells workers how many clients are available
HANDLE clientAvailable;


// =====================================================
// HANDLE CLIENT
// =====================================================

void handleClient(SOCKET clientSocket) {

    DWORD threadId = GetCurrentThreadId();

    cout << "\n[WORKER " << threadId
         << "] Handling client" << endl;


    char buffer[4096];

    int bytesReceived = recv(
        clientSocket,
        buffer,
        sizeof(buffer) - 1,
        0
    );


    if (bytesReceived > 0) {

        buffer[bytesReceived] = '\0';


        cout << "[WORKER " << threadId
             << "] Request received" << endl;


        // Artificial delay so we can see concurrency
        cout << "[WORKER " << threadId
             << "] STARTED processing" << endl;

        Sleep(5000);

        cout << "[WORKER " << threadId
             << "] FINISHED processing" << endl;


        const char* response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 21\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Hello from AegisProxy";


        int bytesSent = send(
            clientSocket,
            response,
            strlen(response),
            0
        );


        if (bytesSent == SOCKET_ERROR) {

            cout << "[WORKER " << threadId
                 << "] Send failed" << endl;

        } else {

            cout << "[WORKER " << threadId
                 << "] Response sent" << endl;
        }

    } else {

        cout << "[WORKER " << threadId
             << "] Client disconnected" << endl;
    }


    closesocket(clientSocket);

    cout << "[WORKER " << threadId
         << "] Client closed" << endl;
}


// =====================================================
// WORKER THREAD FUNCTION
// =====================================================

DWORD WINAPI workerFunction(LPVOID lpParam) {

    DWORD threadId = GetCurrentThreadId();

    cout << "[WORKER " << threadId
         << "] Worker started and waiting for clients"
         << endl;


    while (true) {

        // -------------------------------------------------
        // WAIT FOR WORK
        //
        // If semaphore count is 0:
        //     worker sleeps efficiently
        //
        // When a client arrives:
        //     main thread signals semaphore
        // -------------------------------------------------

        WaitForSingleObject(
            clientAvailable,
            INFINITE
        );


        // -------------------------------------------------
        // LOCK QUEUE
        // -------------------------------------------------

        WaitForSingleObject(
            queueMutex,
            INFINITE
        );


        // Take next client from queue
        SOCKET clientSocket =
            clientQueue.front();

        clientQueue.pop();


        // -------------------------------------------------
        // UNLOCK QUEUE
        // -------------------------------------------------

        ReleaseMutex(
            queueMutex
        );


        cout << "[WORKER " << threadId
             << "] Took client from queue"
             << endl;


        // Handle client OUTSIDE mutex
        //
        // Very important:
        // We don't want to lock the queue for 5 seconds.
        handleClient(clientSocket);
    }


    return 0;
}


// =====================================================
// MAIN
// =====================================================

int main() {


    // =================================================
    // CREATE MUTEX
    // =================================================

    queueMutex = CreateMutex(
        nullptr,
        FALSE,
        nullptr
    );


    if (queueMutex == nullptr) {

        cout << "Failed to create queue mutex" << endl;

        return 1;
    }


    // =================================================
    // CREATE SEMAPHORE
    //
    // Initial count = 0
    //
    // Means:
    // No clients are available yet
    // =================================================

    clientAvailable = CreateSemaphore(
        nullptr,
        0,
        1000,
        nullptr
    );


    if (clientAvailable == nullptr) {

        cout << "Failed to create semaphore" << endl;

        CloseHandle(queueMutex);

        return 1;
    }


    // =================================================
    // START WORKER THREADS
    // =================================================

    const int WORKER_COUNT = 3;


    for (int i = 0; i < WORKER_COUNT; i++) {

        HANDLE workerThread =
            CreateThread(
                nullptr,
                0,
                workerFunction,
                nullptr,
                0,
                nullptr
            );


        if (workerThread == nullptr) {

            cout << "Failed to create worker thread "
                 << i << endl;

        } else {

            cout << "Created worker thread "
                 << i + 1 << endl;


            // We don't need the handle for now
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


    if (result != 0) {

        cout << "WSAStartup failed" << endl;

        return 1;
    }


    cout << "Winsock initialized successfully"
         << endl;


    // =================================================
    // CREATE SERVER SOCKET
    // =================================================

    SOCKET serverSocket =
        socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP
        );


    if (serverSocket == INVALID_SOCKET) {

        cout << "Socket creation failed" << endl;

        WSACleanup();

        return 1;
    }


    cout << "Socket created successfully"
         << endl;


    // =================================================
    // CONFIGURE SERVER ADDRESS
    // =================================================

    sockaddr_in serverAddress;

    serverAddress.sin_family =
        AF_INET;

    serverAddress.sin_addr.s_addr =
        INADDR_ANY;

    serverAddress.sin_port =
        htons(8080);


    // =================================================
    // BIND
    // =================================================

    result = bind(
        serverSocket,
        reinterpret_cast<sockaddr*>(&serverAddress),
        sizeof(serverAddress)
    );


    if (result == SOCKET_ERROR) {

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


    if (result == SOCKET_ERROR) {

        cout << "Listen failed" << endl;

        closesocket(serverSocket);

        WSACleanup();

        return 1;
    }


    cout << "Server listening on port 8080"
         << endl;


    // =================================================
    // MAIN THREAD = PRODUCER
    // =================================================

    while (true) {

        cout << "\n[MAIN] Waiting for client..."
             << endl;


        SOCKET clientSocket =
            accept(
                serverSocket,
                nullptr,
                nullptr
            );


        if (clientSocket == INVALID_SOCKET) {

            cout << "[MAIN] Accept failed"
                 << endl;

            continue;
        }


        cout << "[MAIN] Client accepted"
             << endl;


        // ---------------------------------------------
        // LOCK QUEUE
        // ---------------------------------------------

        WaitForSingleObject(
            queueMutex,
            INFINITE
        );


        // Add client socket to shared queue
        clientQueue.push(
            clientSocket
        );


        cout << "[MAIN] Client added to queue"
             << endl;


        // ---------------------------------------------
        // UNLOCK QUEUE
        // ---------------------------------------------

        ReleaseMutex(
            queueMutex
        );


        // ---------------------------------------------
        // SIGNAL ONE WORKER
        // ---------------------------------------------

        ReleaseSemaphore(
            clientAvailable,
            1,
            nullptr
        );


        cout << "[MAIN] Worker notified"
             << endl;
    }


    // Normally unreachable

    closesocket(serverSocket);

    WSACleanup();

    CloseHandle(queueMutex);

    CloseHandle(clientAvailable);

    return 0;
}