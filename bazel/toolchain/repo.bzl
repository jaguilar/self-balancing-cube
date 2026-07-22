def _detect_impl(repository_ctx):
    # Search for arm-none-eabi-gcc on host PATH
    gcc_path = repository_ctx.which("arm-none-eabi-gcc")
    if not gcc_path:
        gcc_path = repository_ctx.which("arm-none-eabi-gcc.exe")

    if not gcc_path:
        fail("\n\nCould not find 'arm-none-eabi-gcc' in the system PATH.\n" +
             "Please make sure the Arm GNU Toolchain is installed and added to your system PATH.\n\n")

    gcc_dir = gcc_path.dirname
    extension = ".exe" if gcc_path.basename.lower().endswith(".exe") else ""

    # Map of tool names to their paths on the host
    tool_names = ["gcc", "cpp", "ar", "nm", "ld", "objcopy", "objdump", "strip", "gcov"]
    paths = {}
    for t in tool_names:
        exe_base = "arm-none-eabi-"
        if t == "gcc":
            exe_base += "gcc"
        elif t == "cpp":
            exe_base += "cpp"
        elif t == "ar":
            exe_base += "gcc-ar"
        elif t == "nm":
            exe_base += "gcc-nm"
        elif t == "ld":
            exe_base += "ld"
        elif t == "objcopy":
            exe_base += "objcopy"
        elif t == "objdump":
            exe_base += "objdump"
        elif t == "strip":
            exe_base += "strip"
        elif t == "gcov":
            exe_base += "gcov"

        exe_path = gcc_dir.get_child(exe_base + extension)
        paths[t] = str(exe_path).replace("\\", "/")

    # Create source file with newlib header to run preprocessor
    repository_ctx.file(
        "empty.cc",
        """#if __has_include(<newlib.h>)
#include <newlib.h>
#endif
""",
    )
    result = repository_ctx.execute([str(gcc_path), "-E", "-Wp,-v", "-xc++", "empty.cc"])
    if result.return_code != 0:
        fail("Failed to run arm-none-eabi-gcc preprocessor:\n" + result.stderr)

    # Parse system include directories from compiler's verbose preprocessor output
    raw_dirs = []
    in_search_list = False
    for line in result.stderr.replace("\r\n", "\n").split("\n"):
        line = line.strip()
        if line == "#include <...> search starts here:":
            in_search_list = True
            continue
        if line == "End of search list.":
            in_search_list = False
            continue
        if in_search_list:
            normalized = line.replace("\\", "/")
            if normalized.endswith(" (framework directory)"):
                normalized = normalized[:-len(" (framework directory)")]
            raw_dirs.append(normalized)

    # Also extract directories of headers included by the preprocessor (e.g., newlib.h)
    for line in result.stdout.replace("\r\n", "\n").split("\n"):
        line = line.strip()
        if line.startswith("# ") and '"' in line:
            parts = line.split('"')
            if len(parts) >= 2:
                header_file = parts[1].replace("\\", "/")
                if header_file.startswith("/") or ":" in header_file:
                    last_slash = header_file.rfind("/")
                    if last_slash > 0:
                        raw_dirs.append(header_file[:last_slash])

    # Normalize directories and resolve symlinks (e.g. /usr/lib/arm-none-eabi/include -> /usr/include/newlib)
    include_dirs = []
    for d in raw_dirs:
        p = repository_ctx.path(d)
        if p.exists:
            norm_str = str(p).replace("\\", "/")
            if norm_str not in include_dirs:
                include_dirs.append(norm_str)
            real_str = str(p.realpath).replace("\\", "/")
            if real_str not in include_dirs:
                include_dirs.append(real_str)
        else:
            d_norm = d.replace("\\", "/")
            if d_norm not in include_dirs:
                include_dirs.append(d_norm)

    if not include_dirs:
        fail("Could not detect system include directories for arm-none-eabi-gcc.\nCompiler output:\n" + result.stderr)

    # Generate the paths.bzl containing compiler metadata
    bzl_content = "TOOL_PATHS = {\n"
    for k, v in paths.items():
        bzl_content += "    \"%s\": \"%s\",\n" % (k, v)
    bzl_content += "}\n\n"
    bzl_content += "SYS_INCLUDE_DIRS = [\n"
    for d in include_dirs:
        bzl_content += "    \"%s\",\n" % d
    bzl_content += "]\n"

    repository_ctx.file("paths.bzl", bzl_content)
    repository_ctx.file("BUILD", "exports_files(['paths.bzl'])")

detect_arm_none_eabi_toolchain = repository_rule(
    implementation = _detect_impl,
    local = True,
)

def _arm_toolchain_ext_impl(ctx):
    detect_arm_none_eabi_toolchain(name = "arm_none_eabi_config")

arm_toolchain_ext = module_extension(
    implementation = _arm_toolchain_ext_impl,
)
