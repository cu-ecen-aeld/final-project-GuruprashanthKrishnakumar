#ifndef UART_DRIVER_H
#define UART_DRIVER_H

ssize_t uart_receive(char *buf, size_t size);
ssize_t uart_send(char *buf, size_t size);

#endif