// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#include <libproc.h>
#include <mach/mach_time.h>
#include <sys/proc.h>
#include <sys/proc_info.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include "sysproc_posix.hpp"

namespace sysproc {
namespace detail {

inline
process_state map_macos_state(uint32_t status)
{   switch (status)
    {   case SRUN:   return process_state::running;
        case SSLEEP: return process_state::sleeping;
        case SSTOP:  return process_state::stopped;
        case SZOMB:  return process_state::zombie;
        case SIDL:   return process_state::idle;
        default:     return process_state::unknown;
    }
}

inline
double mach_ticks_to_seconds(uint64_t ticks)
{   static mach_timebase_info_data_t timebase = []()
    {   mach_timebase_info_data_t tb{};
        mach_timebase_info(&tb);
        return tb;
    } ();
    double ns
    =   static_cast<double>(ticks)
    *   static_cast<double>(timebase.numer)
    /   static_cast<double>(timebase.denom)
    ;
    return ns * 1e-9;
}

inline
std::vector<std::string> get_process_args(pid_t pid, int max_arg_size)
{   std::vector<std::string> args;
    if (max_arg_size <= 0) return args;

    int mib[3] = {CTL_KERN, KERN_PROCARGS2, pid};
    std::vector<char> buf(static_cast<size_t>(max_arg_size));
    size_t size = buf.size();

    if
    (   sysctl(mib, 3, buf.data(), &size, nullptr, 0) != 0
    ||  size <= sizeof(int)
    )
    {   return args;
    }

    int argc = 0;
    std::memcpy(&argc, buf.data(), sizeof(int));
    if (argc <= 0) return args;

    size_t pos = sizeof(int);
    while (pos < size && buf[pos] != '\0') ++pos;
    while (pos < size && buf[pos] == '\0') ++pos;

    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc && pos < size; ++i)
    {   const char* str_start = buf.data() + pos;
        size_t len = strnlen(str_start, size - pos);
        args.emplace_back(str_start, len);
        pos += len + 1;
    }

    return args;
}

} // namespace detail

inline
std::vector<process_info> list_processes()
{   std::vector<process_info> result;

    int max_arg_size = 0;
    size_t max_arg_len = sizeof(max_arg_size);
    int argmax_mib[2] = {CTL_KERN, KERN_ARGMAX};
    if
    (   sysctl(argmax_mib, 2, &max_arg_size, &max_arg_len, nullptr, 0) != 0
    ||  max_arg_size <= 0
    )
    {   max_arg_size = 262144;
    }

    int pids_needed = proc_listallpids(nullptr, 0);
    if (pids_needed <= 0) return result;

    size_t count = static_cast<size_t>(pids_needed) + 64;
    std::vector<pid_t> pids(count);

    int pids_returned = proc_listallpids
    (   pids.data()
    ,   static_cast<int>(pids.size() * sizeof(pid_t))
    );
    if (pids_returned <= 0) return result;

    size_t actual_count = static_cast<size_t>(pids_returned);
    result.reserve(actual_count);
    std::unordered_map<uid_t, std::string> uid_cache;

    for (size_t i = 0; i < actual_count; ++i)
    {   pid_t pid = pids[i];
        if (pid <= 0) continue;

        process_info info;
        info.pid = static_cast<uint32_t>(pid);

        char name_buf[MAXCOMLEN * 2 + 1]{};
        int name_len = proc_name(pid, name_buf, sizeof(name_buf));
        if (name_len > 0)
        {   info.name.assign(name_buf, static_cast<size_t>(name_len));
        }

        char path_buf[PROC_PIDPATHINFO_MAXSIZE]{};
        int path_len = proc_pidpath(pid, path_buf, sizeof(path_buf));
        if (path_len > 0)
        {   info.exe_path.assign(path_buf, static_cast<size_t>(path_len));
        }

        info.args = detail::get_process_args(pid, max_arg_size);

        proc_bsdinfo bsd_info{};
        if
        (   proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bsd_info, sizeof(bsd_info))
        ==  sizeof(bsd_info)
        )
        {   info.ppid = static_cast<uint32_t>(bsd_info.pbi_ppid);
            info.state = detail::map_macos_state(bsd_info.pbi_status);
            info.username = detail::uid_to_username
            (   bsd_info.pbi_uid
            ,   uid_cache
            );

            uint64_t unix_us
            =   static_cast<uint64_t>(bsd_info.pbi_start_tvsec)
            *   1'000'000ULL
            +   static_cast<uint64_t>(bsd_info.pbi_start_tvusec)
            ;
            info.start_time_raw = unix_us;
            info.start_time = time_point{std::chrono::microseconds(unix_us)};

            if (info.name.empty())
            {   info.name = bsd_info.pbi_name[0]
            ?   bsd_info.pbi_name
            :   bsd_info.pbi_comm;
            }
        }

        proc_taskinfo task_info{};
        if
        (   proc_pidinfo
            (   pid
            ,   PROC_PIDTASKINFO
            ,   0
            ,   &task_info
            ,   sizeof(task_info)
            )
        ==  sizeof(task_info)
        )
        {   info.thread_count = static_cast<uint32_t>(task_info.pti_threadnum);
            info.memory_bytes = task_info.pti_resident_size;
            info.cpu_time_sec = detail::mach_ticks_to_seconds
            (   task_info.pti_total_user
            +   task_info.pti_total_system
            );
        }

        rusage_info_v2 rusage{};
        if
        (   proc_pid_rusage
            (   pid
            ,   RUSAGE_INFO_V2
            ,   reinterpret_cast<rusage_info_t*>(&rusage)
            )
        ==  0
        )
        {   info.io_read_bytes = rusage.ri_diskio_bytesread;
            info.io_write_bytes = rusage.ri_diskio_byteswritten;
        }

        result.push_back(std::move(info));
    }

    return result;
}

}   // namespace sysproc
