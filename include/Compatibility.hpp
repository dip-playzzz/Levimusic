#pragma once
#include "Platform.hpp"

namespace compatibility {

// Preloader build the ABI was last verified against (see compat/verified-target.json).
// Runtime acceptance is ABI/export based, not build-id based, so compatible
// preloader updates keep working; a mismatch only changes what the log says.
inline constexpr const char* VerifiedPreloaderBuildId =
    "5790e73466e108098540d65efd7898e2c1a0490a";

// Validates only the stable exports actually used by LeviMusic.
bool validateRuntime(void* preloader, const char*& missingSymbol);

} // namespace compatibility
