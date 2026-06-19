#ifndef BOOT_H
#define BOOT_H

#include "status.h"

StatusCode boot_init();
StatusCode boot_validateApp();
StatusCode boot_loadApp();
void boot_jumpToApp();

#endif
