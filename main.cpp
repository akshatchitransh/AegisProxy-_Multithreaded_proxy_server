#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <iostream>
#include <cstring>
#include <queue>
#include <cstdlib>

using namespace std;


// =====================================================
// CONFIGURATION
// =====================================================

const int WORKER_COUNT = 3;
const int MAX_QUEUE_SIZE = 5;


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
// HANDLE CLIENT
// =====================================================

void handleClient(SOCKET clientSocket)
{
    DWORD threadId = GetCurrentThreadId();

    cout << "\n=====================================" << endl;
    cout << "[WORKER " << threadId << "] Handling client" << endl;
    cout << "=====================================" << endl;


    // =================================================
    // RECEIVE HTTP REQUEST FROM CLIENT
    // =================================================

    char buffer[4096];

    int bytesReceived = recv(
        clientSocket,
        buffer,
        sizeof(buffer) - 1,
        0
    );


    if (bytesReceived == 0)
    {
        cout << "[WORKER " << threadId
             << "] Client disconnected"
             << endl;

        closesocket(clientSocket);
        return;
    }


    if (bytesReceived == SOCKET_ERROR)
    {
        cout << "[WORKER " << threadId
             << "] recv() failed"
             << endl;

        closesocket(clientSocket);
        return;
    }


    // Add null terminator only for parsing/printing.
    //
    // bytesReceived still represents the real number
    // of bytes that must be forwarded.
    buffer[bytesReceived] = '\0';


    cout << "\n[WORKER " << threadId
         << "] REQUEST RECEIVED"
         << endl;

    cout << "-------------------------------------" << endl;
    cout << buffer << endl;
    cout << "-------------------------------------" << endl;


    // =================================================
    // EXTRACT HOST HEADER
    // =================================================

    const char* hostStart = strstr(
        buffer,
        "\r\nHost:"
    );


    // In case Host happens to be at the start.
    if (hostStart == nullptr)
    {
        if (strncmp(buffer, "Host:", 5) == 0)
        {
            hostStart = buffer;
        }
    }


    if (hostStart == nullptr)
    {
        cout << "[WORKER " << threadId
             << "] Host header not found"
             << endl;

        closesocket(clientSocket);
        return;
    }


    // Move pointer after "Host:"
    if (strncmp(hostStart, "\r\nHost:", 7) == 0)
    {
        hostStart += 7;
    }
    else
    {
        hostStart += 5;
    }


    // Skip spaces after Host:
    while (*hostStart == ' ')
    {
        hostStart++;
    }


    // Find the end of Host header.
    const char* hostEnd = strstr(
        hostStart,
        "\r\n"
    );


    if (hostEnd == nullptr)
    {
        cout << "[WORKER " << threadId
             << "] Invalid Host header"
             << endl;

        closesocket(clientSocket);
        return;
    }


    // =================================================
    // COPY HOST VALUE
    // =================================================

    int hostLength = static_cast<int>(
        hostEnd - hostStart
    );


    char host[256];


    if (hostLength >= static_cast<int>(sizeof(host)))
    {
        cout << "[WORKER " << threadId
             << "] Host header too large"
             << endl;

        closesocket(clientSocket);
        return;
    }


    strncpy(
        host,
        hostStart,
        hostLength
    );


    host[hostLength] = '\0';


    cout << "\n[WORKER " << threadId
         << "] DESTINATION HOST EXTRACTED"
         << endl;

    cout << "[WORKER " << threadId
         << "] Host: "
         << host
         << endl;


    // =================================================
    // PARSE HOSTNAME AND PORT
    // =================================================

    char hostname[256];

    int port = 80;


    char* colon = strchr(
        host,
        ':'
    );


    if (colon != nullptr)
    {
        int hostnameLength = static_cast<int>(
            colon - host
        );


        if (
            hostnameLength <= 0 ||
            hostnameLength >= static_cast<int>(
                sizeof(hostname)
            )
        )
        {
            cout << "[WORKER " << threadId
                 << "] Invalid hostname"
                 << endl;

            closesocket(clientSocket);
            return;
        }


        strncpy(
            hostname,
            host,
            hostnameLength
        );


        hostname[hostnameLength] = '\0';


        port = atoi(
            colon + 1
        );


        if (port <= 0 || port > 65535)
        {
            cout << "[WORKER " << threadId
                 << "] Invalid port"
                 << endl;

            closesocket(clientSocket);
            return;
        }
    }
    else
    {
        strncpy(
            hostname,
            host,
            sizeof(hostname) - 1
        );


        hostname[
            sizeof(hostname) - 1
        ] = '\0';
    }


    cout << "\n[WORKER " << threadId
         << "] DESTINATION PARSED"
         << endl;

    cout << "[WORKER " << threadId
         << "] Hostname: "
         << hostname
         << endl;

    cout << "[WORKER " << threadId
         << "] Port: "
         << port
         << endl;


    // =================================================
    // DNS RESOLUTION
    // =================================================

    addrinfo hints;

    memset(
        &hints,
        0,
        sizeof(hints)
    );


    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;


    char portString[10];

    sprintf(
        portString,
        "%d",
        port
    );


    addrinfo* resultInfo = nullptr;


    cout << "\n[WORKER " << threadId
         << "] Resolving hostname..."
         << endl;


    int dnsResult = getaddrinfo(
        hostname,
        portString,
        &hints,
        &resultInfo
    );


    if (dnsResult != 0)
    {
        cout << "[WORKER " << threadId
             << "] DNS resolution failed"
             << endl;

        closesocket(clientSocket);
        return;
    }


    cout << "[WORKER " << threadId
         << "] DNS resolution successful"
         << endl;


    // =================================================
    // CREATE DESTINATION SOCKET
    // =================================================

    SOCKET destinationSocket = socket(
        resultInfo->ai_family,
        resultInfo->ai_socktype,
        resultInfo->ai_protocol
    );


    if (destinationSocket == INVALID_SOCKET)
    {
        cout << "[WORKER " << threadId
             << "] Failed to create destination socket"
             << endl;

        freeaddrinfo(resultInfo);

        closesocket(clientSocket);

        return;
    }


    cout << "[WORKER " << threadId
         << "] Destination socket created"
         << endl;


    // =================================================
    // CONNECT TO DESTINATION SERVER
    // =================================================

    cout << "[WORKER " << threadId
         << "] Connecting to "
         << hostname
         << ":"
         << port
         << " ..."
         << endl;


    int connectResult = connect(
        destinationSocket,
        resultInfo->ai_addr,
        static_cast<int>(
            resultInfo->ai_addrlen
        )
    );


    // DNS result no longer required after connect().
    freeaddrinfo(resultInfo);


    if (connectResult == SOCKET_ERROR)
    {
        cout << "[WORKER " << threadId
             << "] Connection to destination FAILED"
             << endl;

        closesocket(destinationSocket);
        closesocket(clientSocket);

        return;
    }


    cout << "[WORKER " << threadId
         << "] Successfully connected to destination!"
         << endl;


    // =================================================
    // FORWARD ORIGINAL REQUEST TO DESTINATION
    // =================================================

    cout << "\n[WORKER " << threadId
         << "] Forwarding request to destination..."
         << endl;


    int totalSent = 0;


    while (totalSent < bytesReceived)
    {
        int bytesSentToServer = send(
            destinationSocket,
            buffer + totalSent,
            bytesReceived - totalSent,
            0
        );


        if (
            bytesSentToServer == SOCKET_ERROR ||
            bytesSentToServer == 0
        )
        {
            cout << "[WORKER " << threadId
                 << "] Failed to forward request"
                 << endl;

            closesocket(destinationSocket);
            closesocket(clientSocket);

            return;
        }


        totalSent += bytesSentToServer;
    }


    cout << "[WORKER " << threadId
         << "] Request successfully forwarded!"
         << endl;


    // =================================================
    // RECEIVE RESPONSE FROM DESTINATION
    // AND FORWARD IT TO CLIENT
    // =================================================

    cout << "\n[WORKER " << threadId
         << "] Waiting for destination response..."
         << endl;


    char responseBuffer[8192];


    while (true)
    {
        // =============================================
        // RECEIVE RESPONSE CHUNK
        // =============================================

        int bytesReceivedFromServer = recv(
            destinationSocket,
            responseBuffer,
            sizeof(responseBuffer),
            0
        );


        // Destination closed connection.
        if (bytesReceivedFromServer == 0)
        {
            cout << "[WORKER " << threadId
                 << "] Destination server closed connection"
                 << endl;

            break;
        }


        if (bytesReceivedFromServer == SOCKET_ERROR)
        {
            cout << "[WORKER " << threadId
                 << "] Error receiving response"
                 << endl;

            break;
        }


        cout << "[WORKER " << threadId
             << "] Received "
             << bytesReceivedFromServer
             << " bytes from destination"
             << endl;


        // =============================================
        // FORWARD ENTIRE RESPONSE CHUNK TO CLIENT
        // =============================================

        int totalSentToClient = 0;


        while (
            totalSentToClient <
            bytesReceivedFromServer
        )
        {
            int bytesSentToClient = send(
                clientSocket,
                responseBuffer + totalSentToClient,
                bytesReceivedFromServer -
                    totalSentToClient,
                0
            );


            if (
                bytesSentToClient == SOCKET_ERROR ||
                bytesSentToClient == 0
            )
            {
                cout << "[WORKER " << threadId
                     << "] Failed to forward response to client"
                     << endl;

                break;
            }


            totalSentToClient +=
                bytesSentToClient;
        }


        // Could not forward complete response.
        if (
            totalSentToClient <
            bytesReceivedFromServer
        )
        {
            cout << "[WORKER " << threadId
                 << "] Response relay stopped"
                 << endl;

            break;
        }


        cout << "[WORKER " << threadId
             << "] Response chunk forwarded to client"
             << endl;
    }


    cout << "\n[WORKER " << threadId
         << "] Response relay finished"
         << endl;


    // =================================================
    // CLEANUP
    // =================================================

    closesocket(
        destinationSocket
    );


    closesocket(
        clientSocket
    );


    cout << "[WORKER " << threadId
         << "] Client and destination connections closed"
         << endl;
}


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


// =====================================================
// MAIN
// =====================================================

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
    // CREATE SERVER SOCKET
    // =================================================

    SOCKET serverSocket = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP
    );


    if (serverSocket == INVALID_SOCKET)
    {
        cout << "Server socket creation failed"
             << endl;

        CloseHandle(emptySlots);
        CloseHandle(clientAvailable);
        CloseHandle(queueMutex);

        WSACleanup();

        return 1;
    }


    cout << "Server socket created successfully"
         << endl;


    // =================================================
    // SERVER ADDRESS
    // =================================================

    sockaddr_in serverAddress;

    memset(
        &serverAddress,
        0,
        sizeof(serverAddress)
    );


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
        reinterpret_cast<sockaddr*>(
            &serverAddress
        ),
        sizeof(serverAddress)
    );


    if (result == SOCKET_ERROR)
    {
        cout << "Bind failed"
             << endl;

        closesocket(serverSocket);

        CloseHandle(emptySlots);
        CloseHandle(clientAvailable);
        CloseHandle(queueMutex);

        WSACleanup();

        return 1;
    }


    cout << "Server bound to port 8080"
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
        cout << "Listen failed"
             << endl;

        closesocket(serverSocket);

        CloseHandle(emptySlots);
        CloseHandle(clientAvailable);
        CloseHandle(queueMutex);

        WSACleanup();

        return 1;
    }


    cout << "AegisProxy listening on port 8080"
         << endl;


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