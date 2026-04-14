"""
PlatformIO extra_script: fix_assembler.py

On Windows, xtensa-esp32-elf-gcc passes --longcalls to the assembler, but it
may find the system 'as.exe' (from Git for Windows / MSYS) before the Xtensa
toolchain's 'xtensa-esp32-elf-as.exe'.  That system assembler does not
understand --longcalls and the build fails.

The -B <prefix> GCC flag tells the compiler to look for helper programs
(as, ld, ...) using the form  <prefix>as,  <prefix>ld, etc.  By setting the
prefix to  <toolchain>/bin/xtensa-esp32-elf-  GCC will resolve the assembler
as  <toolchain>/bin/xtensa-esp32-elf-as  which is the correct Xtensa binary.
"""

Import("env")  # noqa: F821 - SCons injects 'env'
import os
import shutil
import sys


if sys.platform == "win32":
    try:
        platform = env.PioPlatform()
        toolchain_dir = platform.get_package_dir("toolchain-xtensa-esp32")
    except Exception:
        toolchain_dir = None

    if toolchain_dir:
        toolchain_bin = os.path.join(toolchain_dir, "bin")
        b_prefix = os.path.join(toolchain_bin, "xtensa-esp32-elf-").replace("\\", "/")
        assembler = os.path.join(toolchain_bin, "xtensa-esp32-elf-as.exe").replace("\\", "/")
        compiler_c = os.path.join(toolchain_bin, "xtensa-esp32-elf-gcc.exe").replace("\\", "/")
        compiler_cxx = os.path.join(toolchain_bin, "xtensa-esp32-elf-g++.exe").replace("\\", "/")
        archiver = os.path.join(toolchain_bin, "xtensa-esp32-elf-ar.exe").replace("\\", "/")
        ranlib = os.path.join(toolchain_bin, "xtensa-esp32-elf-ranlib.exe").replace("\\", "/")

        # Some Windows PlatformIO setups ship toolchain-xtensa-esp-elf without
        # the generic xtensa-esp-elf-ld.exe entrypoint, but the linker driver
        # still tries to execute it during final link.
        shared_toolchain_dir = platform.get_package_dir("toolchain-xtensa-esp-elf")
        if shared_toolchain_dir:
            shared_bin = os.path.join(shared_toolchain_dir, "bin")
            generic_ld = os.path.join(shared_bin, "xtensa-esp-elf-ld.exe")
            esp32_ld = os.path.join(shared_bin, "xtensa-esp32-elf-ld.exe")
            if not os.path.exists(generic_ld) and os.path.exists(esp32_ld):
                shutil.copy2(esp32_ld, generic_ld)
                print("fix_assembler: created missing linker shim {}".format(generic_ld))

        # GCC sometimes still resolves helper tools via PATH on Windows, so
        # force both the lookup prefix and the process environment.
        env.PrependENVPath("PATH", toolchain_bin)
        env.Replace(
            AS=assembler,
            CC=compiler_c,
            CXX=compiler_cxx,
            LINK=compiler_cxx,
            AR=archiver,
            RANLIB=ranlib,
        )
        env.Append(CCFLAGS=["-B{}".format(b_prefix)])
        env.Append(CXXFLAGS=["-B{}".format(b_prefix)])
        env.Append(LINKFLAGS=["-B{}".format(b_prefix), "-fuse-ld=bfd"])

        print(
            "fix_assembler: forcing Xtensa tools from {} with compiler {} and assembler {}".format(
                toolchain_bin, compiler_cxx, assembler
            )
        )
    else:
        print("fix_assembler: toolchain-xtensa-esp32 package not found, skipping.")
