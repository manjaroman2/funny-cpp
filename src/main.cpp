#include "tcpserver.hpp"
#include "tcpsocket.hpp"
#include "api.hpp"

using namespace std;

TCPSocket<> **connections_map = new TCPSocket<> *[MAX_CONNECTIONS];

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
    MAGIC_TYPE magic, connections = 0;
    SIZE_TYPE message_size;
    char ip;
    char *pch;
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
            conn = create_socket(&ip, atoi(pch), connections);
            connections_map[connections] = &conn;
            write_api(CONNECT, "%d", connections); // Send connection number (magic byte + 2 bytes length + 1 byte connection number
            connections += 1;
            break;
        case DISCONNECT: 
            if (message_size > connections - 1) // We save 1 Byte by passing the connection number in the size yay 
            {
                write_log("  Connection number %d is invalid", message_size);
                break;
            }
            connections_map[message_size]->Close();
            connections_map[message_size] = connections_map[connections - 1];
            connections_map[connections - 1] = nullptr;
            connections -= 1;
            break;
        case ALLOW_CONNECT:
            if (message_size > connections - 1) // We save another Byte 
            {
                write_log("  Connection number %d is invalid", message_size);
                break;
            }
            break;
        default: // Send message to one of connected sockets
            if (magic == LOG) break; // Client should not send log messages
            if (magic > connections - 1)
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
    {
        listen_port = atoi(argv[1]);
    }
    // Initialize server socket..
    TCPServer<> tcpServer;

    // When a new client connected:
    tcpServer.onNewConnection = [&](TCPSocket<> *newClient)
    {
        // write_log("New client: [%s:%d]", newClient->remoteAddress().c_str(), newClient->remotePort());
        write_req_connect(newClient->remoteAddress().c_str(), newClient->remotePort());

        char *message_buffer = (char *)malloc(PRE_MESSAGE_BUFFER_SIZE * sizeof(char));
        int message_buffer_filled = 0;

        newClient->onRawMessageReceived = [newClient, message_buffer, &message_buffer_filled](const char *message, int length)
        {
            if (length > MAX_FULL_MESSAGE_SIZE) {
                newClient->Close(); // No log just f you  
                return;
            }
            int d = message_buffer_filled + length - PRE_MESSAGE_BUFFER_SIZE;
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