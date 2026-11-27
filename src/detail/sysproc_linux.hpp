// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#include <hwinfo/computer.h>

#include "sysproc_posix.hpp"

namespace sysproc {
namespace detail {

inline
process_state map_linux_state(char s)
{   switch (s)
    {   case 'R': return process_state::running;
        case 'S': return process_state::sleeping;
        case 'D': return process_state::disk_sleep;
        case 'T':
        case 't': return process_state::stopped;
        case 'Z': return process_state::zombie;
        case 'I': return process_state::idle;
        default:  return process_state::unknown;
    }
}

inline
std::vector<std::string> parse_linux_cmdline
(   const std::filesystem::path& cmdline_path
)
{   std::vector<std::string> args;
    std::ifstream file(cmdline_path, std::ios::binary);
    if (!file) return args;

    std::string raw
    (   (std::istreambuf_iterator<char>(file))
    ,   std::istreambuf_iterator<char>()
    );
    if (raw.empty()) return args;

    size_t start = 0;
    while (start < raw.size())
    {   size_t end = raw.find('\0', start);
        if (end == std::string::npos)
        {   args.emplace_back(raw.substr(start));
            break;
        }
        args.emplace_back(raw.substr(start, end - start));
        start = end + 1;
    }
    return args;
}

inline
void parse_linux_io
(   const std::filesystem::path& io_path
,   uint64_t& read_bytes
,   uint64_t& write_bytes
)
{   std::ifstream file(io_path);
    if (!file) return;

    std::string key;
    uint64_t val = 0;
    while (file >> key >> val)
    {   if (key == "read_bytes:")
            read_bytes = val;
        else if (key == "write_bytes:")
            write_bytes = val;
    }
}

} // namespace detail

inline
std::vector<process_info> list_processes()
{   std::vector<process_info> result;
    namespace fs = std::filesystem;

    static const long clk_tck = sysconf(_SC_CLK_TCK);
    static const long page_size = sysconf(_SC_PAGESIZE);
    const auto computer = hwinfo::computer();
    std::unordered_map<uid_t, std::string> uid_cache;

    for (const auto& entry : fs::directory_iterator("/proc"))
    {   std::error_code ec;
        if (!entry.is_directory(ec)) continue;

        const std::string dir_name = entry.path().filename().string();
        if
        (   dir_name.empty()
        ||  !std::all_of(dir_name.begin(), dir_name.end(), [](unsigned char c)
            { return std::isdigit(c); })
        )
        {   continue;
        }

        uint32_t pid = 0;
        const auto [end, error]
        =   std::from_chars
            (   dir_name.data()
            ,   dir_name.data() + dir_name.size()
            ,   pid
            );
        if
        (   error != std::errc{}
        ||  end != dir_name.data() + dir_name.size()
        )
        {   continue;
        }

        const fs::path proc_dir = entry.path();

        struct stat st{};
        std::string username;
        if (stat(proc_dir.c_str(), &st) == 0)
        {   username = detail::uid_to_username(st.st_uid, uid_cache);
        }

        std::ifstream stat_file(proc_dir / "stat");
        std::string line;
        if (!std::getline(stat_file, line)) continue;

        const size_t open_paren = line.find('(');
        const size_t close_paren = line.rfind(')');
        if
        (open_paren == std::string::npos
        ||  close_paren == std::string::npos
        ||  close_paren <= open_paren
        )
        {   continue;
        }

        process_info info;
        info.pid = pid;
        info.name = line.substr(open_paren + 1, close_paren - open_paren - 1);
        info.username = std::move(username);

        std::error_code link_ec;
        fs::path exe = fs::read_symlink(proc_dir / "exe", link_ec);
        if (!link_ec)
        {   info.exe_path = exe.string();
        }

        info.args = detail::parse_linux_cmdline(proc_dir / "cmdline");
        detail::parse_linux_io
        (   proc_dir / "io"
        ,   info.io_read_bytes
        ,   info.io_write_bytes
        );

        if (close_paren + 2 < line.size())
        {   std::istringstream iss(line.substr(close_paren + 2));
            std::string state_str;
            uint64_t ppid = 0, pgrp = 0, session = 0, flags = 0;
            int64_t tty_nr = 0, tpgid = 0;
            uint64_t minflt = 0, cminflt = 0, majflt = 0, cmajflt = 0;
            uint64_t utime = 0, stime = 0;
            int64_t cutime = 0, cstime = 0, priority = 0, nice = 0, num_threads = 0, itrealvalue = 0;
            uint64_t starttime = 0, vsize = 0;
            int64_t rss = 0;

            if
            (   iss >> state_str >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
                    >> minflt >> cminflt >> majflt >> cmajflt
                    >> utime >> stime >> cutime >> cstime
                    >> priority >> nice >> num_threads >> itrealvalue
                    >> starttime >> vsize >> rss
            )
            {   if (!state_str.empty())
                    info.state = detail::map_linux_state(state_str[0]);
                info.ppid = static_cast<uint32_t>(ppid);
                if (num_threads > 0)
                {   info.thread_count = static_cast<uint32_t>(num_threads);
                }
                info.start_time_raw = starttime;
                if (clk_tck > 0)
                {   info.cpu_time_sec =
                    (   static_cast<double>(utime)
                    +   static_cast<double>(stime)
                    )
                    /   static_cast<double>(clk_tck)
                    ;
                    if (computer && computer->boot_time)
                    {   int64_t offset_us =
                        static_cast<int64_t>
                        (   (   static_cast<double>(starttime)
                            /   static_cast<double>(clk_tck)
                            )
                        *   1e6
                        );
                        info.start_time
                        =   *computer->boot_time
                        +   std::chrono::microseconds(offset_us)
                        ;
                    }
                }
                if (rss > 0 && page_size > 0)
                {   info.memory_bytes
                =   static_cast<uint64_t>(rss)
                *   static_cast<uint64_t>(page_size)
                ;
                }
            }
        }

        result.push_back(std::move(info));
    }

    return result;
}

}   // namespace sysproc