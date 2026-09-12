#define _WIN32_WINNT 0x0600

#include "server.h"

#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <cstring>

using namespace std;


SOCKET createServerSocket()
{
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

        return INVALID_SOCKET;
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

    int result = bind(
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

        return INVALID_SOCKET;
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

        return INVALID_SOCKET;
    }


    cout << "AegisProxy listening on port 8080"
         << endl;


    return serverSocket;
}