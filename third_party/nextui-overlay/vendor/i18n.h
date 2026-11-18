#ifndef __I18N_H__
#define __I18N_H__

#include "ui_overlay.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Translate an English source string to the active UI language.
//
// The English string is the lookup key. When the active language is English,
// or when the Chinese table has no entry for the key, the key itself is
// returned unchanged. Callers should therefore always pass the exact English
// text they want to display.
//
// Keys are static string literals; the missing-translation log keeps pointers
// to them and does not copy the text.
const char* I18N_translate(const char* key);

/* A pak with its own translation table (LedControl) defines TR before including
 * this header so its call sites keep using that table. */
#ifndef TR
#define TR(key) I18N_translate(key)
#endif

/*
 * The shared UIOverlay's default copy, translated for the active language.
 * Apps pass this to FnBridgeConfig.overlay_strings; the returned struct only
 * holds pointers, and those point into static storage, so a local copy may
 * live for the rest of the process.
 */
UIOverlayStrings I18N_overlayStrings(void);

#ifdef __cplusplus
}
#endif

#endif
