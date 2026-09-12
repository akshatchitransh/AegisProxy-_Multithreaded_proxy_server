#define _WIN32_WINNT 0x0600

#include "proxy.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <iostream>
#include <cstring>
#include <cstdlib>

using namespace std;


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