#include "api.hpp";
#include "tcpsocket.hpp"
#include <mutex>
#include <functional>

namespace Api
{
    std::array<Connection, MAX_CONNECTIONS>
        connections;
    std::mutex connectionsLock;

    class Connection
    {
    private:
        MagicType id;
        std::mutex id_lock;
        std::string ip;
        int port;

    public:
        TCPSocket<> *socket;
        Connection(std::string ip, int port)
        {
            this->ip = ip;
            this->port = port;
        }
        ~Connection()
        {
            socket->Close();
        }
        void createSocket()
        {
            socket = &TCPSocket<>([](int errorCode, std::string errorMessage)
                                  { log_info("Socket creation error: %d : %s", errorCode, errorMessage); });

            socket->onRawMessageReceived = [this](const char *message, int length)
            {
                api_message(getId(), message, length);
            };

            socket->onSocketClosed = [this](int errorCode)
            {
                // api_disconnect(connection);
            };

            socket->Connect(
                ip, port, [this]
                { this -
                      api_connect(connection); },
                [this](int errorCode, std::string errorMessage)
                {
                    api_disconnect(connection);
                    // log_info("Connection failed: %d : %s", errorCode, errorMessage);
                });
        }
        // Returns the connection id
        MagicType create()
        {
            connectionsLock.lock();
            setId(connections.size());
            connections[id] = this;
            connectionsLock.unlock();
            return id;
        }
        void setId(MagicType newId)
        {
            id_lock.lock();
            id = newId;
            id_lock.unlock();
        }
        MagicType getId()
        {
            MagicType idCopy;
            id_lock.lock();
            idCopy = id;
            id_lock.unlock();
        }
        bool isAccepted() { return true; }

        void sendMessage(char *messageBuffer, MessageLengthType messageLength)
        {
            hang_until_socket_send(socket, messageBuffer, messageLength);
        }
    };

    class PreConnection : public Connection
    {
    private:
        bool accepted = false;
        std::mutex acceptedLock;

    public:
        std::array<char, MAX_PRE_MESSAGE_LENGTH> preMessageBuffer;
        bool isAccepted()
        {
            bool acceptedCopy;
            acceptedLock.lock();
            acceptedCopy = accepted;
            acceptedLock.unlock();
            return acceptedCopy;
        }

        void accept()
        {
            acceptedLock.lock();
            accepted = true;
            acceptedLock.unlock();
        }

        template <typename Func>
        void iteratePreMessageBufferChunks(Func func)
        {
            size_t size = preMessageBuffer.size();
            char *iter = preMessageBuffer.begin();
            if (size > MAX_MESSAGE_LENGTH)
            {
                for (; iter < size - MAX_MESSAGE_LENGTH; iter += MAX_MESSAGE_LENGTH)
                {
                    func(iter, MAX_MESSAGE_LENGTH);
                }
            }
            if (iter % MAX_MESSAGE_LENGTH)
            {
                func(iter, iter % MAX_MESSAGE_LENGTH);
            }
        }
    };
    // template <class T, class Func>
    // void do_chunks(T container, size_t K, Func func)
    // {
    //     size_t size = container.size();
    //     size_t i = 0;

    //     // do we have more than one chunk?
    //     if (size > K)
    //     {
    //         // handle all but the last chunk
    //         for (; i < size - K; i += K)
    //         {
    //             func(container, i, i + K);
    //         }
    //     }

    //     // if we still have a part of a chunk left, handle it
    //     if (i % K)
    //     {
    //         func(container, i, i + i % K);
    //     }
    // }
}
