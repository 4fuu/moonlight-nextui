/* Apostrophe implementation unit for the host tests.
 *
 * apostrophe.h is header-only: exactly one translation unit in a program must
 * define AP_IMPLEMENTATION. main.c does that for the app; the tests need their
 * own so they can link moonlight_cli.c without the app's main(). */

#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_HOST_IMPLEMENTATION
#include "apostrophe_host.h"
