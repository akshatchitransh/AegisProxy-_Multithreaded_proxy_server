#include <iostream>
#include <winsock2.h>
#include <windows.h>
#include <cstring>

using namespace std;


// =====================================================
// This function runs in a separate Windows thread
// =====================================================

DWORD WINAPI handleClient(LPVOID lpParam) {

    // Convert generic pointer back to SOCKET pointer
    SOCKET* clientSocketPtr =
        static_cast<SOCKET*>(lpParam);

    // Copy actual socket value
    SOCKET clientSocket =
        *clientSocketPtr;

    // Heap memory is no longer needed
    delete clientSocketPtr;


    // Get ID of current worker thread
    DWORD threadId = GetCurrentThreadId();

    cout << "\n=================================" << endl;
    cout << "Client thread started" << endl;
    cout << "Thread ID: " << threadId << endl;
    cout << "=================================" << endl;


    // Receive HTTP request
    char buffer[4096];

    int bytesReceived = recv(
        clientSocket,
        buffer,
        sizeof(buffer) - 1,
        0
    );


    if (bytesReceived > 0) {

        // Add null terminator
        buffer[bytesReceived] = '\0';


        cout << "\nThread " << threadId
             << " received request" << endl;

        cout << "-----------------" << endl;

        cout << buffer << endl;


        // Artificial delay to test concurrency
        cout << "\nThread " << threadId
             << " STARTED processing" << endl;

        Sleep(5000);

        cout << "Thread " << threadId
             << " FINISHED processing" << endl;


        // Create HTTP response
        const char* response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 21\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Hello from AegisProxy";


        // Send response
        int bytesSent = send(
            clientSocket,
            response,
            strlen(response),
            0
        );


        if (bytesSent == SOCKET_ERROR) {

            cout << "Thread " << threadId
                 << " failed to send response"
                 << endl;

        }
        else {

            cout << "Thread " << threadId
                 << " sent response successfully"
                 << endl;
        }

    }
    else {

        cout << "Thread " << threadId
             << ": no data received or client disconnected"
             << endl;
    }


    // Close client connection
    closesocket(clientSocket);

    cout << "Thread " << threadId
         << " closed client connection"
         << endl;

    cout << "Thread " << threadId
         << " finished"
         << endl;


    // End worker thread
    return 0;
}


// =====================================================
// MAIN SERVER
// =====================================================

int main() {

    // Step 1: Initialize Winsock
    WSADATA wsaData;

    int result = WSAStartup(
        MAKEWORD(2, 2),
        &wsaData
    );

    if (result != 0) {

        cout << "WSAStartup failed" << endl;

        return 1;
    }

    cout << "Winsock initialized successfully" << endl;


    // Step 2: Create TCP socket
    SOCKET serverSocket = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP
    );

    if (serverSocket == INVALID_SOCKET) {

        cout << "Socket creation failed" << endl;

        WSACleanup();

        return 1;
    }

    cout << "Socket created successfully" << endl;


    // Step 3: Configure server address
    sockaddr_in serverAddress;

    serverAddress.sin_family = AF_INET;

    serverAddress.sin_addr.s_addr =
        INADDR_ANY;

    serverAddress.sin_port =
        htons(8080);


    // Step 4: Bind socket
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

    cout << "Socket bound successfully to port 8080" << endl;


    // Step 5: Listen
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

    cout << "Server is listening on port 8080" << endl;


    // =====================================================
    // MAIN THREAD: ONLY ACCEPTS CLIENTS
    // =====================================================

    while (true) {

        cout << "\n[MAIN THREAD] Waiting for a client..."
             << endl;


        // Create separate memory for every client's socket
        SOCKET* clientSocketPtr =
            new SOCKET;


        // Accept client
        *clientSocketPtr = accept(
            serverSocket,
            nullptr,
            nullptr
        );


        if (*clientSocketPtr == INVALID_SOCKET) {

            cout << "[MAIN THREAD] Accept failed"
                 << endl;

            delete clientSocketPtr;

            continue;
        }


        cout << "[MAIN THREAD] Client accepted"
             << endl;


        // Create a separate Windows OS thread
        HANDLE clientThread = CreateThread(

            nullptr,          // Default security attributes

            0,                // Default stack size

            handleClient,     // Function executed by new thread

            clientSocketPtr,  // Argument passed to thread

            0,                // Start immediately

            nullptr           // We don't need thread ID here
        );


        if (clientThread == nullptr) {

            cout << "[MAIN THREAD] Thread creation failed"
                 << endl;

            closesocket(*clientSocketPtr);

            delete clientSocketPtr;

            continue;
        }


        cout << "[MAIN THREAD] Worker thread created"
             << endl;


        // We don't want main thread to wait for worker thread.
        //
        // CloseHandle DOES NOT stop the thread.
        // It only releases the main thread's HANDLE reference.
        CloseHandle(clientThread);


        // Immediately loop back to accept another client
    }


    // Normally unreachable
    closesocket(serverSocket);

    WSACleanup();

    return 0;
}