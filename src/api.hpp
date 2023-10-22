#include <stdarg.h>
#include <fmt/core.h>
#include <assert.h>

/* ** API specification **
 *  The magic byte(s) encode
 *     1. The fundamental logic for API communication
 *     2. The connection numbers
 *
 *  The message length (ML) bytes encode
 *     1. The message length without the magic byte.
 *     2. Sometimes it encodes the connection number (fundamental connection logic)
 *     This is a deliberate design choice, done to save bytes.
 *
 *  A message is all the bytes following the ML. It has to be encodable by the MLENGTH.
 *  For example if MLENGTH is unsigned short, then the MAX_MESSAGE_LENGTH is 65535.
 */
namespace Api
{
#define LOG_FILENO STDOUT_FILENO
#define API_IN_FILENO STDIN_FILENO
#define API_OUT_FILENO STDOUT_FILENO

#define MagicType unsigned char
// 1 Byte of magic can hold 255 states
#define MessageLengthType unsigned short
    // 2 Bytes, encodes message length up to 65535 bytes = 64 KB

#define MAX_VAL(TYPE) (TYPE) ~0

    const char MAGIC_TYPE_SIZE = sizeof(MagicType);
    const char MESSAGE_LENGTH_TYPE_SIZE = sizeof(MessageLengthType);
    const char PREFIX_SIZE = MAGIC_TYPE_SIZE + MESSAGE_LENGTH_TYPE_SIZE;
    // Max number encoded by MessageLengthType
    const MessageLengthType MAX_MESSAGE_LENGTH = MAX_VAL(MessageLengthType);
    // Max amount of bytes to store of accepted connection
    const int MAX_FULL_MESSAGE_SIZE = MAX_MESSAGE_LENGTH + PREFIX_SIZE;
    const int MAX_PRE_MESSAGE_LENGTH = MAX_MESSAGE_LENGTH * 4;
    // Max amount of bytes to store of non-accepted connection
    // const int PRE_MESSAGE_BUFFER_SIZE = MAX_MESSAGE_LENGTH * 3;

    /*
    const Magic DISCONNECT = MAX_VAL(MagicType);
    const Magic CONNECT = DISCONNECT - 1;
    const Magic LOG_INFO = CONNECT - 1;
    const Magic REQ_CONNECT = LOG_INFO - 1;
    const Magic ALLOW_CONNECT = REQ_CONNECT - 1;
    const Magic MESSAGE_BUFFER_OVERFLOW = ALLOW_CONNECT - 1;
    const Magic UPDATE_CONNECTION = MESSAGE_BUFFER_OVERFLOW - 1;
    const Magic CONNECTION_INFO = UPDATE_CONNECTION - 1;
    const Magic LOG_ERROR = CONNECTION_INFO - 1;
    const Magic MAX_CONNECTIONS = CONNECTION_INFO - 1;
    */

    enum Magic
    {
        DISCONNECT = MAX_VAL(MagicType),
        CONNECT = DISCONNECT - 1,

        REQUEST_CONNECT = CONNECT - 1,
        ACCEPT_CONNECT = REQUEST_CONNECT - 1,
        CREATE_CONNECT = ACCEPT_CONNECT - 1,

        LOG_INFO = CREATE_CONNECT - 1,
        LOG_ERROR = LOG_INFO - 1,

        MAX_CONNECTIONS = LOG_ERROR - 1
    };

    int hang_until_write_len(int fd, char *buf, int len)
    {
        int m = write(fd, buf, len);
        int d = len - m;
        while (d > 0)
        {
            buf += m;
            m = write(fd, buf, d);
            d -= m;
        }
        return len;
    }
    int hang_until_write(int fd, char *buf) { return hang_until_write_len(fd, buf, strlen(buf)); }

    // Api out calls
    int api_req_connect(MagicType cn) { return hang_until_write(API_OUT_FILENO, make_buffer_special(Magic::REQUEST_CONNECT, cn)); }

    // Log calls
    template <typename... T>
    inline int log(MagicType log, char *fmt, T &&...args)
    {
        char buf[MAX_MESSAGE_LENGTH];
        int n = fmt::format_to_n(buf, MAX_MESSAGE_LENGTH, fmt, args...);
        assert(n <= MAX_MESSAGE_LENGTH);
        return hang_until_write_len(API_OUT_FILENO, make_buffer(log, buf, n), n);
    }
    template <typename... T>
    inline int log_info(char *fmt, T &&...args) { return log(LOG_INFO, fmt, args...); }
    template <typename... T>
    inline int log_error(char *fmt, T &&...args) { return log(LOG_ERROR, fmt, args...); }

    // Api calls
    inline int api(MagicType connId, const char *message, MessageLengthType length) { return hang_until_write_len(API_OUT_FILENO, make_buffer(connId, message, length), length + PREFIX_SIZE); }
    inline int api_message(MagicType connId, const char *message, MessageLengthType length) { return api(connId, message, length); }

    inline int api_special(MagicType mag, MagicType mag_as_message_length) { return hang_until_write_len(API_OUT_FILENO, make_buffer_special(mag, mag_as_message_length), PREFIX_SIZE); }
    inline int api_create_connect(MagicType connId) { return api_special(Magic::CREATE_CONNECT, connId); }

    // Make buffer methods
    char *make_buffer_special(MagicType mag, MagicType mag_as_message_length)
    {
        char prefix_buffer[PREFIX_SIZE];
        memcpy(prefix_buffer, &mag, MAGIC_TYPE_SIZE);
        (*prefix_buffer) += MAGIC_TYPE_SIZE;
        memcpy(prefix_buffer, &mag_as_message_length, MESSAGE_LENGTH_TYPE_SIZE);
        return prefix_buffer;
    }

    char *make_buffer(MagicType mag, char *message_buffer, MessageLengthType message_length)
    {
        if (message_length > MAX_MESSAGE_LENGTH)
        {
            perror("[make_buffer] message length is bigger than " + MAX_MESSAGE_LENGTH);
            exit(1);
        }
        char full_message_buffer[PREFIX_SIZE + message_length];
        memcpy(full_message_buffer, &mag, MAGIC_TYPE_SIZE);
        (*full_message_buffer) += MAGIC_TYPE_SIZE;
        memcpy(full_message_buffer, &message_length, MESSAGE_LENGTH_TYPE_SIZE);
        (*full_message_buffer) += MESSAGE_LENGTH_TYPE_SIZE;
        memcpy(full_message_buffer, message_buffer, message_length);
        return full_message_buffer;
    }
}

void hang_until_read(int fd, char *buf, int len)
{
    int m = read(fd, buf, len);
    int d = len - m;
    while (d > 0)
    {
        buf += m;
        m = read(fd, buf, d);
        d -= m;
    }
}

void hang_until_socket_send(TCPSocket<> *socket, char *buf, int len)
{
    int m = socket->Send(buf, len);
    int d = len - m;
    while (d > 0)
    {
        buf += m;
        m = socket->Send(buf, d);
        d -= m;
    }
}

/*
char *make_buffer_fmt(Magic magic, const char *fmt, va_list va)
{
    // Read function args ...
    va_list va2;
    va_copy(va2, va);
    MLength mlength = vsnprintf(nullptr, 0, fmt, va) + 1;
    if (mlength > MAX_MESSAGE_LENGTH)
    {
        perror("[make_buffer_fmt] message length is bigger than " + MAX_MESSAGE_LENGTH);
        exit(1);
    }
    char formatted_buf[mlength];
    vsnprintf(formatted_buf, sizeof(formatted_buf), fmt, va2);
    // va_end(va);
    va_end(va2);

    return make_buffer(magic, formatted_buf, mlength);
    // char mlength_buf[MLENGTH_SIZE];
    // memcpy(mlength_buf, &mlength, MLENGTH_SIZE); // Convert ushort to 2 Bytes
    // Magic magic_buf[MAGIC_SIZE];
    // magic_buf[0] = magic;
    // char *buffer = new char[MAGIC_SIZE + MLENGTH_SIZE + mlength];
    // memcpy(buffer, magic_buf, MAGIC_SIZE);
    // (*buffer) += MAGIC_SIZE;
    // memcpy(buffer, mlength_buf, MLENGTH_SIZE);
    // (*buffer) += MLENGTH_SIZE;
    // memcpy(buffer, formatted_buf, mlength);
    // return buffer;
}

template <typename... Arrr>
char *make_buffer_fmt_prepend(Magic magic, char *mprepend, MLength mprepend_length, const char *fmt, Arrr... args)
{
    if (mprepend_length > MAX_MESSAGE_LENGTH)
    {
        perror("[make_nbuffer] mprepend is bigger than " + MAX_MESSAGE_LENGTH);
        exit(1);
    }

    va_list va;
    va_start(va, fmt);

    va_list va2;
    va_copy(va2, va);
    MLength formatted_length = vsnprintf(nullptr, 0, fmt, va) + 1;
    MLength mlength = formatted_length + mprepend_length;
    if (mlength > MAX_MESSAGE_LENGTH)
    {
        perror("[make_buffer_fmt_prepend] message length is bigger than " + MAX_MESSAGE_LENGTH);
        exit(1);
    }
    char formatted_buf[mlength];
    vsnprintf(formatted_buf, formatted_length, fmt, va2);
    va_end(va);
    va_end(va2);

    (*formatted_buf) += mprepend_length;
    memcpy(formatted_buf, mprepend, mprepend_length);

    return make_buffer(magic, formatted_buf, mlength);
    // return make_buffer(magic, formatted_buf, mlength);

    // char mlength_buf[MLENGTH_SIZE];
    // memcpy(mlength_buf, &mlength, MLENGTH_SIZE); // Convert ushort to 2 Bytes
    // Magic magic_buf[MAGIC_SIZE];
    // magic_buf[0] = magic; // Log message magic byte
    // char *buffer = new char[MLENGTH_SIZE + formatted_length];
    // memcpy(buffer, magic_buf, MAGIC_SIZE);
    // (*buffer) += MAGIC_SIZE;
    // memcpy(buffer, mlength_buf, MLENGTH_SIZE);
    // (*buffer) += MLENGTH_SIZE;
    // memcpy(buffer, mprepend, mprepend_length);
    // (*buffer) += mprepend_length;
    // memcpy(buffer, formatted_buf, formatted_length);
    // return buffer;
}



int write_fmt(int fd, Magic magic, const char *fmt, va_list va)
{
    char *buf = make_buffer_fmt(magic, fmt, va);
    return hang_until_write(fd, buf, strlen(buf));
}

int log_info(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    int ret = write_fmt(LOG_FILENO, LOG_INFO, fmt, va);
    va_end(va);
    return ret;
}
int log_err(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    int ret = write_fmt(LOG_FILENO, LOG_ERROR, fmt, va);
    va_end(va);
    return ret;
}

int api_message(Magic connection, const char *message) { return write(API_OUT_FILENO, connection, message); }
template <typename... Arrr>
int api_message_fmt(Magic connection, const char *fmt, Arrr... args) { return write_fmt(API_OUT_FILENO, connection, fmt, std::forward<Arrr>(args)...); }
int api_special(Magic magic, Magic magic_as_mlength)
{
    char prefix_buffer[PREFIX_SIZE];
    memcpy(prefix_buffer, &magic, MAGIC_SIZE);
    (*prefix_buffer) += MAGIC_SIZE;
    MLength size = (MLength)magic_as_mlength;
    memcpy(prefix_buffer, &size, MLENGTH_SIZE);
    return hang_until_write(API_OUT_FILENO, prefix_buffer, PREFIX_SIZE);
}
int api_connect(Magic connection) { return api_special(CONNECT, connection); }
int api_disconnect(Magic connection) { return api_special(DISCONNECT, connection); }
int api_req_connect(Magic connection) { return api_special(REQ_CONNECT, connection); }
template <typename... Arrr>
int api_connection_info(Magic connection, const char *fmt, Arrr... args)
{
    char mprepend[1] = {(char)connection};
    char *buf = make_buffer_fmt_prepend(CONNECTION_INFO, mprepend, 1, fmt, std::forward<Arrr>(args)...);
    return hang_until_write(API_OUT_FILENO, buf, strlen(buf));
}
 */