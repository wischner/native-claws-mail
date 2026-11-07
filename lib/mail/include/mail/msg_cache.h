//
// The per-folder summary files. The formats are Claws': .claws_cache
// holds the summaries and .claws_mark the flags, little endian, so a
// folder keeps its read state whichever program last touched it.
//
// Derived from Claws Mail src/msgcache.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef MAIL_MSG_CACHE_H
#define MAIL_MSG_CACHE_H

#include <cstdint>
#include <filesystem>
#include <map>

#include "mail/msg_info.h"

namespace mail {

// A folder's summaries by message number.
using msg_map = std::map<std::uint32_t, msg_info>;

//
// Read a folder's summaries.
//
// Parameters:
//      directory   - Folder directory holding the files.
//
// Notes:
//      A missing, corrupt or wrong-version cache reads as empty; a scan
//      then rebuilds it from the messages, so this cannot fail. Flags
//      from the mark file are applied to what the cache holds.
//
msg_map read_msg_cache(const std::filesystem::path &directory);

// Write a folder's summaries; returns whether both files were written.
bool write_msg_cache(const std::filesystem::path &directory,
                     const msg_map &messages);

} // namespace mail

#endif
