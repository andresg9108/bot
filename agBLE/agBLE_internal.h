#ifndef AGBLE_INTERNAL_H
#define AGBLE_INTERNAL_H

#include <stdbool.h>

/* The NimBLE host is a singleton: central and peripheral modes cannot own it
 * independently. */
bool agble_owner_claim(void);
void agble_owner_release(void);

#endif
