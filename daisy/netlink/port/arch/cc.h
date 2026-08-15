#ifndef NEON_NETLINK_ARCH_CC_H
#define NEON_NETLINK_ARCH_CC_H

/* lwIP compiler/platform glue for arm-none-eabi-gcc on the STM32H750.
 * lwIP 2.x autodetects most of this from GCC; only diagnostics land
 * here. Failures print to nowhere in a release build — there is no
 * console on this hardware — so LWIP_PLATFORM_ASSERT parks in a loop a
 * debugger can find. */

#include <stdint.h>

#define LWIP_PLATFORM_DIAG(x)
#define LWIP_PLATFORM_ASSERT(x) \
  do { \
    for (;;) { \
    } \
  } while (0)

#define LWIP_NO_INTTYPES_H 0

#endif /* NEON_NETLINK_ARCH_CC_H */
