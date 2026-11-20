"""Papyrus native functions of SkyrimSE.exe by byte patterns (standard library only).

Every native is registered the same way (seen in AE#56246, 1.6.1170):
    lea r8, [class name]      4C 8D 05 disp32
    lea rdx, [function name]  48 8D 15 disp32
    ...
    call NativeFunctionBase constructor
    lea reg, [NativeFunctionN vftable]
    mov [obj], reg
    lea reg, [native implementation]      <- the callback, stored at obj+0x50
    mov [obj+0x50], reg
The constructors are found as the call targets that follow such lea pairs most often.
"""
import collections
import re
import struct

LEA_R8 = re.compile(rb"\x4C\x8D\x05(.{4})", re.S)
LEA_RIP = re.compile(rb"[\x48\x4C]\x8D([\x05\x0D\x15\x1D\x25\x2D\x35\x3D])(.{4})", re.S)


LEA_RDX = bytes([0x48, 0x8D, 0x15])
CALL = bytes([0xE8])
LEA_R9 = bytes([0x4C, 0x8D, 0x0D])
RDX_MODRM = bytes([0x15])


def _cstr(pe, rva, limit=96):
    try:
        off = pe.rva2off(rva)
    except ValueError:
        return None
    end = pe.data.find(b"\0", off, off + limit)
    if end <= off:
        return None
    s = pe.data[off:end]
    if not re.fullmatch(rb"[A-Za-z_][A-Za-z0-9_]*", s):
        return None
    return s.decode("ascii")


BIND = bytes([0xFF, 0x90, 0xC0, 0x00, 0x00, 0x00])  # call qword ptr [rax+0xC0]: IVirtualMachine::BindNativeMethod


def _store50(b):
    """mov [reg+0x50], reg64: REX.W 89 ModRM(mod=01) [SIB if rm=100] disp8=0x50."""
    if len(b) < 4 or b[0] not in (0x48, 0x49, 0x4C, 0x4D) or b[1] != 0x89 or b[2] >> 6 != 1:
        return False
    return (b[4] if (b[2] & 7) == 4 and len(b) > 4 else b[3]) == 0x50


def _rip(code, va, pos):
    """Target RVA of the 7-byte RIP-relative instruction at pos."""
    return va + pos + 7 + struct.unpack_from("<i", code, pos + 3)[0]


def natives(pe):
    """[(callback rva, class, function, site rva, pattern)].

    A: lea r8,[class]; lea rdx,[name]; call NativeFunctionBase ctor; ...; lea reg,[callback]; mov [obj+0x50],reg
    B: lea r9,[callback]; lea r8,[class]; lea rdx,[name]; mov rcx,rax; call NativeFunctionN<...> ctor
    Both are kept only if `call [rax+0xC0]` (BindNativeMethod) follows within 400 bytes.
    """
    out = []
    for name, va, size, raw, chars in pe.sections:
        if not chars & 0x20000000:
            continue
        code = pe.data[raw:raw + size]
        for m in LEA_R8.finditer(code):
            p = m.start()
            cls = _cstr(pe, _rip(code, va, p))
            if cls is None:
                continue
            q = code.find(LEA_RDX, p + 7, p + 7 + 12)
            if q < 0:
                continue
            fn = _cstr(pe, _rip(code, va, q))
            c = code.find(CALL, q + 7, q + 7 + 12)
            if fn is None or c < 0 or code.find(BIND, c + 5, c + 5 + 400) < 0:
                continue
            cb, pat = None, None
            r9 = code.rfind(LEA_R9, max(0, p - 12), p)
            if r9 >= 0 and pe.is_code(_rip(code, va, r9)):
                cb, pat = _rip(code, va, r9), "B"
            else:
                for lm in LEA_RIP.finditer(code, c + 5, c + 5 + 48):
                    tgt = _rip(code, va, lm.start())
                    if pe.is_code(tgt):
                        if _store50(code[lm.end():lm.end() + 5]):
                            cb, pat = tgt, "A"
                        break
            if cb is not None:
                out.append((cb, cls, fn, va + p, pat))
    return out
