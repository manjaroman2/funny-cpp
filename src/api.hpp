#include "tcpsocket.hpp"
#include <stdarg.h>

#define MAGIC_TYPE unsigned char // 1 Byte
#define SIZE_TYPE unsigned short // 2 Bytes, encodes messages up to 65535 bytes = 64 KB

const char MAGIC_BYTES = sizeof(MAGIC_TYPE);
const char SIZE_BYTES = sizeof(SIZE_TYPE);
const char PREFIX_BYTES = MAGIC_BYTES + SIZE_BYTES;
const SIZE_TYPE MAX_MESSAGE_SIZE = (SIZE_TYPE) 65536;              // Max number encoded by SIZE_TYPE
const int MAX_FULL_MESSAGE_SIZE = MAX_MESSAGE_SIZE + PREFIX_BYTES; // Max amount of bytes to store of accepted connection
const int PRE_MESSAGE_BUFFER_SIZE = MAX_FULL_MESSAGE_SIZE * 3;     // Max amount of bytes to store of non-accepted connection

const MAGIC_TYPE DISCONNECT = 255;  
const MAGIC_TYPE CONNECT = DISCONNECT - 1;
const MAGIC_TYPE LOG = CONNECT - 1;
const MAGIC_TYPE REQ_CONNECT = LOG - 1;
const MAGIC_TYPE ALLOW_CONNECT = REQ_CONNECT - 1;
const MAGIC_TYPE MESSAGE_BUFFER_OVERFLOW = ALLOW_CONNECT - 1;
const MAGIC_TYPE UPDATE_CONNECTION = MESSAGE_BUFFER_OVERFLOW - 1;
const MAGIC_TYPE CONNECTION_INFO = UPDATE_CONNECTION - 1;

const MAGIC_TYPE MAX_CONNECTIONS = CONNECTION_INFO - 1;

#define LOG_FILENO STDOUT_FILENO
#define API_IN_FILENO STDIN_FILENO
#define API_OUT_FILENO STDOUT_FILENO

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

/*
    1 Byte magic + 2 Bytes length + MAX_READ_SIZE Bytes message
*/
char *make_buffer(MAGIC_TYPE magic, const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);

    va_list va2;
    va_copy(va2, va);
    int r = vsnprintf(nullptr, 0, fmt, va) + 1;
    if (r > MAX_MESSAGE_SIZE)
    {
        perror("[write_api] format string result is bigger than " + MAX_MESSAGE_SIZE);
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
    char *buffer = new char[PREFIX_BYTES + r];
    memcpy(buffer, magic_buf, MAGIC_BYTES);
    memcpy(buffer + MAGIC_BYTES, message_size_buf, SIZE_BYTES);
    memcpy(buffer + PREFIX_BYTES, buf, r);
    return buffer;
}

int _write(int fd, MAGIC_TYPE magic, const char *fmt, ...)
{
    char *buffer = make_buffer(magic, fmt);
    int r = strlen(buffer);
    hang_until_write(fd, buffer, PREFIX_BYTES + r);
    return PREFIX_BYTES + r;
}

int write_connect(MAGIC_TYPE connection) {
    char* buffer = new char[PREFIX_BYTES];
    memcpy(buffer, &CONNECT, MAGIC_BYTES);
    buffer += MAGIC_BYTES;
    SIZE_TYPE size = (SIZE_TYPE) connection;
    memcpy(buffer, &size, SIZE_BYTES);
    return hang_until_write(API_OUT_FILENO, buffer, PREFIX_BYTES);
}
int write_log(const char *fmt, ...) { return _write(LOG_FILENO, LOG, fmt); }
int write_api(MAGIC_TYPE magic, const char *fmt, ...) { return _write(API_OUT_FILENO, magic, fmt); }

int write_close(MAGIC_TYPE connection) { return write_api(DISCONNECT, "%d", connection); }
int write_req_connect(MAGIC_TYPE connection) { 
    char *buffer = new char[PREFIX_BYTES];
    memcpy(buffer, &REQ_CONNECT, MAGIC_BYTES);
    buffer += MAGIC_BYTES;
    SIZE_TYPE size = (SIZE_TYPE) connection;
    memcpy(buffer, &size, SIZE_BYTES);
    return hang_until_write(API_OUT_FILENO, buffer, PREFIX_BYTES);
}
int write_msg(MAGIC_TYPE connection, const char *fmt, ...) { return write_api(connection, fmt); }
