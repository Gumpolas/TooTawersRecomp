/*
 * MCPX APU MMIO Hook - VEH instruction decoder for APU register access
 *
 * Reuses the same x86-64 instruction decoder pattern as nv2a_mmio_hook.c
 * but routes reads/writes through the MCPX APU register handlers.
 */

#include "apu.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

/* Global APU state pointer -- referenced from main.c regardless of which
 * platform's MMIO hook is active, so define it before the #if guard. */
MCPXAPUState *g_apu_state = NULL;

/* The MMIO hook is a Win32-VEH x86-64 instruction decoder. On Linux the
 * equivalent goes through sigaction + ucontext_t (Stage 2 / main.c). For
 * now the whole body is Windows-only so apu_emu links on Debian. */
#if 1   /* Windows VEH, or its POSIX emulation in win32_compat.c */
#include <windows.h>

/* APU MMIO base in Xbox VA space */
#define APU_MMIO_BASE  0xFE800000u
#define APU_MMIO_SIZE  0x00080000u  /* 512KB */

/* (g_apu_state is defined above, outside the Win32 guard) */

/* Statistics */
static int g_apu_mmio_read_count = 0;
static int g_apu_mmio_write_count = 0;
static int g_apu_mmio_decode_fail = 0;

/* ============================================================
 * x86-64 register access helpers (same as NV2A hook)
 * ============================================================ */

static uint64_t *ctx_reg64(PCONTEXT ctx, int reg)
{
    switch (reg & 0xF) {
    case 0:  return (uint64_t*)&ctx->Rax;
    case 1:  return (uint64_t*)&ctx->Rcx;
    case 2:  return (uint64_t*)&ctx->Rdx;
    case 3:  return (uint64_t*)&ctx->Rbx;
    case 4:  return (uint64_t*)&ctx->Rsp;
    case 5:  return (uint64_t*)&ctx->Rbp;
    case 6:  return (uint64_t*)&ctx->Rsi;
    case 7:  return (uint64_t*)&ctx->Rdi;
    case 8:  return (uint64_t*)&ctx->R8;
    case 9:  return (uint64_t*)&ctx->R9;
    case 10: return (uint64_t*)&ctx->R10;
    case 11: return (uint64_t*)&ctx->R11;
    case 12: return (uint64_t*)&ctx->R12;
    case 13: return (uint64_t*)&ctx->R13;
    case 14: return (uint64_t*)&ctx->R14;
    case 15: return (uint64_t*)&ctx->R15;
    default: return NULL;
    }
}

static int decode_modrm_len(const uint8_t *ip, int has_rex_b)
{
    uint8_t modrm = *ip;
    int mod = (modrm >> 6) & 3;
    int rm = (modrm & 7) | (has_rex_b ? 8 : 0);
    int len = 1;

    if (mod == 3) return 1;
    if ((rm & 7) == 4) len += 1; /* SIB */
    if (mod == 0 && (rm & 7) == 5) len += 4; /* disp32 */
    else if (mod == 1) len += 1;
    else if (mod == 2) len += 4;
    return len;
}

/* ============================================================
 * Instruction decoder for APU MMIO access
 * ============================================================ */

static uintptr_t g_apu_host_delta;   /* host address - Xbox VA, per fault */

/* One element of a string op: APU addresses go through the emulated APU,
 * anything else is ordinary memory on whichever side it is. */
static uint64_t apu_str_load(uintptr_t host, int size)
{
    uint32_t va = (uint32_t)(host - g_apu_host_delta);
    if (host >= g_apu_host_delta && va >= APU_MMIO_BASE && va < APU_MMIO_BASE + 0x80000u)
        return mcpx_apu_mmio_read(g_apu_state, va - APU_MMIO_BASE, size);
    switch (size) {
    case 1: return *(volatile uint8_t *)host;
    case 2: return *(volatile uint16_t *)host;
    case 4: return *(volatile uint32_t *)host;
    default: return *(volatile uint64_t *)host;
    }
}

static void apu_str_store(uintptr_t host, uint64_t v, int size)
{
    uint32_t va = (uint32_t)(host - g_apu_host_delta);
    if (host >= g_apu_host_delta && va >= APU_MMIO_BASE && va < APU_MMIO_BASE + 0x80000u) {
        mcpx_apu_mmio_write(g_apu_state, va - APU_MMIO_BASE, v, size);
        return;
    }
    switch (size) {
    case 1: *(volatile uint8_t *)host = (uint8_t)v; break;
    case 2: *(volatile uint16_t *)host = (uint16_t)v; break;
    case 4: *(volatile uint32_t *)host = (uint32_t)v; break;
    default: *(volatile uint64_t *)host = v; break;
    }
}

static bool apu_decode_and_handle(PCONTEXT ctx, uint32_t mmio_offset, int is_write)
{
    const uint8_t *ip = (const uint8_t *)ctx->Rip;
    if (!g_apu_state) return false;

    int prefix_len = 0;
    int has_66 = 0;
    int rex = 0, has_rex = 0;

    while (1) {
        uint8_t b = ip[prefix_len];
        if (b == 0x66) { has_66 = 1; prefix_len++; }
        else if (b == 0xF2 || b == 0xF3) { prefix_len++; }
        else if (b >= 0x40 && b <= 0x4F) { rex = b; has_rex = 1; prefix_len++; }
        else break;
    }

    int rex_w = has_rex && (rex & 0x08);
    int rex_r = has_rex && (rex & 0x04);
    int rex_b = has_rex && (rex & 0x01);

    const uint8_t *opcode = ip + prefix_len;
    int access_size = 4;
    if (has_66) access_size = 2;
    if (rex_w) access_size = 8;

    /* MOV r/m, r (write: 88/89) */
    if (opcode[0] == 0x89 || opcode[0] == 0x88) {
        if (opcode[0] == 0x88) access_size = 1;
        int modrm_len = decode_modrm_len(opcode + 1, rex_b);
        int reg = ((opcode[1] >> 3) & 7) | (rex_r ? 8 : 0);
        uint64_t val = *ctx_reg64(ctx, reg);
        mcpx_apu_mmio_write(g_apu_state, mmio_offset, val, access_size);
        ctx->Rip += prefix_len + 1 + modrm_len;
        g_apu_mmio_write_count++;
        return true;
    }

    /* MOV r/m, imm32 (write: C7 /0) */
    if (opcode[0] == 0xC7) {
        int modrm_len = decode_modrm_len(opcode + 1, rex_b);
        uint32_t imm = *(uint32_t *)(opcode + 1 + modrm_len);
        mcpx_apu_mmio_write(g_apu_state, mmio_offset, imm, access_size);
        ctx->Rip += prefix_len + 1 + modrm_len + 4;
        g_apu_mmio_write_count++;
        return true;
    }

    /* MOV r/m8, imm8 (write: C6 /0) */
    if (opcode[0] == 0xC6) {
        access_size = 1;
        int modrm_len = decode_modrm_len(opcode + 1, rex_b);
        uint8_t imm = *(opcode + 1 + modrm_len);
        mcpx_apu_mmio_write(g_apu_state, mmio_offset, imm, 1);
        ctx->Rip += prefix_len + 1 + modrm_len + 1;
        g_apu_mmio_write_count++;
        return true;
    }

    /* MOV r, r/m (read: 8A/8B) */
    if (opcode[0] == 0x8B || opcode[0] == 0x8A) {
        if (opcode[0] == 0x8A) access_size = 1;
        int modrm_len = decode_modrm_len(opcode + 1, rex_b);
        int reg = ((opcode[1] >> 3) & 7) | (rex_r ? 8 : 0);
        uint64_t val = mcpx_apu_mmio_read(g_apu_state, mmio_offset, access_size);
        uint64_t *dst = ctx_reg64(ctx, reg);
        if (access_size == 1) *dst = (*dst & ~0xFFULL) | (val & 0xFF);
        else if (access_size == 2) *dst = (*dst & ~0xFFFFULL) | (val & 0xFFFF);
        else if (access_size == 4) *dst = val & 0xFFFFFFFF;
        else *dst = val;
        ctx->Rip += prefix_len + 1 + modrm_len;
        g_apu_mmio_read_count++;
        return true;
    }

    /* MOVZX r32, r/m8 (0F B6) */
    if (opcode[0] == 0x0F && opcode[1] == 0xB6) {
        access_size = 1;
        int modrm_len = decode_modrm_len(opcode + 2, rex_b);
        int reg = ((opcode[2] >> 3) & 7) | (rex_r ? 8 : 0);
        uint64_t val = mcpx_apu_mmio_read(g_apu_state, mmio_offset, 1);
        *ctx_reg64(ctx, reg) = val & 0xFF;
        ctx->Rip += prefix_len + 2 + modrm_len;
        g_apu_mmio_read_count++;
        return true;
    }

    /* MOVZX r32, r/m16 (0F B7) */
    if (opcode[0] == 0x0F && opcode[1] == 0xB7) {
        access_size = 2;
        int modrm_len = decode_modrm_len(opcode + 2, rex_b);
        int reg = ((opcode[2] >> 3) & 7) | (rex_r ? 8 : 0);
        uint64_t val = mcpx_apu_mmio_read(g_apu_state, mmio_offset, 2);
        *ctx_reg64(ctx, reg) = val & 0xFFFF;
        ctx->Rip += prefix_len + 2 + modrm_len;
        g_apu_mmio_read_count++;
        return true;
    }

    /* TEST r/m, r (84/85) - read */
    if (opcode[0] == 0x85 || opcode[0] == 0x84) {
        if (opcode[0] == 0x84) access_size = 1;
        int modrm_len = decode_modrm_len(opcode + 1, rex_b);
        int reg = ((opcode[1] >> 3) & 7) | (rex_r ? 8 : 0);
        uint64_t mem_val = mcpx_apu_mmio_read(g_apu_state, mmio_offset, access_size);
        uint64_t reg_val = *ctx_reg64(ctx, reg);
        uint64_t result = mem_val & reg_val;
        ctx->EFlags &= ~(0x0001 | 0x0040 | 0x0080 | 0x0800);
        if (result == 0) ctx->EFlags |= 0x0040;
        if (result & (1ULL << (access_size * 8 - 1))) ctx->EFlags |= 0x0080;
        ctx->Rip += prefix_len + 1 + modrm_len;
        g_apu_mmio_read_count++;
        return true;
    }

    /* CMP r/m, r (38/39) */
    if (opcode[0] == 0x39 || opcode[0] == 0x38) {
        if (opcode[0] == 0x38) access_size = 1;
        int modrm_len = decode_modrm_len(opcode + 1, rex_b);
        int reg = ((opcode[1] >> 3) & 7) | (rex_r ? 8 : 0);
        uint64_t mem_val = mcpx_apu_mmio_read(g_apu_state, mmio_offset, access_size);
        uint64_t reg_val = *ctx_reg64(ctx, reg);
        if (access_size <= 4) {
            mem_val &= (1ULL << (access_size * 8)) - 1;
            reg_val &= (1ULL << (access_size * 8)) - 1;
        }
        uint64_t result = mem_val - reg_val;
        ctx->EFlags &= ~(0x0001 | 0x0040 | 0x0080 | 0x0800);
        if (result == 0) ctx->EFlags |= 0x0040;
        if (mem_val < reg_val) ctx->EFlags |= 0x0001;
        if (result & (1ULL << (access_size * 8 - 1))) ctx->EFlags |= 0x0080;
        ctx->Rip += prefix_len + 1 + modrm_len;
        g_apu_mmio_read_count++;
        return true;
    }

    /* OR r/m, r (08/09) */
    if (opcode[0] == 0x09 || opcode[0] == 0x08) {
        if (opcode[0] == 0x08) access_size = 1;
        int modrm_len = decode_modrm_len(opcode + 1, rex_b);
        int reg = ((opcode[1] >> 3) & 7) | (rex_r ? 8 : 0);
        uint64_t mem_val = mcpx_apu_mmio_read(g_apu_state, mmio_offset, access_size);
        uint64_t reg_val = *ctx_reg64(ctx, reg);
        mcpx_apu_mmio_write(g_apu_state, mmio_offset, mem_val | reg_val, access_size);
        ctx->Rip += prefix_len + 1 + modrm_len;
        g_apu_mmio_write_count++;
        return true;
    }

    /* AND r/m, r (20/21) */
    if (opcode[0] == 0x21 || opcode[0] == 0x20) {
        if (opcode[0] == 0x20) access_size = 1;
        int modrm_len = decode_modrm_len(opcode + 1, rex_b);
        int reg = ((opcode[1] >> 3) & 7) | (rex_r ? 8 : 0);
        uint64_t mem_val = mcpx_apu_mmio_read(g_apu_state, mmio_offset, access_size);
        uint64_t reg_val = *ctx_reg64(ctx, reg);
        mcpx_apu_mmio_write(g_apu_state, mmio_offset, mem_val & reg_val, access_size);
        ctx->Rip += prefix_len + 1 + modrm_len;
        g_apu_mmio_write_count++;
        return true;
    }

    /* MOVS / STOS, optionally REP: a block copy to or from APU memory (the
     * runtime's rep movsd is a host memcpy, so the faulting instruction is in
     * the C library, with host RSI/RDI/RCX). The Two Towers' DirectSound
     * copies DSP state out of the APU at 0xFE830200 this way. */
    if (opcode[0] == 0xA4 || opcode[0] == 0xA5 || opcode[0] == 0xAA || opcode[0] == 0xAB) {
        int rep = 0, i;
        int size = (opcode[0] == 0xA4 || opcode[0] == 0xAA) ? 1 : access_size;
        int step = (ctx->EFlags & 0x400) ? -size : size;
        uint64_t n;
        for (i = 0; i < prefix_len; i++)
            if (ip[i] == 0xF3 || ip[i] == 0xF2) rep = 1;
        n = rep ? ctx->Rcx : 1;
        while (n--) {
            if (opcode[0] == 0xA4 || opcode[0] == 0xA5) {
                apu_str_store(ctx->Rdi, apu_str_load(ctx->Rsi, size), size);
                ctx->Rsi += step;
            } else {
                apu_str_store(ctx->Rdi, ctx->Rax, size);
            }
            ctx->Rdi += step;
        }
        if (rep) ctx->Rcx = 0;
        ctx->Rip += prefix_len + 1;
        if (is_write) g_apu_mmio_write_count++; else g_apu_mmio_read_count++;
        return true;
    }

    /* Unrecognized */
    g_apu_mmio_decode_fail++;
    if (g_apu_mmio_decode_fail <= 20) {
        fprintf(stderr, "[APU] MMIO decode fail at RIP=%p offset=0x%X: %02X %02X %02X %02X %02X %02X\n",
                (void*)ctx->Rip, mmio_offset, ip[0], ip[1], ip[2], ip[3], ip[4], ip[5]);
        fflush(stderr);
    }
    return false;
}

/* Direct calls for constant-address APU accesses in the generated code
 * (twotowers/postproc_apu.py rewrites them): the same handlers the fault
 * path reaches, without the exception round trip. */
uint32_t xbox_apu_r32(uint32_t va)
{
    if (!g_apu_state) return 0;
    g_apu_mmio_read_count++;
    return (uint32_t)mcpx_apu_mmio_read(g_apu_state, va - APU_MMIO_BASE, 4);
}
void xbox_apu_w32(uint32_t va, uint32_t v)
{
    if (!g_apu_state) return;
    g_apu_mmio_write_count++;
    mcpx_apu_mmio_write(g_apu_state, va - APU_MMIO_BASE, v, 4);
}

/* ============================================================
 * Public API (called from VEH in main.c)
 * ============================================================ */

bool apu_hook_handle_mmio(PCONTEXT ctx, uintptr_t fault_addr,
                          uint32_t fault_xbox_va, int is_write)
{
    uint32_t mmio_offset = fault_xbox_va - APU_MMIO_BASE;
    g_apu_host_delta = fault_addr - fault_xbox_va;
    {
        /* RECOMP_APU_SITES=1: which code makes the trapped APU accesses, and
         * how many a second (each one is an exception round trip). */
        static int sites = -1;
        if (sites < 0) sites = getenv("RECOMP_APU_SITES") != NULL;
        if (sites) {
            static struct { uint64_t rip; uint32_t n, off; } t[256];
            static uint32_t total; static DWORD t0;
            uint32_t h = (uint32_t)((ctx->Rip >> 2) * 2654435761u) & 255, i;
            for (i = 0; i < 256; i++) {
                uint32_t k = (h + i) & 255;
                if (!t[k].rip || t[k].rip == ctx->Rip) { t[k].rip = ctx->Rip; t[k].n++; t[k].off = mmio_offset; break; }
            }
            total++;
            if (!t0) t0 = GetTickCount();
            if (GetTickCount() - t0 > 10000) {
                HMODULE exe = GetModuleHandleA(NULL);
                fprintf(stderr, "  [APUSITES] %u trapped accesses in %lu ms; top sites:\n", total, (unsigned long)(GetTickCount() - t0));
                for (int top = 0; top < 12; top++) {
                    int best = -1;
                    for (i = 0; i < 256; i++) if (t[i].n && (best < 0 || t[i].n > t[best].n)) best = (int)i;
                    if (best < 0) break;
                    fprintf(stderr, "  [APUSITES]   %7u exe+0x%llX off %05X\n", t[best].n,
                            (unsigned long long)(t[best].rip - (uint64_t)(uintptr_t)exe), t[best].off);
                    t[best].n = 0;
                }
                memset(t, 0, sizeof t); total = 0; t0 = GetTickCount();
            }
        }
    }
    bool ok = apu_decode_and_handle(ctx, mmio_offset, is_write);

    /* What the title actually asks the APU for. The DSPs are stubbed here, so
     * a title that waits on one waits forever, and the only way to work out
     * what it is waiting for is to see the register traffic that precedes the
     * wait. */
    static int atrace = -1;
    if (atrace < 0) atrace = getenv("RECOMP_APU_TRACE") != NULL;
    if (atrace) {
        static unsigned n;
        if (n++ < 400) {
            /* The value as well as the offset: finding which register carries
             * the command-block address means recognising the address when it
             * goes past, and an offset alone never shows it. */
            uint64_t v = g_apu_state
                       ? mcpx_apu_mmio_read(g_apu_state, mmio_offset, 4) : 0;
            fprintf(stderr, "  [APUMMIO] %s 0x%05X = %08X%s\n",
                    is_write ? "write" : "read ", mmio_offset,
                    (uint32_t)v, ok ? "" : "  (decode failed)");
        }
    }
    return ok;
}

#endif /* _WIN32 */
