#include "tcpsocket.hpp"
#include <stdarg.h>
#include <fmt/core.h>
#include <fmt/color.h>
#include <magic_enum_all.hpp>

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
#define MAGIC_TYPE unsigned char    // 1 Byte -> 255 Magic Bytes
#define MLEN_TYPE unsigned short      // 2 Bytes, encodes message length up to 65535 bytes = 64 KB

#define LOG_FILENO STDOUT_FILENO
#define API_IN_FILENO STDIN_FILENO
#define API_OUT_FILENO STDOUT_FILENO

#define MAX_VAL(TYPE) (TYPE) ~0

constexpr char MAGIC_SIZE = sizeof(MAGIC_TYPE);               // 
constexpr char MLEN_SIZE = sizeof(MLEN_TYPE);                 // Bytes
constexpr char PREFIX_SIZE = MAGIC_SIZE + MLEN_SIZE;          // 
constexpr MLEN_TYPE MAX_MESSAGE_LENGTH = MAX_VAL(MLEN_TYPE);            // Max number encoded by MLength  
constexpr int MAX_FULL_MESSAGE_SIZE = MAX_MESSAGE_LENGTH + PREFIX_SIZE; // Max amount of bytes to store of accepted connection
constexpr int PRE_MESSAGE_BUFFER_SIZE = MAX_MESSAGE_LENGTH * 3;         // Max amount of bytes to store of non-accepted connection

enum class MagicBytes { 
    /* Connection magic */
    DISCONNECT = MAX_VAL(MAGIC_TYPE),
    CONNECT = DISCONNECT - 1, 
    CONNECT_INFO = CONNECT - 1,  
    REQ_CONNECT = CONNECT_INFO - 1,
    ACCEPT_CONNECT = REQ_CONNECT - 1,
    UPDATE_CONNECT = ACCEPT_CONNECT - 1,
    /* Log magic */
    LOG_INFO = UPDATE_CONNECT - 1, 
    LOG_ERROR = LOG_INFO - 1,
}; 

constexpr MAGIC_TYPE MAX_CONNECTIONS = magic_enum::enum_count<MagicBytes>();

/* 
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

int hang_until_write(int fd, char *buf, int len)
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

char *make_buffer(Magic magic, char *message_buffer, MLength mlength)
{
    if (mlength > MAX_MESSAGE_LENGTH)
    {
        perror("[make_buffer] message length is bigger than " + MAX_MESSAGE_LENGTH);
        exit(1);
    }
    char *buffer = new char[MAGIC_SIZE + MLENGTH_SIZE + mlength];
    memcpy(buffer, &magic, MAGIC_SIZE);
    (*buffer) += MAGIC_SIZE;
    memcpy(buffer, &mlength, MLENGTH_SIZE);
    (*buffer) += MLENGTH_SIZE;
    memcpy(buffer, message_buffer, mlength);
    return buffer;
}

char *make_buffer_fmt(MagicBytes magic , const char *fmt, va_list va) 
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

int write(int fd, Magic magic, const char *message)
{
    char *buf = make_buffer(magic, (char *)message, strlen(message));
    return hang_until_write(fd, buf, strlen(buf));
}

int write_fmt(int fd, Magic magic, const char *fmt, va_list va) 
{
    char *buf = make_buffer_fmt(magic, fmt, va);  
    return hang_until_write(fd, buf, strlen(buf));
}
*/ 

template<typename ...Args>
char* write_fmt(int fd, MagicBytes magic, const char* fmt, Args ...args) {
    fmt::format_to_n()
}

template<typename ...Args> 
int log_info(const char *fmt, Args ...args) { 
    fmt::format_to_n()
    va_list va;
    va_start(va, fmt);
    int ret = write_fmt(LOG_FILENO, LOG_INFO, fmt, va); 
    va_end(va);
    return ret;
} 
int log_err(const char *fmt, ...) { 
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
