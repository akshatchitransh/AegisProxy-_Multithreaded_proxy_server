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
const int MAX_QUEUE_SIZE = 10;


// =====================================================
// SHARED RESOURCES
// =====================================================

// Shared queue containing accepted client sockets
queue<SOCKET> clientQueue;


// Mutex protects all queue operations:
// push(), pop(), front(), size()
HANDLE queueMutex;


// Counts available clients in queue.
// Workers wait on this semaphore.
HANDLE clientAvailable;


// Counts free slots in bounded queue.
// Main thread waits when queue is full.
HANDLE queueSlots;


// =====================================================
// HANDLE CLIENT
// =====================================================

void handleClient(SOCKET clientSocket)
{
    DWORD threadId = GetCurrentThreadId();

    cout << "\n=====================================" << endl;

    cout << "[WORKER " << threadId
         << "] Handling client"
         << endl;

    cout << "====================================="
         << endl;


    // =================================================
    // RECEIVE HTTP REQUEST
    // =================================================

    char buffer[4096];


    int bytesReceived = recv(

        clientSocket,
        buffer,
        sizeof(buffer) - 1,
        0

    );


    if (bytesReceived <= 0)
    {
        cout << "[WORKER " << threadId
             << "] Client disconnected or recv failed"
             << endl;


        closesocket(clientSocket);

        return;
    }


    // Null terminate received request
    buffer[bytesReceived] = '\0';


    cout << "\n[WORKER " << threadId
         << "] Request received:"
         << endl;

    cout << "-------------------------------------"
         << endl;

    cout << buffer << endl;

    cout << "-------------------------------------"
         << endl;


    // =================================================
    // EXTRACT HOST HEADER
    // =================================================

    const char* hostStart = strstr(

        buffer,
        "\r\nHost:"

    );


    // Handle case where Host happens to be at beginning
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


    // Find end of Host line
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

    int hostLength = hostEnd - hostStart;


    char host[256];


    if (hostLength >= 255)
    {
        hostLength = 255;
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


    // Look for :port
    char* colon = strchr(

        host,
        ':'

    );


    if (colon != nullptr)
    {
        // Host contains hostname:port

        int hostnameLength = colon - host;


        if (hostnameLength >= 255)
        {
            hostnameLength = 255;
        }


        strncpy(

            hostname,
            host,
            hostnameLength

        );


        hostname[hostnameLength] = '\0';


        // Convert port text to integer
        port = atoi(

            colon + 1

        );
    }
    else
    {
        // No port specified.
        // Default HTTP port = 80.

        strncpy(

            hostname,
            host,
            sizeof(hostname) - 1

        );


        hostname[sizeof(hostname) - 1] = '\0';
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

    cout << "\n[WORKER " << threadId
         << "] Resolving hostname..."
         << endl;


    addrinfo hints;


    memset(

        &hints,
        0,
        sizeof(hints)

    );


    // IPv4
    hints.ai_family = AF_INET;

    // TCP
    hints.ai_socktype = SOCK_STREAM;


    // Convert port integer to string
    char portString[10];


    sprintf(

        portString,
        "%d",
        port

    );


    // Pointer where DNS result will be stored
    addrinfo* resultInfo = nullptr;


    int dnsResult = getaddrinfo(

        hostname,
        portString,
        &hints,
        &resultInfo

    );


    if (dnsResult != 0)
    {
        cout << "[WORKER " << threadId
             << "] DNS resolution failed for: "
             << hostname
             << endl;


        closesocket(clientSocket);

        return;
    }


    cout << "[WORKER " << threadId
         << "] Hostname resolved successfully"
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

        resultInfo->ai_addrlen

    );


    // DNS address information is no longer needed
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
// FORWARD ORIGINAL REQUEST TO DESTINATION SERVER
// =================================================

cout << "\n[WORKER " << threadId
     << "] Forwarding request to destination..."
     << endl;


// buffer contains the original request received from client
// bytesReceived tells us how many bytes were actually received

int totalSent = 0;


while (totalSent < bytesReceived)
{
    int bytesSentToServer = send(

        destinationSocket,

        buffer + totalSent,

        bytesReceived - totalSent,

        0

    );


    if (bytesSentToServer == SOCKET_ERROR)
    {
        cout << "[WORKER " << threadId
             << "] Failed to forward request to destination"
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
// RECEIVE RESPONSE FROM DESTINATION SERVER
// AND FORWARD IT TO CLIENT
// =================================================

cout << "\n[WORKER " << threadId
     << "] Waiting for response from destination..."
     << endl;


// Buffer for chunks received from destination
char responseBuffer[8192];


while (true)
{
    // =============================================
    // RECEIVE RESPONSE FROM DESTINATION SERVER
    // =============================================

    int bytesReceivedFromServer = recv(

        destinationSocket,

        responseBuffer,

        sizeof(responseBuffer),

        0

    );


    // Server closed connection
    if (bytesReceivedFromServer == 0)
    {
        cout << "[WORKER " << threadId
             << "] Destination server closed connection"
             << endl;

        break;
    }


    // Receive error
    if (bytesReceivedFromServer == SOCKET_ERROR)
    {
        cout << "[WORKER " << threadId
             << "] Error receiving response from destination"
             << endl;

        break;
    }


    cout << "[WORKER " << threadId
         << "] Received "
         << bytesReceivedFromServer
         << " bytes from destination"
         << endl;


    // =============================================
    // FORWARD ENTIRE CHUNK TO CLIENT
    // =============================================

    int totalSentToClient = 0;


    while (totalSentToClient < bytesReceivedFromServer)
    {
        int bytesSentToClient = send(

            clientSocket,

            responseBuffer + totalSentToClient,

            bytesReceivedFromServer - totalSentToClient,

            0

        );


        if (bytesSentToClient == SOCKET_ERROR)
        {
            cout << "[WORKER " << threadId
                 << "] Failed to forward response to client"
                 << endl;

            break;
        }


        totalSentToClient += bytesSentToClient;
    }


    cout << "[WORKER " << threadId
         << "] Response chunk forwarded to client"
         << endl;


    // If sending to client failed,
    // stop receiving more data.
    if (totalSentToClient < bytesReceivedFromServer)
    {
        break;
    }
}


cout << "[WORKER " << threadId
     << "] Response relay finished"
     << endl;

    // =================================================
    // CURRENT TEMPORARY RESPONSE
    //
    // Next step:
    //
    // send(destinationSocket, buffer, bytesReceived, 0)
    //
    // Then receive destination response.
    // =================================================


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


    if (bytesSent == SOCKET_ERROR)
    {
        cout << "[WORKER " << threadId
             << "] Response send failed"
             << endl;
    }
    else
    {
        cout << "[WORKER " << threadId
             << "] Temporary response sent to client"
             << endl;
    }


    // =================================================
    // CLEANUP CLIENT + DESTINATION
    // =================================================

    closesocket(

        destinationSocket

    );


    cout << "[WORKER " << threadId
         << "] Destination connection closed"
         << endl;


    closesocket(

        clientSocket

    );


    cout << "[WORKER " << threadId
         << "] Client connection closed"
         << endl;
}


// =====================================================
// WORKER THREAD FUNCTION
// =====================================================

DWORD WINAPI workerFunction(LPVOID lpParam)
{
    DWORD threadId = GetCurrentThreadId();


    cout << "[WORKER " << threadId
         << "] Worker started and waiting for tasks"
         << endl;


    while (true)
    {
        // =============================================
        // WAIT FOR AVAILABLE CLIENT
        //
        // If queue is empty:
        // worker sleeps.
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


        // Take one client from queue
        SOCKET clientSocket =

            clientQueue.front();


        clientQueue.pop();


        // =============================================
        // UNLOCK QUEUE
        // =============================================

        ReleaseMutex(

            queueMutex

        );


        // =============================================
        // ONE SLOT BECAME FREE
        //
        // Increase available queue slots
        // =============================================

        ReleaseSemaphore(

            queueSlots,
            1,
            nullptr

        );


        cout << "[WORKER " << threadId
             << "] Took client from queue"
             << endl;


        // Handle outside mutex
        handleClient(

            clientSocket

        );
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
        cout << "Failed to create queue mutex"
             << endl;

        return 1;
    }


    // =================================================
    // CREATE CLIENT AVAILABLE SEMAPHORE
    //
    // Initially:
    // Queue has 0 clients
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

        return 1;
    }


    // =================================================
    // CREATE QUEUE SLOT SEMAPHORE
    //
    // Initially:
    // All MAX_QUEUE_SIZE slots are available
    // =================================================

    queueSlots = CreateSemaphore(

        nullptr,
        MAX_QUEUE_SIZE,
        MAX_QUEUE_SIZE,
        nullptr

    );


    if (queueSlots == nullptr)
    {
        cout << "Failed to create queue slot semaphore"
             << endl;

        CloseHandle(queueMutex);

        CloseHandle(clientAvailable);

        return 1;
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
        cout << "WSAStartup failed"
             << endl;

        return 1;
    }


    cout << "Winsock initialized successfully"
         << endl;


    // =================================================
    // CREATE FIXED WORKER THREAD POOL
    // =================================================

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
            cout << "Created worker "
                 << i + 1
                 << endl;


            // Thread continues running.
            // We just don't need the HANDLE anymore.
            CloseHandle(workerThread);
        }
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

        WSACleanup();

        return 1;
    }


    cout << "\nAegisProxy listening on port 8080"
         << endl;


    // =================================================
    // MAIN THREAD = PRODUCER
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
            cout << "[MAIN] Accept failed"
                 << endl;

            continue;
        }


        cout << "[MAIN] Client accepted"
             << endl;


        // =============================================
        // WAIT FOR FREE QUEUE SLOT
        //
        // If queue is full:
        // main thread sleeps efficiently.
        // =============================================

        WaitForSingleObject(

            queueSlots,
            INFINITE

        );


        // =============================================
        // LOCK QUEUE
        // =============================================

        WaitForSingleObject(

            queueMutex,
            INFINITE

        );


        // Add client socket to queue
        clientQueue.push(

            clientSocket

        );


        cout << "[MAIN] Client added to queue"
             << endl;


        // =============================================
        // UNLOCK QUEUE
        // =============================================

        ReleaseMutex(

            queueMutex

        );


        // =============================================
        // SIGNAL AVAILABLE CLIENT
        // =============================================

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

    CloseHandle(queueSlots);


    return 0;
}