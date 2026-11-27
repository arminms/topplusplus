// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <cerrno>
#include <pwd.h>
#include <string>
#include <unordered_map>
#include <unistd.h>
#include <vector>

namespace sysproc::detail {

inline
std::string uid_to_username
(   uid_t uid
,   std::unordered_map<uid_t
,   std::string>& cache
)
{   if (auto it = cache.find(uid); it != cache.end())
        return it->second;

    const long suggested_size = sysconf(_SC_GETPW_R_SIZE_MAX);
    std::vector<char> buffer
    (   suggested_size > 0
    ?   static_cast<std::size_t>(suggested_size)
    :   1024
    );
    struct passwd entry{};
    struct passwd* result = nullptr;
    int error = 0;
    while
    (   (   error
        =   getpwuid_r(uid, &entry, buffer.data(), buffer.size(), &result)
        )  == ERANGE
    )   buffer.resize(buffer.size() * 2);

    auto name
    =   error == 0 && result && result->pw_name
    ?   std::string(result->pw_name)
    :   std::to_string(uid)
    ;
    cache.emplace(uid, name);
    return name;
}

} // namespace sysproc::detail
