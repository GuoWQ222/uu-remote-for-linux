/*
 * Minimal native Win64 program used in place of UU's Upgrade.exe.
 *
 * The Linux update bridge performs updates transactionally.  This program
 * prevents the upstream updater from replacing managed compatibility DLLs,
 * then leaves a prefix-local request for the native bridge.  UU starts
 * shutting down its Qt controller immediately after launching Upgrade.exe,
 * so silently exiting here would strand the user with no updater or restart.
 */

    .text
    .globl WinMainCRTStartup
WinMainCRTStartup:
    subq $72, %rsp
    leaq update_request(%rip), %rcx
    movl $0x40000000, %edx       /* GENERIC_WRITE */
    movl $7, %r8d                /* share read/write/delete */
    xorl %r9d, %r9d              /* lpSecurityAttributes */
    movq $2, 32(%rsp)            /* CREATE_ALWAYS */
    movq $0x80, 40(%rsp)         /* FILE_ATTRIBUTE_NORMAL */
    movq $0, 48(%rsp)            /* hTemplateFile */
    call *__imp_CreateFileA(%rip)
    cmpq $-1, %rax
    je marker_failed
    movq %rax, %rcx
    call *__imp_CloseHandle(%rip)
    xorl %ecx, %ecx
    call *__imp_ExitProcess(%rip)

marker_failed:
    movl $1, %ecx
    call *__imp_ExitProcess(%rip)
    hlt

    .section .rdata,"dr"
update_request:
    .asciz "C:\\uu-remote-upstream-update.request"
