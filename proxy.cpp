#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "cache.h"
#include <iostream>
#include <cstring>
#include <string>
#include <sstream>
using namespace std;


// =====================================================
// GLOBAL CACHE
// =====================================================

LRUCache responseCache(5);


// =====================================================
// SEND ALL DATA
// =====================================================

bool sendAll(
    SOCKET socket,
    const char* data,
    int length
)
{
    int totalSent = 0;

    while (totalSent < length)
    {
        int bytesSent = send(
            socket,
            data + totalSent,
            length - totalSent,
            0
        );

        if (bytesSent == SOCKET_ERROR)
        {
            return false;
        }

        totalSent += bytesSent;
    }

    return true;
}


// =====================================================
// RECEIVE HTTP HEADERS
// =====================================================

bool receiveHttpRequest(
    SOCKET clientSocket,
    string& request
)
{
    char buffer[4096];

    request.clear();

    while (request.find("\r\n\r\n") == string::npos)
    {
        int bytesReceived = recv(
            clientSocket,
            buffer,
            sizeof(buffer),
            0
        );

        if (bytesReceived <= 0)
        {
            return false;
        }

        request.append(
            buffer,
            bytesReceived
        );

        // Safety limit
        if (request.size() > 65536)
        {
            return false;
        }
    }

    return true;
}


// =====================================================
// EXTRACT HOST
// =====================================================

bool extractHost(
    const string& request,
    string& host
)
{
    size_t position =
        request.find("\r\nHost:");

    if (position == string::npos)
    {
        position =
            request.find("Host:");
    }

    if (position == string::npos)
    {
        return false;
    }

    position =
        request.find(':', position);

    if (position == string::npos)
    {
        return false;
    }

    position++;

    // Skip spaces
    while (
        position < request.size() &&
        request[position] == ' '
    )
    {
        position++;
    }

    size_t end =
        request.find("\r\n", position);

    if (end == string::npos)
    {
        return false;
    }

    host =
        request.substr(
            position,
            end - position
        );

    return !host.empty();
}


// =====================================================
// PARSE HOSTNAME + PORT
// =====================================================

void parseHostAndPort(
    const string& hostHeader,
    string& hostname,
    int& port
)
{
    hostname = hostHeader;
    port = 80;

    size_t colon =
        hostHeader.rfind(':');

    // Port exists
    if (
        colon != string::npos &&
        hostHeader.find(']') == string::npos
    )
    {
        hostname =
            hostHeader.substr(0, colon);

        port =
            atoi(
                hostHeader.substr(
                    colon + 1
                ).c_str()
            );
    }
}


// =====================================================
// CONNECT TO DESTINATION SERVER
// =====================================================

SOCKET connectToServer(
    const string& hostname,
    int port
)
{
    cout
        << "[PROXY] Resolving "
        << hostname
        << endl;

    addrinfo hints;
    addrinfo* result = nullptr;

    memset(
        &hints,
        0,
        sizeof(hints)
    );

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    string portString =
        to_string(port);

    int status =
        getaddrinfo(
            hostname.c_str(),
            portString.c_str(),
            &hints,
            &result
        );

    if (status != 0)
    {
        cout
            << "[PROXY] DNS resolution failed"
            << endl;

        return INVALID_SOCKET;
    }

    SOCKET destinationSocket =
        INVALID_SOCKET;

    for (
        addrinfo* ptr = result;
        ptr != nullptr;
        ptr = ptr->ai_next
    )
    {
        destinationSocket =
            socket(
                ptr->ai_family,
                ptr->ai_socktype,
                ptr->ai_protocol
            );

        if (
            destinationSocket ==
            INVALID_SOCKET
        )
        {
            continue;
        }

        if (
            connect(
                destinationSocket,
                ptr->ai_addr,
                (int)ptr->ai_addrlen
            ) == 0
        )
        {
            break;
        }

        closesocket(
            destinationSocket
        );

        destinationSocket =
            INVALID_SOCKET;
    }

    freeaddrinfo(result);

    return destinationSocket;
}


// =====================================================
// HTTPS CONNECT TUNNEL
// =====================================================

void handleConnect(
    SOCKET clientSocket,
    const string& request
)
{
    DWORD threadId =
        GetCurrentThreadId();

    cout
        << "[WORKER "
        << threadId
        << "] HTTPS CONNECT request"
        << endl;

    // -------------------------------------------------
    // REQUEST LINE
    //
    // CONNECT google.com:443 HTTP/1.1
    // -------------------------------------------------

    size_t firstSpace =
        request.find(' ');

    if (firstSpace == string::npos)
    {
        closesocket(clientSocket);
        return;
    }

    size_t secondSpace =
        request.find(
            ' ',
            firstSpace + 1
        );

    if (secondSpace == string::npos)
    {
        closesocket(clientSocket);
        return;
    }

    string target =
        request.substr(
            firstSpace + 1,
            secondSpace - firstSpace - 1
        );

    cout
        << "[WORKER "
        << threadId
        << "] CONNECT target: "
        << target
        << endl;


    // -------------------------------------------------
    // SPLIT HOST + PORT
    // -------------------------------------------------

    size_t colon =
        target.rfind(':');

    if (colon == string::npos)
    {
        closesocket(clientSocket);
        return;
    }

    string hostname =
        target.substr(
            0,
            colon
        );

    int port =
        atoi(
            target.substr(
                colon + 1
            ).c_str()
        );


    // -------------------------------------------------
    // CONNECT TO REAL SERVER
    // -------------------------------------------------

    SOCKET destinationSocket =
        connectToServer(
            hostname,
            port
        );

    if (
        destinationSocket ==
        INVALID_SOCKET
    )
    {
        cout
            << "[WORKER "
            << threadId
            << "] Could not connect to HTTPS server"
            << endl;

        const char* response =
            "HTTP/1.1 502 Bad Gateway\r\n"
            "Connection: close\r\n"
            "\r\n";

        sendAll(
            clientSocket,
            response,
            (int)strlen(response)
        );

        closesocket(clientSocket);
        return;
    }


    // -------------------------------------------------
    // TELL BROWSER TUNNEL IS READY
    // -------------------------------------------------

    const char* established =
        "HTTP/1.1 200 Connection Established\r\n"
        "\r\n";

    if (
        !sendAll(
            clientSocket,
            established,
            (int)strlen(established)
        )
    )
    {
        closesocket(destinationSocket);
        closesocket(clientSocket);
        return;
    }

    cout
        << "[WORKER "
        << threadId
        << "] HTTPS tunnel established"
        << endl;


    // -------------------------------------------------
    // BIDIRECTIONAL TCP RELAY
    // -------------------------------------------------

    char buffer[8192];

    while (true)
    {
        fd_set readSet;

        FD_ZERO(&readSet);

        FD_SET(
            clientSocket,
            &readSet
        );

        FD_SET(
            destinationSocket,
            &readSet
        );

        int result =
            select(
                0,
                &readSet,
                NULL,
                NULL,
                NULL
            );

        if (result <= 0)
        {
            break;
        }


        // Browser -> HTTPS server

        if (
            FD_ISSET(
                clientSocket,
                &readSet
            )
        )
        {
            int bytes =
                recv(
                    clientSocket,
                    buffer,
                    sizeof(buffer),
                    0
                );

            if (bytes <= 0)
            {
                break;
            }

            if (
                !sendAll(
                    destinationSocket,
                    buffer,
                    bytes
                )
            )
            {
                break;
            }
        }


        // HTTPS server -> Browser

        if (
            FD_ISSET(
                destinationSocket,
                &readSet
            )
        )
        {
            int bytes =
                recv(
                    destinationSocket,
                    buffer,
                    sizeof(buffer),
                    0
                );

            if (bytes <= 0)
            {
                break;
            }

            if (
                !sendAll(
                    clientSocket,
                    buffer,
                    bytes
                )
            )
            {
                break;
            }
        }
    }


    cout
        << "[WORKER "
        << threadId
        << "] HTTPS tunnel closed"
        << endl;


    closesocket(
        destinationSocket
    );

    closesocket(
        clientSocket
    );
}


// =====================================================
// NORMAL HTTP REQUEST
// =====================================================

void handleHttp(
    SOCKET clientSocket,
    const string& request
)
{
    DWORD threadId =
        GetCurrentThreadId();


    // =================================================
    // GET REQUEST CHECK
    // =================================================

    bool isGetRequest =
        request.compare(
            0,
            4,
            "GET "
        ) == 0;


    // =================================================
    // EXTRACT REQUEST TARGET
    // =================================================

    string requestTarget;

    size_t firstSpace =
        request.find(' ');

    if (firstSpace != string::npos)
    {
        size_t secondSpace =
            request.find(
                ' ',
                firstSpace + 1
            );

        if (
            secondSpace !=
            string::npos
        )
        {
            requestTarget =
                request.substr(
                    firstSpace + 1,
                    secondSpace - firstSpace - 1
                );
        }
    }


    // =================================================
    // EXTRACT HOST
    // =================================================

    string host;

    if (
        !extractHost(
            request,
            host
        )
    )
    {
        cout
            << "[WORKER "
            << threadId
            << "] Host header not found"
            << endl;

        closesocket(clientSocket);
        return;
    }


    cout
        << "[WORKER "
        << threadId
        << "] Host: "
        << host
        << endl;


    // =================================================
    // CACHE KEY
    // =================================================

    string cacheKey;

    if (
        requestTarget.find("http://") == 0 ||
        requestTarget.find("https://") == 0
    )
    {
        cacheKey =
            requestTarget;
    }
    else
    {
        cacheKey =
            "http://" +
            host +
            requestTarget;
    }


    cout
        << "[WORKER "
        << threadId
        << "] CACHE KEY: "
        << cacheKey
        << endl;


    // =================================================
    // CACHE CHECK
    // =================================================

    if (isGetRequest)
    {
        string cachedResponse;

        bool cacheHit =
            responseCache.get(
                cacheKey,
                cachedResponse
            );

        if (cacheHit)
        {
            cout
                << "[WORKER "
                << threadId
                << "] CACHE HIT!"
                << endl;

            bool sent =
                sendAll(
                    clientSocket,
                    cachedResponse.data(),
                    (int)cachedResponse.size()
                );

            if (sent)
            {
                cout
                    << "[WORKER "
                    << threadId
                    << "] Cached response sent"
                    << endl;
            }

            closesocket(
                clientSocket
            );

            return;
        }

        cout
            << "[WORKER "
            << threadId
            << "] CACHE MISS"
            << endl;
    }


    // =================================================
    // PARSE DESTINATION
    // =================================================

    string hostname;
    int port;

    parseHostAndPort(
        host,
        hostname,
        port
    );

    cout
        << "[WORKER "
        << threadId
        << "] Hostname: "
        << hostname
        << endl;

    cout
        << "[WORKER "
        << threadId
        << "] Port: "
        << port
        << endl;


    // =================================================
    // CONNECT TO DESTINATION
    // =================================================

    SOCKET destinationSocket =
        connectToServer(
            hostname,
            port
        );

    if (
        destinationSocket ==
        INVALID_SOCKET
    )
    {
        cout
            << "[WORKER "
            << threadId
            << "] Destination connection failed"
            << endl;

        const char* response =
            "HTTP/1.1 502 Bad Gateway\r\n"
            "Connection: close\r\n"
            "\r\n";

        sendAll(
            clientSocket,
            response,
            (int)strlen(response)
        );

        closesocket(clientSocket);
        return;
    }


    // =================================================
    // FORCE DESTINATION CONNECTION CLOSE
    //
    // This makes response completion predictable.
    // =================================================

    string modifiedRequest =
        request;

    size_t connectionPos =
        modifiedRequest.find(
            "\r\nConnection:"
        );

    if (
        connectionPos != string::npos
    )
    {
        size_t lineEnd =
            modifiedRequest.find(
                "\r\n",
                connectionPos + 2
            );

        if (lineEnd != string::npos)
        {
            modifiedRequest.replace(
                connectionPos,
                lineEnd - connectionPos,
                "\r\nConnection: close"
            );
        }
    }
    else
    {
        size_t headerEnd =
            modifiedRequest.find(
                "\r\n\r\n"
            );

        if (headerEnd != string::npos)
        {
            modifiedRequest.insert(
                headerEnd,
                "\r\nConnection: close"
            );
        }
    }


    // =================================================
    // REMOVE PROXY-CONNECTION HEADER
    // =================================================

    size_t proxyConnectionPos =
        modifiedRequest.find(
            "\r\nProxy-Connection:"
        );

    if (
        proxyConnectionPos !=
        string::npos
    )
    {
        size_t lineEnd =
            modifiedRequest.find(
                "\r\n",
                proxyConnectionPos + 2
            );

        if (lineEnd != string::npos)
        {
            modifiedRequest.erase(
                proxyConnectionPos,
                lineEnd - proxyConnectionPos
            );
        }
    }


    // =================================================
    // FORWARD REQUEST
    // =================================================

    cout
        << "[WORKER "
        << threadId
        << "] Forwarding request..."
        << endl;

    if (
        !sendAll(
            destinationSocket,
            modifiedRequest.data(),
            (int)modifiedRequest.size()
        )
    )
    {
        cout
            << "[WORKER "
            << threadId
            << "] Request forwarding failed"
            << endl;

        closesocket(destinationSocket);
        closesocket(clientSocket);
        return;
    }


    cout
        << "[WORKER "
        << threadId
        << "] Request forwarded successfully"
        << endl;


    // =================================================
    // RECEIVE + RELAY RESPONSE
    // =================================================

    char responseBuffer[8192];

    string fullResponse;

    bool relaySuccessful = true;

    while (true)
    {
        int bytesReceived =
            recv(
                destinationSocket,
                responseBuffer,
                sizeof(responseBuffer),
                0
            );


        // Destination closed

        if (bytesReceived == 0)
        {
            cout
                << "[WORKER "
                << threadId
                << "] Destination closed connection"
                << endl;

            break;
        }


        // Error

        if (
            bytesReceived ==
            SOCKET_ERROR
        )
        {
            cout
                << "[WORKER "
                << threadId
                << "] Response receive failed"
                << endl;

            relaySuccessful = false;
            break;
        }


        cout
            << "[WORKER "
            << threadId
            << "] Received "
            << bytesReceived
            << " bytes"
            << endl;


        // -------------------------------------------------
        // SAVE COMPLETE RESPONSE FOR CACHE
        // -------------------------------------------------

        if (isGetRequest)
        {
            fullResponse.append(
                responseBuffer,
                bytesReceived
            );
        }


        // -------------------------------------------------
        // SEND RESPONSE TO BROWSER
        // -------------------------------------------------

        if (
            !sendAll(
                clientSocket,
                responseBuffer,
                bytesReceived
            )
        )
        {
            cout
                << "[WORKER "
                << threadId
                << "] Failed sending response to client"
                << endl;

            relaySuccessful = false;
            break;
        }
    }


    // =================================================
    // STORE RESPONSE IN CACHE
    // =================================================

    if (
        isGetRequest &&
        relaySuccessful &&
        !fullResponse.empty()
    )
    {
        cout
            << "[WORKER "
            << threadId
            << "] Storing response in cache..."
            << endl;

        responseCache.put(
            cacheKey,
            fullResponse
        );

        cout
            << "[WORKER "
            << threadId
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

    cout
        << "[WORKER "
        << threadId
        << "] HTTP request finished"
        << endl;
}


// =====================================================
// MAIN CLIENT HANDLER
// =====================================================

void handleClient(
    SOCKET clientSocket
)
{
    DWORD threadId =
        GetCurrentThreadId();

    cout
        << "\n====================================="
        << endl;

    cout
        << "[WORKER "
        << threadId
        << "] Handling client"
        << endl;

    cout
        << "====================================="
        << endl;


    // =================================================
    // RECEIVE COMPLETE HTTP REQUEST
    // =================================================

    string request;

    if (
        !receiveHttpRequest(
            clientSocket,
            request
        )
    )
    {
        cout
            << "[WORKER "
            << threadId
            << "] Failed to receive request"
            << endl;

        closesocket(clientSocket);
        return;
    }


    // =================================================
    // PRINT REQUEST
    // =================================================

    cout
        << "[WORKER "
        << threadId
        << "] REQUEST RECEIVED"
        << endl;

    cout
        << "-------------------------------------"
        << endl;

    cout
        << request
        << endl;

    cout
        << "-------------------------------------"
        << endl;


    // =================================================
    // CHECK CONNECT
    // =================================================

    if (
        request.compare(
            0,
            8,
            "CONNECT "
        ) == 0
    )
    {
        handleConnect(
            clientSocket,
            request
        );

        return;
    }


    // =================================================
    // NORMAL HTTP
    // =================================================

    handleHttp(
        clientSocket,
        request
    );
}