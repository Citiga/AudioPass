#include "Seh.h"

#include <windows.h>

namespace bv {

int invokeSeh(int (*fn)(void*), void* user) {
    __try {
        return fn(user);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

} // namespace bv
