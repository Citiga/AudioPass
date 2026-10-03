#pragma once

namespace bv {

// Catches an access violation inside a plugin probe. The function is noinline
// so the __try block never has to unwind C++ objects in the caller.
int invokeSeh(int (*fn)(void*), void* user);

} // namespace bv
