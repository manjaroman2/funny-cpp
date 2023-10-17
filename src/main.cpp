#include "tcpserver.hpp"
#include "tcpsocket.hpp"
#include "api.hpp"
#include <atomic>
#include <algorithm>

TCPSocket<> *connections_map[MAX_CONNECTIONS];
char *pre_message_buffer[MAX_CONNECTIONS];
bool connection_accepted[MAX_CONNECTIONS];

std::atomic<Magic> CC = 0; // Connections counter, points to the next free connection

TCPSocket<> create_socket(
    std::string ip, const int port, Magic connection)
{

    TCPSocket<> tcpSocket([](int errorCode, std::string errorMessage)
                          { log_info("Socket creation error: %d : %s", errorCode, errorMessage); });

    tcpSocket.onRawMessageReceived = [connection](const char *message, int length)
    {
        api_message(connection, message);
    };

    tcpSocket.onSocketClosed = [connection](int errorCode)
    {
        api_disconnect(connection);
    };

    tcpSocket.Connect(
        ip, port, [connection]
        { api_connect(connection); },
        [connection](int errorCode, std::string errorMessage)
        {
            api_disconnect(connection);
            // log_info("Connection failed: %d : %s", errorCode, errorMessage);
        });

    return tcpSocket;
}

void start_api()
{
    /* API Language

    1 Byte magic + 2**16 bits length (ushort=2 bytes)
    */

    char magic_buf[MAGIC_SIZE];
    char mlength_buf[MLENGTH_SIZE];
    char message_buf[MAX_MESSAGE_LENGTH];
    Magic magic, client_CC;
    MLength mlength;
    
    std::string ip; 
    int port; 

    memset(connections_map, 0, MAX_CONNECTIONS);
    int pre_message_buffer_filled;
    while (true)
    {
        hang_until_read(STDIN_FILENO, magic_buf, MAGIC_SIZE);
        hang_until_read(STDIN_FILENO, mlength_buf, MLENGTH_SIZE);
        memcpy(&mlength, &mlength_buf, MLENGTH_SIZE); // Convert 2 Bytes to ushort
        hang_until_read(STDIN_FILENO, message_buf, mlength);
        memcpy(&magic, magic_buf, MAGIC_SIZE);
        switch (magic) // 256 Cases possible
        {
        case CONNECT:
            if (CC >= MAX_CONNECTIONS)
            {
                log_err("  Connection limit reached");
                break;
            }
            ip = strtok(message_buf, ":");
            port = atoi(strtok(NULL, ":")); 
            *connections_map[CC] = create_socket(ip, port, CC);
            api_connect(CC);
            CC += 1;
            memset(message_buf, 0, mlength);
            break;
        case DISCONNECT:
            client_CC = (Magic)mlength;
            if (client_CC > CC - 1) // We save 1 Byte by passing the connection number in the size yay
            {
                log_err("  Connection number %d is invalid", client_CC);
                break;
            }
            connections_map[client_CC]->Close();
            CC -= 1;
            connections_map[client_CC] = connections_map[CC];
            connections_map[CC] = nullptr;
            break;
        case ALLOW_CONNECT: // Need this to accept incoming messages
            client_CC = (Magic)mlength;
            if (client_CC > CC - 1 || pre_message_buffer[client_CC] == nullptr) // We save another Byte
            {
                log_err("  Connection number %d is invalid", client_CC);
                break;
            }
            pre_message_buffer_filled = strlen(pre_message_buffer[client_CC]);
            while (pre_message_buffer_filled > 0) // Send message buffer
            {
                if (pre_message_buffer_filled <= MAX_MESSAGE_LENGTH)
                {
                    memset(message_buf, 0, pre_message_buffer_filled);
                    memcpy(message_buf, pre_message_buffer[client_CC], pre_message_buffer_filled);
                    pre_message_buffer_filled = 0;
                    api_message(client_CC, message_buf);
                    memset(message_buf, 0, pre_message_buffer_filled);
                    break;
                }
                memcpy(message_buf, pre_message_buffer[client_CC], MAX_MESSAGE_LENGTH);
                pre_message_buffer_filled -= MAX_MESSAGE_LENGTH;
                api_message(client_CC, message_buf);
            }
            break;
        case CONNECTION_INFO:
            client_CC = (Magic)mlength;
            if (client_CC > CC - 1) // We save another Byte
            {
                log_info("  Connection number %d is invalid", client_CC);
                break;
            }
            ip = connections_map[client_CC]->remoteAddress().c_str(); 
            port = connections_map[client_CC]->remotePort();
            api_connection_info(client_CC, "%s:%d", ip, port);  
            break;
        default: // Send message to one of connected sockets
            if (magic == LOG_INFO || magic == LOG_ERROR)
                break; // Client should not send log messages
            if (magic > CC - 1)
            {
                printf("  Connection number %d is invalid\n", *magic_buf);
                break;
            }
            hang_until_socket_send(connections_map[magic], message_buf, mlength);
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
        // log_info("New client: [%s:%d]", newClient->remoteAddress().c_str(), newClient->remotePort());
        Magic client_CC = CC;
        api_req_connect(CC);
        CC += 1;
        pre_message_buffer[client_CC] = new char[MAX_MESSAGE_LENGTH];
        int message_buffer_filled = 0;

        newClient->onRawMessageReceived = [newClient, &message_buffer_filled, &client_CC](const char *message, int length)
        {
            if (length > MAX_MESSAGE_LENGTH) // No log just f you
                return newClient->Close();
            if (connections_map[client_CC] != nullptr) // Connection accepted
                api_message(client_CC, message);
            else
            { // Save messages to buffer while connection is not accepted
                int d = message_buffer_filled + length - MAX_MESSAGE_LENGTH;
                if (d > 0)
                {
                    log_info("Message buffer overflow from %s:%d by %d bytes", newClient->remoteAddress().c_str(), newClient->remotePort(), d);
                    // newClient->Send(make_buffer_fmt(MESSAGE_BUFFER_OVERFLOW, "Message buffer overflow by %d bytes", d));
                    newClient->Close();
                    return;
                }
                memcpy(pre_message_buffer, message, length);
                message_buffer_filled += length;
                log_info("Message from the Client %s:%d with %d bytes -> pre_message_buffer", length, newClient->remoteAddress().c_str(), newClient->remotePort());
                // TODO
                // Maybe done for now?
            }
        };

        newClient->onSocketClosed = [newClient, &client_CC](int errorCode)
        {
            log_info("Socket closed: %s:%d -> %d", newClient->remoteAddress().c_str(), newClient->remotePort(), errorCode);
            api_disconnect(client_CC);
            delete[] pre_message_buffer[client_CC];
            // TODO
        };
    };

    // Bind the server to a port.
    tcpServer.Bind(listen_port, [](int errorCode, std::string errorMessage)
                   { log_info("Binding failed: %d : %s", errorCode, errorMessage); });

    // Start Listening the server.
    tcpServer.Listen([](int errorCode, std::string errorMessage)
                     { log_info("Listening failed: %d : %s", errorCode, errorMessage); });

    log_info("TCP Server started on port %d", listen_port);

    start_api();

    // Close the server before exiting the program.
    tcpServer.Close();

    return 0;
}