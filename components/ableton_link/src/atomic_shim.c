// Xtensa newlib/gcc used by ESP-IDF does not always ship libatomic.
// Ableton Link references __atomic_is_lock_free (visible when SPIRAM is
// on and more C++ atomics get instantiated). Provide a minimal shim.
//
// Signature must match the GCC builtin: _Bool(unsigned int, const volatile void*).

#include <stdbool.h>

bool __atomic_is_lock_free(unsigned int size, const volatile void* ptr) {
  (void)ptr;
  return size <= sizeof(void*);
}
