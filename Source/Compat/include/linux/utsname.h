/*
 * RTL8814Native — linux/utsname.h shim
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef _LINUX_UTSNAME_H_RTL8814
#define _LINUX_UTSNAME_H_RTL8814

struct new_utsname {
        char sysname[65];
        char nodename[65];
        char release[65];
        char version[65];
        char machine[65];
        char domainname[65];
};

static inline struct new_utsname *init_utsname(void)
{
        static struct new_utsname s_uts = {
                "Darwin", "macOS", "25.6.0", "Tahoe", "x86_64", ""
        };
        return &s_uts;
}

#define utsname() init_utsname()

#endif /* _LINUX_UTSNAME_H_RTL8814 */
