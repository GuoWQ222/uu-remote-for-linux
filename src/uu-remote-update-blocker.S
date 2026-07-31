/*
 * Minimal native Win64 program used in place of UU's Upgrade.exe.
 *
 * The Linux update bridge performs updates transactionally.  This program
 * deliberately does nothing and exits successfully so the upstream idle
 * updater cannot replace managed compatibility DLLs behind the bridge.
 */

    .text
    .globl WinMainCRTStartup
WinMainCRTStartup:
    subq $40, %rsp
    xorl %ecx, %ecx
    call *__imp_ExitProcess(%rip)
    hlt
