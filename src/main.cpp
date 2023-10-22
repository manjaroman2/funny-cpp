#include "tcpserver.hpp"
#include "main.hpp"
#include <algorithm>
#include <ranges>

namespace Api
{

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
            PreConnection connection = PreConnection(newClient->remoteAddress().c_str(), newClient->remotePort());
            api_req_connect(connection.create());
            newClient->onRawMessageReceived = [newClient, &connection](const char *message, int length)
            {
                if (length > MAX_MESSAGE_LENGTH) // Incoming message is too long, abort
                    return newClient->Close();
                // Connection accepted
                if (connection.accepted())
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

            newClient->onSocketClosed = [newClient, &connection](int errorCode)
            {
                log_info(
                    "Socket closed: %s:%d -> %d",
                    newClient->remoteAddress().c_str(),
                    newClient->remotePort(),
                    errorCode);

                delete &connection;

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

    // Start while(true) loop
    void start_api()
    {
        char magicBuffer[MAGIC_TYPE_SIZE];
        char messageLengthBuffer[MESSAGE_LENGTH_TYPE_SIZE];
        char messageBuffer[MAX_MESSAGE_LENGTH];
        MessageLengthType messageLength;
        MagicType magic, connId;

        PreConnection *preConnection;
        Connection *connection;

        std::string ip;
        int port;
        while (true)
        {
            hang_until_read(STDIN_FILENO, magicBuffer, MAGIC_TYPE_SIZE);
            hang_until_read(STDIN_FILENO, messageLengthBuffer, MESSAGE_LENGTH_TYPE_SIZE);
            // Convert 2 Bytes to ushort
            memcpy(&messageLength, &messageLengthBuffer, MESSAGE_LENGTH_TYPE_SIZE);
            hang_until_read(STDIN_FILENO, messageBuffer, messageLength);
            memcpy(&magic, magicBuffer, MAGIC_TYPE_SIZE);
            switch (magic)
            {
            case Magic::CONNECT:
                connectionsLock.lock();
                if (connections.size() == MAX_CONNECTIONS)
                {
                    connectionsLock.unlock();
                    log_error("  Connection limit reached");
                    break;
                }
                connectionsLock.unlock();
                ip = strtok(messageBuffer, ":");
                port = atoi(strtok(NULL, ":"));
                preConnection = &PreConnection(ip, port);
                preConnection->create();
                api_create_connect(preConnection->getId());
                break;
            case Magic::DISCONNECT:
                connId = (MagicType)messageLength;
                connectionsLock.lock();
                if (connId > connections.size() - 1)
                {
                    connectionsLock.unlock();
                    log_error("  Connection %d is invalid", connId);
                    break;
                }
                if (connId < connections.size() - 1)
                    connections[connId].swap(connections.back());
                delete connections.back();
                connectionsLock.unlock();
                break;
            case Magic::ACCEPT_CONNECT: // Need this to accept incoming messages
                connId = (MagicType)messageLength;
                connectionsLock.lock();
                if (connId > connections.size() - 1)
                {
                    connectionsLock.unlock();
                    log_error("  Connection %d is invalid", connId);
                    break;
                }
                if (connections[connId].accepted())
                {
                    connectionsLock.unlock();
                    log_error("  Connection %d was already accepted", connId);
                    break;
                }
                preConnection = &connections[connId];
                connectionsLock.unlock();

                preConnection->iteratePreMessageBufferChunks([&connId](char *iter, MessageLengthType length)
                                                             { api_message(connId, iter, length); });
                break;
            case Magic::LOG_INFO || Magic::LOG_ERROR:
                // Client should not send log messages
                break;
            default: // Send message to one of connected sockets
                connId = magic;
                connectionsLock.lock();
                if (connId > connections.size() - 1)
                {
                    connectionsLock.unlock();
                    log_error("  Connection %d is invalid", connId);
                    break;
                }
                if (connections[connId]->isAccepted())
                {
                    connectionsLock.unlock();
                    log_error("  Connection %d is not accepted", connId);
                    break;
                }
                connection = &connections[connId];
                connectionsLock.unlock();
                connection->sendMessage(messageBuffer, messageLength);
                break;
            }
            // case CONNECTION_INFO:
            //     client_CC = (Magic)mlength;
            //     if (client_CC > CC - 1) // We save another Byte
            //     {
            //         log_info("  Connection number %d is invalid", client_CC);
            //         break;
            //     }
            //     ip = connections_map[client_CC]->remoteAddress().c_str();
            //     port = connections_map[client_CC]->remotePort();
            //     api_connection_info(client_CC, "%s:%d", ip, port);
            //     break;
        }
    }
}
