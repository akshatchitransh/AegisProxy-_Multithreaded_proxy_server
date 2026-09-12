#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <string>

#include "proxy.h"
#include "cache.h"

using namespace std;


// =====================================================
// LRU RESPONSE CACHE
// =====================================================

// Maximum 5 responses
LRUCache responseCache(5);


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


    // Null terminator only for parsing/printing
    buffer[bytesReceived] = '\0';


    cout << "\n[WORKER " << threadId
         << "] REQUEST RECEIVED"
         << endl;

    cout << "-------------------------------------" << endl;
    cout << buffer << endl;
    cout << "-------------------------------------" << endl;


    // =================================================
    // CHECK HTTP METHOD AND EXTRACT REQUEST TARGET
    // =================================================

    string request(
        buffer,
        bytesReceived
    );

    bool isGetRequest = false;

    string requestTarget;


    // Find first line
    size_t firstLineEnd = request.find(
        "\r\n"
    );


    if (firstLineEnd != string::npos)
    {
        string requestLine =
            request.substr(
                0,
                firstLineEnd
            );

        // Find first space
        size_t firstSpace =
            requestLine.find(' ');

        // Find second space
        size_t secondSpace =
            requestLine.find(
                ' ',
                firstSpace + 1
            );


        if (
            firstSpace != string::npos &&
            secondSpace != string::npos
        )
        {
            string method =
                requestLine.substr(
                    0,
                    firstSpace
                );

            requestTarget =
                requestLine.substr(
                    firstSpace + 1,
                    secondSpace - firstSpace - 1
                );


            if (method == "GET")
            {
                isGetRequest = true;
            }
        }
    }


    // =================================================
    // EXTRACT HOST HEADER
    // =================================================

    const char* hostStart = strstr(
        buffer,
        "\r\nHost:"
    );


    // In case Host happens to be at the start
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


    // Skip spaces
    while (*hostStart == ' ')
    {
        hostStart++;
    }


    // Find end of Host header
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

    int hostLength =
        static_cast<int>(
            hostEnd - hostStart
        );


    char host[256];


    if (
        hostLength >=
        static_cast<int>(sizeof(host))
    )
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
    // CREATE CACHE KEY
    // =================================================

    string cacheKey;


    if (isGetRequest)
    {
        /*
            Proxy request normally contains:

            GET http://example.com/ HTTP/1.1

            So requestTarget itself can be the cache key.

            If request target is relative:

            GET /index.html HTTP/1.1

            then use:

            http://host/index.html
        */

        if (
            requestTarget.find("http://") == 0 ||
            requestTarget.find("https://") == 0
        )
        {
            cacheKey = requestTarget;
        }
        else
        {
            cacheKey =
                string("http://") +
                string(host) +
                requestTarget;
        }


        cout << "\n[WORKER " << threadId
             << "] CACHE KEY: "
             << cacheKey
             << endl;
    }


    // =================================================
    // CHECK CACHE
    // =================================================

    if (isGetRequest)
    {
        string cachedResponse;


        bool cacheHit =
            responseCache.get(
                cacheKey,
                cachedResponse
            );


        // =============================================
        // CACHE HIT
        // =============================================

        if (cacheHit)
        {
            cout << "\n[WORKER " << threadId
                 << "] CACHE HIT!"
                 << endl;


            cout << "[WORKER " << threadId
                 << "] Sending cached response"
                 << endl;


            int totalSentToClient = 0;

            int cachedResponseSize =
                static_cast<int>(
                    cachedResponse.size()
                );


            while (
                totalSentToClient <
                cachedResponseSize
            )
            {
                int bytesSentToClient = send(
                    clientSocket,
                    cachedResponse.data() +
                        totalSentToClient,
                    cachedResponseSize -
                        totalSentToClient,
                    0
                );


                if (
                    bytesSentToClient == SOCKET_ERROR ||
                    bytesSentToClient == 0
                )
                {
                    cout << "[WORKER " << threadId
                         << "] Failed to send cached response"
                         << endl;

                    closesocket(clientSocket);
                    return;
                }


                totalSentToClient +=
                    bytesSentToClient;
            }


            cout << "[WORKER " << threadId
                 << "] Cached response sent successfully"
                 << endl;


            closesocket(clientSocket);

            return;
        }


        // =============================================
        // CACHE MISS
        // =============================================

        cout << "\n[WORKER " << threadId
             << "] CACHE MISS"
             << endl;

        cout << "[WORKER " << threadId
             << "] Going to destination server..."
             << endl;
    }


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
        int hostnameLength =
            static_cast<int>(
                colon - host
            );


        if (
            hostnameLength <= 0 ||
            hostnameLength >=
                static_cast<int>(
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


        hostname[hostnameLength] =
            '\0';


        port = atoi(
            colon + 1
        );


        if (
            port <= 0 ||
            port > 65535
        )
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


    if (
        destinationSocket ==
        INVALID_SOCKET
    )
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


    // DNS result no longer required
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


        totalSent +=
            bytesSentToServer;
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


    // =================================================
    // FULL RESPONSE FOR CACHE
    // =================================================

    string fullResponse;


    // If this becomes false, we will NOT cache response
    bool responseRelaySuccessful = true;


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


        // Destination closed connection
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

            responseRelaySuccessful = false;

            break;
        }


        cout << "[WORKER " << threadId
             << "] Received "
             << bytesReceivedFromServer
             << " bytes from destination"
             << endl;


        // =============================================
        // SAVE RESPONSE FOR CACHE
        // =============================================

        if (isGetRequest)
        {
            fullResponse.append(
                responseBuffer,
                bytesReceivedFromServer
            );
        }


        // =============================================
        // FORWARD RESPONSE CHUNK TO CLIENT
        // =============================================

        int totalSentToClient = 0;


        while (
            totalSentToClient <
            bytesReceivedFromServer
        )
        {
            int bytesSentToClient = send(
                clientSocket,
                responseBuffer +
                    totalSentToClient,
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

                responseRelaySuccessful = false;

                break;
            }


            totalSentToClient +=
                bytesSentToClient;
        }


        // Could not forward complete response
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
    // STORE RESPONSE IN CACHE
    // =================================================

    if (
        isGetRequest &&
        responseRelaySuccessful &&
        !fullResponse.empty()
    )
    {
        cout << "\n[WORKER " << threadId
             << "] Storing response in cache..."
             << endl;


        responseCache.put(
            cacheKey,
            fullResponse
        );


        cout << "[WORKER " << threadId
             << "] Response stored in cache"
             << endl;
    }


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