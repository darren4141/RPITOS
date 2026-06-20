#ifndef STATUS_H
#define STATUS_H

typedef enum {
  E_OK                 = 0,
  E_INVALID_ARGS       = 1,
  E_OUT_OF_MEM         = 2,
  E_RESOURCE_EXHAUSTED = 3,
  E_EMPTY              = 4,
  E_TIMED_OUT          = 5,
  E_CORRUPTED          = 6,
  E_ABORTED            = 7,
  E_CMD                = 8,
  E_DATA               = 9,
  E_CRC                = 10,
  E_NOTSUPP            = 11,
} StatusCode;

// Requires uart.h to be included before use.
#define STATUS_OK_OR_WARN(expr)                              \
        do {                                                       \
          StatusCode _sc = (expr);                                 \
          if (_sc != E_OK) {                                       \
            uart_print("WARN: " #expr " returned non-OK\r\n");    \
          }                                                        \
        } while (0)

#endif
