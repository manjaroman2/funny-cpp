#include <iostream>
#include "tcpserver.hpp"
#include "tcpsocket.hpp"
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <string>

using namespace std;

#define MAGIC_TYPE unsigned char     // 1 Byte
#define SIZE_TYPE unsigned short int // 2 Bytes
#define MAGIC_BYTES sizeof(MAGIC_TYPE)
#define SIZE_BYTES sizeof(SIZE_TYPE)
#define PREFIX_BYTES MAGIC_BYTES + SIZE_BYTES
#define MAX_MESSAGE_SIZE 65536 // 2**16 Bytes
#define DISCONNECT_MAGIC_BYTE 255
#define CONNECT_MAGIC_BYTE 254
#define LOG_MAGIC_BYTE 253
#define MAX_CONNECTIONS 252
#define LOG_FILENO STDOUT_FILENO
#define API_FILENO STDIN_FILENO

void hang_until_read(int fd, void *buf, int len)
{
    int m = 0;
    int d = len;
    while (d > 0)
    {
        buf += m;
        m = read(fd, buf, d);
        d -= m;
    }
}

void hang_until_write(int fd, void *buf, int len)
// the compiler will hopefully optimize this
{
    int m = 0;
    int d = len;
    while (d > 0)
    {
        buf += m;
        m = write(fd, buf, d);
        d -= m;
    }
}

void hang_until_socket_send(TCPSocket<> *socket, char *buf, int len)
{
    int m = 0;
    int d = len;
    while (d > 0)
    {
        buf += m;
        m = socket->Send(buf, d);
        d -= m;
    }
}

/*
    Send log message to stdout
    FORMAT:
    1 Byte magic + 2 Bytes length + MAX_READ_SIZE Bytes message
*/

int write_api(MAGIC_TYPE magic, const char *fmt, ...)
{
    int r;
    va_list va;
    va_start(va, fmt);

    va_list va2;
    va_copy(va2, va);
    r = vsnprintf(nullptr, 0, fmt, va) + 1;
    if (r > MAX_MESSAGE_SIZE)
    {
        perror("format string result is bigger than " + MAX_MESSAGE_SIZE);
        exit(1);
    }
    char buf[r];
    vsnprintf(buf, sizeof(buf), fmt, va2);
    va_end(va);
    va_end(va2);

    char message_size_buf[SIZE_BYTES];
    memcpy(message_size_buf, &r, SIZE_BYTES); // Convert ushort to 2 Bytes
    MAGIC_TYPE magic_buf[MAGIC_BYTES];
    magic_buf[0] = magic; // Log message magic byte
    hang_until_write(LOG_FILENO, magic_buf, MAGIC_BYTES);
    hang_until_write(LOG_FILENO, message_size_buf, SIZE_BYTES);
    hang_until_write(LOG_FILENO, buf, r);

    return PREFIX_BYTES + r;
}

int write_log(const char *fmt, ...) { write_api(LOG_MAGIC_BYTE, fmt); }
int write_msg(MAGIC_TYPE connection, const char *fmt, ...) { write_api(connection, fmt); }
int write_close(MAGIC_TYPE connection) { write_api(DISCONNECT_MAGIC_BYTE, "%d", connection); }

TCPSocket<> create_socket(
    char *ip, int port, MAGIC_TYPE connection) 
{

    TCPSocket<> tcpSocket([](int errorCode, std::string errorMessage)
                          { write_log("Socket creation error: %d : %s", errorCode, errorMessage); });

    tcpSocket.onRawMessageReceived = [connection](const char *message, int length)
    {
        write_msg(connection, message); 
    };

    tcpSocket.onSocketClosed = [connection](int errorCode)
    {
        write_close(connection);
    };

    tcpSocket.Connect(
        ip, port, [connection]
        {
            write_msg(connection, "Hello Server!");
        },
        [connection](int errorCode, std::string errorMessage)
        { 
            write_close(connection);
            // write_log("Connection failed: %d : %s", errorCode, errorMessage); 
        });

    return tcpSocket;
}

void start_api()
{
    /* API Language

    1 Byte magic + 2**16 bits length (ushort=2 bytes)
    */

    char *magic_buf = new char[MAGIC_BYTES];
    char *message_size_buf = new char[SIZE_BYTES];
    char *read_buffer = new char[MAX_MESSAGE_SIZE];

    TCPSocket<> **connections_map = new TCPSocket<> *[MAX_CONNECTIONS];
    memset(connections_map, 0, MAX_CONNECTIONS);
    TCPSocket<> conn;
    MAGIC_TYPE magic, connections = 0;
    SIZE_TYPE *len;
    char ip;
    char *pch;
    while (true)
    {
        hang_until_read(STDIN_FILENO, magic_buf, MAGIC_BYTES);
        hang_until_read(STDIN_FILENO, message_size_buf, SIZE_BYTES);
        memcpy(len, message_size_buf, SIZE_BYTES); // Convert 2 Bytes to ushort
        hang_until_read(STDIN_FILENO, read_buffer, *len);
        memcpy(&magic, magic_buf, MAGIC_BYTES);
        switch (magic)
        {                        // 256 Cases possible
        case CONNECT_MAGIC_BYTE: // Connect
            pch = strtok(read_buffer, ":");
            ip = *pch;
            pch = strtok(NULL, ":");
            conn = create_socket(&ip, atoi(pch), connections);
            connections_map[connections] = &conn;
            write_api(CONNECT_MAGIC_BYTE, "%d", connections); // Send connection number (magic byte + 2 bytes length + 1 byte connection number
            connections += 1;
            break;
        case DISCONNECT_MAGIC_BYTE: // Disconnect
            if (*len > connections - 1)
            {
                write_log("  Connection number %d is invalid", *len);
                break;
            }
            connections_map[*len]->Close();
            connections_map[*len] = connections_map[connections - 1];
            connections_map[connections - 1] = nullptr;
            connections -= 1;
            break;
        case LOG_MAGIC_BYTE: // Log message
            // write_log(read_buffer);
            break;
        default: // Send message to one of connected sockets
            if (magic > connections - 1)
            {
                printf("  Connection number %d is invalid\n", *magic_buf);
                break;
            }
            hang_until_socket_send(connections_map[magic], read_buffer, *len);
            break;
        }
    }
}

int main(int argc, char **argv)
{
    int listen_port = 8888;
    if (argc > 1)
    {
        listen_port = atoi(argv[1]);
    }
    // Initialize server socket..
    TCPServer<> tcpServer;

    // When a new client connected:
    tcpServer.onNewConnection = [&](TCPSocket<> *newClient)
    {
        write_log("New client: [%s:%d]", newClient->remoteAddress().c_str(), newClient->remotePort());

        // If you want to use raw bytes
        newClient->onRawMessageReceived = [newClient](const char *message, int length)
        {
            write_log("Message from the Client: %s (%d)", message, length);
            // newClient->Send("OK!");
            // TODO
        };

        newClient->onSocketClosed = [newClient](int errorCode)
        {
            write_log("Socket closed: %s:%d -> %d", newClient->remoteAddress().c_str(), newClient->remotePort(), errorCode);
            // TODO
        };
    };

    // Bind the server to a port.
    tcpServer.Bind(listen_port, [](int errorCode, string errorMessage)
                   { write_log("Binding failed: %d : %s", errorCode, errorMessage); });

    // Start Listening the server.
    tcpServer.Listen([](int errorCode, string errorMessage)
                     { write_log("Listening failed: %d : %s", errorCode, errorMessage); });

    write_log("TCP Server started on port %d", listen_port);

    start_api();

    // Close the server before exiting the program.
    tcpServer.Close();

    return 0;
}