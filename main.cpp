#include <iostream>
#include <winsock2.h>
#include <windows.h>
#include <cstring>

using namespace std;

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


    // Step 2: Create TCP server socket
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
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(8080);


    // Step 4: Bind socket to port 8080
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


    // Step 5: Listen for incoming connections
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


    // Step 6: Keep accepting clients forever
    while (true) {

        cout << endl;
        cout << "Waiting for a client..." << endl;


        // Accept a new client
        SOCKET clientSocket = accept(
            serverSocket,
            nullptr,
            nullptr
        );

        if (clientSocket == INVALID_SOCKET) {

            cout << "Accept failed" << endl;

            continue;
        }

        cout << "Client connected!" << endl;


        // Step 7: Receive HTTP request
        char buffer[4096];

        int bytesReceived = recv(
            clientSocket,
            buffer,
            sizeof(buffer) - 1,
            0
        );


        if (bytesReceived > 0) {

            // Add null terminator so we can print as text
            buffer[bytesReceived] = '\0';

            cout << endl;
            cout << "Request received:" << endl;
            cout << "-----------------" << endl;

            cout << buffer << endl;

        }
        else {

            cout << "No data received or client disconnected" << endl;

            closesocket(clientSocket);

            continue;
        }


        // Artificial delay
        // Sleep takes milliseconds
        cout << "Processing request for 5 seconds..." << endl;

        Sleep(10000);


        // Step 8: Create HTTP response
        const char* response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 21\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Hello from AegisProxy";


        // Step 9: Send HTTP response
        int bytesSent = send(
            clientSocket,
            response,
            strlen(response),
            0
        );


        if (bytesSent == SOCKET_ERROR) {

            cout << "Send failed" << endl;

        }
        else {

            cout << "Response sent successfully!" << endl;
        }


        // Close only this client's connection
        closesocket(clientSocket);

        cout << "Client connection closed" << endl;
    }


    // Normally unreachable because while(true) runs forever
    closesocket(serverSocket);

    WSACleanup();

    return 0;
}