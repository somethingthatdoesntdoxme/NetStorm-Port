#pragma once
// Minimal PE resource reader -- just enough to pull bitmaps (and their
// palettes) out of the user's own netstorm.game without shipping any assets.
#include "core/BinaryFile.h"

namespace ns {

// Returns the raw resource data for (type, id), first language found.
std::optional<Bytes> peResource(const Bytes& pe, uint32_t type, uint32_t id);

}  // namespace ns
