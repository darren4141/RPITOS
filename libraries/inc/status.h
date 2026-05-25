#ifndef STATUS_H
#define STATUS_H

typedef enum {
  E_OK                 = 0,
  E_INVALID_ARGS       = 1,
  E_OUT_OF_MEM         = 2,
  E_RESOURCE_EXHAUSTED = 3
} StatusCode;

#endif
