/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OPLUS_NANDSWAP_H_
#define _OPLUS_NANDSWAP_H_

#include <linux/types.h>

#if IS_ENABLED(CONFIG_OPLUS_NANDSWAP)
bool oplus_nandswap_is_enabled(void);
#else
static inline bool oplus_nandswap_is_enabled(void) { return false; }
#endif

#endif /* _OPLUS_NANDSWAP_H_ */
