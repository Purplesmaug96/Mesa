/*
 * Public GL entry points that route through the glapi dispatch table, so that
 * clients linking statically against the xbox360 backend can call gl* directly
 * (matches how softpipe/classic GL exposes the legacy API).
 *
 * SPDX-License-Identifier: MIT
 */

#include "glapi/glapi_priv.h"

#define MAPI_TMP_PUBLIC_ENTRIES_NO_HIDDEN
#include "glapi_public.h"