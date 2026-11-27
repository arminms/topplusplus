// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Armin Sobhani
//
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <winternl.h>

// Windows macros collide with FTXUI's Canvas::DrawText and Color::RGB.
#undef DrawText
#undef RGB

namespace sysproc {
namespace detail {

inline
std::string wide_to_utf8(const wchar_t* wstr, int wchar_count = -1)
{   if (!wstr || (wchar_count == 0)) return {};
    if (wchar_count < 0 && !*wstr) return {};

    int size = WideCharToMultiByte
    (   CP_UTF8
    ,   0
    ,   wstr
    ,   wchar_count
    ,   nullptr
    ,   0
    ,   nullptr
    ,   nullptr
    );
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    if
    (   WideCharToMultiByte
        (   CP_UTF8
        ,   0
        ,   wstr
        ,   wchar_count
        ,   out.data()
        ,   size
        ,   nullptr
        ,   nullptr
        )   == 0
    )   return {};
    if (wchar_count < 0)
        out.pop_back();
    return out;
}

inline
uint64_t filetime_to_u64(const FILETIME& ft)
{   return
    (   static_cast<uint64_t>(ft.dwHighDateTime) << 32
    )
    |   ft.dwLowDateTime;
}

inline time_point windows_ticks_to_unix_time(uint64_t win_100ns)
{   constexpr uint64_t kEpochDiff100ns = 116444736000000000ULL;
    if (win_100ns < kEpochDiff100ns)
    {   return time_point{};
    }
    uint64_t unix_us = (win_100ns - kEpochDiff100ns) / 10ULL;
    return time_point{std::chrono::microseconds(unix_us)};
}

inline std::unordered_map<uint32_t, process_state> get_windows_process_states()
{   std::unordered_map<uint32_t, process_state> states;

    using nt_query_system_information_fn
    =   NTSTATUS(NTAPI*)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
    static auto nt_query_sys_info
    =   reinterpret_cast<nt_query_system_information_fn>
    (   GetProcAddress
        (   GetModuleHandleW(L"ntdll.dll")
        ,   "NtQuerySystemInformation"
        )
    );
    if (!nt_query_sys_info) return states;

    ULONG buf_size = 256 * 1024;
    std::vector<uint8_t> buf(buf_size);
    NTSTATUS status = 0;

    while
    (   (   status
        =   nt_query_sys_info
            (   SystemProcessInformation
            ,   buf.data()
            ,   buf_size
            ,   &buf_size
            )
        )   == static_cast<NTSTATUS>(0xC0000004L)
    )
    {   buf_size += 64 * 1024;
        buf.resize(buf_size);
    }
    if (status != 0) return states;

    size_t offset = 0;
    while
    (   offset <= buf.size()
    &&  buf.size() - offset >= sizeof(SYSTEM_PROCESS_INFORMATION)
    )
    {   auto* spi = reinterpret_cast<const SYSTEM_PROCESS_INFORMATION*>
        (   buf.data()
        +   offset
        );
        const size_t entry_size
        =   spi->NextEntryOffset
        ?   spi->NextEntryOffset
        :   buf.size() - offset
        ;
        if
        (   entry_size < sizeof(SYSTEM_PROCESS_INFORMATION)
        ||  entry_size > buf.size() - offset
        ||  spi->NumberOfThreads >
            (   entry_size
            -   sizeof(SYSTEM_PROCESS_INFORMATION)
            )
            /   sizeof(SYSTEM_THREAD_INFORMATION)
        )
        {   break;
        }
        uint32_t pid = static_cast<uint32_t>
        (   reinterpret_cast<uintptr_t>(spi->UniqueProcessId)
        );

        if (pid == 0)
        {   states[pid] = process_state::idle;
        }
        else if (spi->NumberOfThreads == 0)
        {   states[pid] = process_state::zombie;
        }
        else
        {   const auto* threads
            =   reinterpret_cast<const SYSTEM_THREAD_INFORMATION*>
                (   reinterpret_cast<const uint8_t*>(spi)
                +   sizeof(SYSTEM_PROCESS_INFORMATION)
                );
            bool any_running = false;
            bool all_suspended = true;
            for (ULONG i = 0; i < spi->NumberOfThreads; ++i)
            {   if
                (   threads[i].ThreadState == 1
                ||  threads[i].ThreadState == 2
                ||  threads[i].ThreadState == 3
                )
                {   any_running = true;
                }
                if
                (   !(threads[i].ThreadState == 5 && threads[i].WaitReason == 5)
                )
                {   all_suspended = false;
                }
            }

            if (any_running)
            {   states[pid] = process_state::running;
            }
            else if (all_suspended)
            {   states[pid] = process_state::stopped;
            }
            else
            {   states[pid] = process_state::sleeping;
            }
        }

        if (spi->NextEntryOffset == 0) break;
        offset += spi->NextEntryOffset;
    }

    return states;
}

inline
std::string get_process_username(HANDLE hProcess)
{   HANDLE hToken = nullptr;
    if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hToken))
    {   return {};
    }

    DWORD len = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &len);
    if (len == 0)
    {   CloseHandle(hToken);
        return {};
    }

    std::vector<uint8_t> buf(len);
    std::string username;
    if (GetTokenInformation(hToken, TokenUser, buf.data(), len, &len))
    {   auto* user = reinterpret_cast<TOKEN_USER*>(buf.data());
        wchar_t name[256]{};
        wchar_t domain[256]{};
        DWORD name_len = 256;
        DWORD domain_len = 256;
        SID_NAME_USE sid_type;
        if
        (   LookupAccountSidW
            (   nullptr
            ,   user->User.Sid
            ,   name
            ,   &name_len
            ,   domain
            ,   &domain_len, &sid_type
            )
        )
        {   username = wide_to_utf8(name);
        }
    }

    CloseHandle(hToken);
    return username;
}

inline
std::string get_process_exe_path(HANDLE hProcess)
{   std::vector<wchar_t> buf(1024, L'\0');
    DWORD size = static_cast<DWORD>(buf.size());
    while (!QueryFullProcessImageNameW(hProcess, 0, buf.data(), &size))
    {   if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && buf.size() < 32768)
        {   buf.resize(buf.size() * 2, L'\0');
            size = static_cast<DWORD>(buf.size());
        }
        else
        {   return {};
        }
    }
    return wide_to_utf8(buf.data(), static_cast<int>(size));
}

inline
std::vector<std::string> parse_windows_cmdline(const std::wstring& cmdline)
{   std::vector<std::string> result;
    if (cmdline.empty()) return result;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdline.c_str(), &argc);
    if (!argv) return result;

    result.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i)
        result.push_back(wide_to_utf8(argv[i]));
    LocalFree(argv);
    return result;
}

inline
std::vector<std::string> get_process_args(HANDLE hProcess)
{   using nt_query_information_process_fn
    =   NTSTATUS(NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
    static auto nt_query_info = reinterpret_cast<nt_query_information_process_fn>
    (   GetProcAddress
        (   GetModuleHandleW(L"ntdll.dll")
        ,   "NtQueryInformationProcess"
        )
    );
    if (!nt_query_info) return {};

    BOOL is_wow64 = FALSE;
    IsWow64Process(hProcess, &is_wow64);

#if defined(_WIN64)
    if (is_wow64)
    {   ULONG_PTR peb32_addr = 0;
        if
        (   nt_query_info
            (   hProcess
            ,   ProcessWow64Information
            ,   &peb32_addr
            ,   sizeof(peb32_addr)
            ,   nullptr
            ) != 0
        ||  !peb32_addr
        )
        {   return {};
        }
        uint32_t proc_params32 = 0;
        if
        (   !ReadProcessMemory
            (   hProcess
            ,   reinterpret_cast<LPCVOID>(peb32_addr + 0x10)
            ,   &proc_params32
            ,   sizeof(proc_params32)
            ,   nullptr
            )
        ||  !proc_params32
        )
        {   return {};
        }
        struct unicode_string_32
        {   uint16_t Length;
            uint16_t MaximumLength;
            uint32_t Buffer;
        }   cmd32{};
        if
        (   !ReadProcessMemory
            (   hProcess
            ,   reinterpret_cast<LPCVOID>
                (   static_cast<uintptr_t>(proc_params32)
                +   0x40
                )
            ,   &cmd32
            ,   sizeof(cmd32)
            ,   nullptr
            )
        ||  cmd32.Length == 0
        ||  cmd32.Length % sizeof(wchar_t) != 0
        ||  !cmd32.Buffer
        )
        {   return {};
        }
        std::wstring cmdline(cmd32.Length / sizeof(wchar_t), L'\0');
        if
        (   !ReadProcessMemory
            (   hProcess
            ,   reinterpret_cast<LPCVOID>(static_cast<uintptr_t>(cmd32.Buffer))
            ,   cmdline.data()
            ,   cmd32.Length
            ,   nullptr
            )
        )
        {   return {};
        }
        return parse_windows_cmdline(cmdline);
    }
#endif

    PROCESS_BASIC_INFORMATION pbi{};
    if
    (   nt_query_info
        (   hProcess
        ,   ProcessBasicInformation
        ,   &pbi
        ,   sizeof(pbi)
        ,   nullptr
        )   != 0
    ||  !pbi.PebBaseAddress
    )
    {   return {};
    }

    PEB peb{};
    if
    (   !ReadProcessMemory
        (   hProcess
        ,   pbi.PebBaseAddress
        ,   &peb
        ,   sizeof(peb)
        ,   nullptr
        )
    ||  !peb.ProcessParameters
    )
    {   return {};
    }

    RTL_USER_PROCESS_PARAMETERS params{};
    if
    (   !ReadProcessMemory
        (   hProcess
        ,   peb.ProcessParameters
        ,   &params
        ,   sizeof(params)
        ,   nullptr
        )
    ||  params.CommandLine.Length == 0
    ||  params.CommandLine.Length % sizeof(wchar_t) != 0
    ||  !params.CommandLine.Buffer
    )
    {   return {};
    }

    std::wstring cmdline(params.CommandLine.Length / sizeof(wchar_t), L'\0');
    if
    (   !ReadProcessMemory
        (   hProcess
        ,   params.CommandLine.Buffer
        ,   cmdline.data()
        ,   params.CommandLine.Length
        ,   nullptr
        )
    )
    {   return {};
    }

    return parse_windows_cmdline(cmdline);
}

} // namespace detail

inline
std::vector<process_info> list_processes()
{   std::vector<process_info> result;
    const auto state_map = detail::get_windows_process_states();

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
    {   return result;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snap, &entry))
    {   do
        {   process_info info;
            info.pid = static_cast<uint32_t>(entry.th32ProcessID);
            info.ppid = static_cast<uint32_t>(entry.th32ParentProcessID);
            info.thread_count = static_cast<uint32_t>(entry.cntThreads);
            info.name = detail::wide_to_utf8(entry.szExeFile);

            auto st_it = state_map.find(info.pid);
            if (st_it != state_map.end())
            {   info.state = st_it->second;
            }

            if (info.pid > 0)
            {   bool can_read_vm = true;
                HANDLE hProc = OpenProcess
                (   PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ
                ,   FALSE
                ,   info.pid
                );
                if (!hProc)
                {   can_read_vm = false;
                    hProc = OpenProcess
                    (   PROCESS_QUERY_LIMITED_INFORMATION
                    ,   FALSE
                    ,   info.pid
                    );
                }
                if (hProc)
                {   info.username = detail::get_process_username(hProc);
                    info.exe_path = detail::get_process_exe_path(hProc);
                    if (can_read_vm)
                    {   info.args = detail::get_process_args(hProc);
                    }

                    PROCESS_MEMORY_COUNTERS pmc{};
                    if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc)))
                    {   info.memory_bytes = static_cast<uint64_t>
                        (   pmc.WorkingSetSize
                        );
                    }

                    IO_COUNTERS io{};
                    if (GetProcessIoCounters(hProc, &io))
                    {   info.io_read_bytes = static_cast<uint64_t>
                        (   io.ReadTransferCount
                        );
                        info.io_write_bytes = static_cast<uint64_t>
                        (   io.WriteTransferCount
                        );
                    }

                    FILETIME ft_create{}, ft_exit{}, ft_kernel{}, ft_user{};
                    if
                    (   GetProcessTimes
                        (   hProc
                        ,   &ft_create
                        ,   &ft_exit
                        ,   &ft_kernel
                        ,   &ft_user
                        )
                    )
                    {   info.start_time_raw
                        =   detail::filetime_to_u64(ft_create);
                        info.start_time
                        =   detail::windows_ticks_to_unix_time
                            (   info.start_time_raw
                            );
                        uint64_t total_100ns
                        =   detail::filetime_to_u64(ft_kernel)
                        +   detail::filetime_to_u64(ft_user)
                        ;
                        info.cpu_time_sec
                        =   static_cast<double>(total_100ns) * 1e-7;
                    }

                    CloseHandle(hProc);
                }
            }
            else
            {   info.state = process_state::idle;
            }

            result.push_back(std::move(info));
        } while (Process32NextW(snap, &entry));
    }

    CloseHandle(snap);
    return result;
}

} // namespace sysproc