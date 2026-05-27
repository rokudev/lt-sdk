/*
 * Copyright (c) 2021 Roku, Inc. All rights reserved.
 * This software and any compilation or derivative thereof is, and shall
 * remain, the proprietary information of Roku, Inc. and is highly confidential
 * in nature.
 */
#ifndef sys_arch_a699c60fdd5bdf9a
#define sys_arch_a699c60fdd5bdf9a

#include <lwipopts.h>

// This would usually be declared for us by lwip/sys.h but not when
// NO_SYS=1
#if NO_SYS
void sys_init(void);
#endif // NO_SYS

#endif // sys_arch_a699c60fdd5bdf9a
