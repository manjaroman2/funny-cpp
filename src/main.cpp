#include "tcpserver.hpp"
#include "tcpsocket.hpp"
#include "api.hpp"
#include <atomic>
#include <algorithm>

TCPSocket<> **connections_map = new TCPSocket<> *[MAX_CONNECTIONS];
char **message_buffer = new char *[MAX_CONNECTIONS];
bool *connection_accepted = new bool[MAX_CONNECTIONS];

std::atomic<MAGIC_TYPE> CC = 0; // Connections counter

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
        { write_msg(connection, "Hello Server!"); },
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

    memset(connections_map, 0, MAX_CONNECTIONS);
    TCPSocket<> conn;
    MAGIC_TYPE magic;
    MAGIC_TYPE client_CC;
    SIZE_TYPE message_size;
    char ip;
    char *pch;
    int message_buffer_filled;
    while (true)
    {
        hang_until_read(STDIN_FILENO, magic_buf, MAGIC_BYTES);
        hang_until_read(STDIN_FILENO, message_size_buf, SIZE_BYTES);
        memcpy(&message_size, message_size_buf, SIZE_BYTES); // Convert 2 Bytes to ushort
        hang_until_read(STDIN_FILENO, read_buffer, message_size);
        memcpy(&magic, magic_buf, MAGIC_BYTES);
        switch (magic)
        {             // 256 Cases possible
        case CONNECT: // Connect
            pch = strtok(read_buffer, ":");
            ip = *pch;
            pch = strtok(NULL, ":");
            conn = create_socket(&ip, atoi(pch), CC);
            connections_map[CC] = &conn;
            // write_api(CONNECT, "%d", CC); // Send connection number (magic byte + 2 bytes length + 1 byte connection number
            write_connect(CC);
            CC += 1;
            memset(read_buffer, 0, message_size);
            break;
        case DISCONNECT:
            if (message_size > CC - 1) // We save 1 Byte by passing the connection number in the size yay
            {
                write_log("  Connection number %d is invalid", message_size);
                break;
            }
            client_CC = (MAGIC_TYPE) message_size;
            connections_map[client_CC]->Close();
            CC -= 1;
            connections_map[client_CC] = connections_map[CC];
            connections_map[CC] = nullptr;
            break;
        case ALLOW_CONNECT:
            if (message_size > CC - 1) // We save another Byte
            {
                write_log("  Connection number %d is invalid", message_size);
                break;
            }
            client_CC = (MAGIC_TYPE) message_size;
            message_buffer_filled = strlen(message_buffer[client_CC]);
            while (message_buffer_filled > 0) // Send message buffer
            {
                if (message_buffer_filled - MAX_MESSAGE_SIZE <= 0)
                {
                    memcpy(read_buffer, message_buffer[client_CC], message_buffer_filled);
                    message_buffer_filled = 0;
                    write_msg(client_CC, read_buffer);
                    memset(read_buffer, 0, message_buffer_filled);
                    break;
                }
                memcpy(read_buffer, message_buffer[client_CC], MAX_MESSAGE_SIZE);
                message_buffer_filled -= MAX_MESSAGE_SIZE;
                write_msg(client_CC, read_buffer);
                memset(read_buffer, 0, MAX_MESSAGE_SIZE); // This will be called more often than nessesary but it's fine
            }
            break;
        case CONNECTION_INFO:
            if (message_size > CC - 1) // We save another Byte
            {
                write_log("  Connection number %d is invalid", message_size);
                break;
            }
            conn = *connections_map[message_size];
            write_api(CONNECTION_INFO, "%s:%d", conn.remoteAddress().c_str(), conn.remotePort());
            break;
        default: // Send message to one of connected sockets
            if (magic == LOG)
                break; // Client should not send log messages
            if (magic > CC - 1)
            {
                printf("  Connection number %d is invalid\n", *magic_buf);
                break;
            }
            hang_until_socket_send(connections_map[magic], read_buffer, message_size);
            break;
        }
    }
}

int main(int argc, char **argv)
{
    int listen_port = 8888;
    if (argc > 1)
        listen_port = atoi(argv[1]);
    // Initialize server socket..
    TCPServer<> tcpServer;

    // When a new client connected:
    tcpServer.onNewConnection = [](TCPSocket<> *newClient)
    {
        // write_log("New client: [%s:%d]", newClient->remoteAddress().c_str(), newClient->remotePort());
        MAGIC_TYPE client_CC = CC;
        write_req_connect(CC);
        CC += 1;
        message_buffer[client_CC] = new char[MAX_MESSAGE_SIZE];
        int message_buffer_filled = 0;

        newClient->onRawMessageReceived = [newClient, &message_buffer_filled, &client_CC](const char *message, int length)
        {
            if (length > MAX_MESSAGE_SIZE) // No log just f you
                return newClient->Close();
            if (connections_map[client_CC] != nullptr) // Connection accepted
                write_msg(client_CC, message);
            else
            { // Save messages to buffer while connection is not accepted
                int d = message_buffer_filled + length - MAX_MESSAGE_SIZE;
                if (d > 0)
                {
                    write_log("Message buffer overflow from %s:%d by %d bytes", newClient->remoteAddress().c_str(), newClient->remotePort(), d);
                    newClient->Send(make_buffer(MESSAGE_BUFFER_OVERFLOW, "Message buffer overflow by %d bytes", d));
                    write_log("Closing connection with %s:%d", newClient->remoteAddress().c_str(), newClient->remotePort());
                    newClient->Close();
                    return;
                }
                memcpy(message_buffer, message, length);
                message_buffer_filled += length;
                write_log("Message from the Client %s:%d with %d bytes -> message_buffer", length, newClient->remoteAddress().c_str(), newClient->remotePort());
                // TODO
                // Maybe done for now?
            }
        };

        newClient->onSocketClosed = [newClient, &client_CC](int errorCode)
        {
            write_log("Socket closed: %s:%d -> %d", newClient->remoteAddress().c_str(), newClient->remotePort(), errorCode);
            write_close(client_CC);
            delete[] message_buffer[client_CC];
            // TODO
        };
    };

    // Bind the server to a port.
    tcpServer.Bind(listen_port, [](int errorCode, std::string errorMessage)
                   { write_log("Binding failed: %d : %s", errorCode, errorMessage); });

    // Start Listening the server.
    tcpServer.Listen([](int errorCode, std::string errorMessage)
                     { write_log("Listening failed: %d : %s", errorCode, errorMessage); });

    write_log("TCP Server started on port %d", listen_port);

    start_api();

    // Close the server before exiting the program.
    tcpServer.Close();

    return 0;
}