"""Check the published DPF implementation on small complete and truncated domains."""
import ctypes
import os
from pathlib import Path
import sys


def check(library):
    byte = ctypes.c_ubyte
    pointer = ctypes.POINTER(byte)
    unsigned = ctypes.c_uint
    library.bd_gen.argtypes = [unsigned, unsigned, pointer, pointer, pointer]
    library.bd_eval.argtypes = [unsigned, unsigned, pointer, unsigned, pointer]
    checked = 0
    for depth in range(7):
        domain = 1 << depth
        for length in sorted({1, max(1, domain - 1), domain}):
            for selected in range(domain):
                key = byte * (16 + 17 * depth)
                left, right = key(), key()
                entropy = (byte * 32).from_buffer_copy(os.urandom(32))
                if library.bd_gen(depth, selected, entropy, left, right):
                    raise ValueError("Key generation failed")
                reply = byte * ((length + 7) // 8)
                first, second = reply(), reply()
                if library.bd_eval(depth, 0, left, length, first) or library.bd_eval(depth, 1, right, length, second):
                    raise ValueError("Evaluation failed")
                combined = int.from_bytes(first, "little") ^ int.from_bytes(second, "little")
                expected = 1 << selected if selected < length else 0
                if combined != expected:
                    raise ValueError(f"Unexpected response at depth={depth}, length={length}, index={selected}")
                checked += 1
    print(f"{checked} two-server DPF reconstruction cases passed.")


if __name__ == "__main__":
    check(ctypes.CDLL(str(Path(sys.argv[1]).resolve())))
