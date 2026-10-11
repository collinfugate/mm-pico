#!/usr/bin/env python3
"""Build and flash the Multi-Metrics Pico firmware.

    python flash.py                  ask which board, then flash it
    python flash.py pico             flash a Pico
    python flash.py pico_w           flash a Pico W (asks for Wi-Fi details)
    python flash.py pico_w --build   compile src/ first, then flash
    python flash.py all --build-only compile both images, flash nothing

A box that already runs this firmware and is plugged in over USB is switched
into flashing mode automatically (this part uses pyserial when it is
installed). Otherwise hold the BOOTSEL button on the Pico while plugging in
its USB cable.

Compiling needs the Pico SDK and an ARM compiler. The first build downloads
pinned versions of both into ~/.pico-sdk, the same folder and layout the
Raspberry Pi Pico VS Code extension uses, so the two share one install.

Needs Python 3.8 or newer. Nothing outside the standard library is required.
"""
import argparse
import os
import platform
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

REPO = Path(__file__).resolve().parent
BUILD = REPO / "build"
PREBUILT = REPO / "prebuilt"
TARGET = "mm_firmware"
BOARDS = {"pico": "Pico", "pico_w": "Pico W"}

# ---------------------------------------------------------------- pinned tools
# These must match the three version lines at the top of CMakeLists.txt.
SDK_VERSION = "2.3.1"
TOOLCHAIN_VERSION = "15_2_Rel1"
PICOTOOL_VERSION = "2.3.1"
CMAKE_VERSION = "v4.3.4"
NINJA_VERSION = "v1.13.2"

TOOLS = Path.home() / ".pico-sdk"
WINDOWS = platform.system() == "Windows"
MAC = platform.system() == "Darwin"
EXE = ".exe" if WINDOWS else ""

_GH = "https://github.com/"
_ARM = ("https://armkeil.blob.core.windows.net/developer/files/downloads/gnu/"
        "15.2.rel1/binrel/arm-gnu-toolchain-15.2.rel1-")
_RPI = _GH + "raspberrypi/pico-sdk-tools/releases/download/v2.3.1-0/"
_CMAKE = _GH + "Kitware/CMake/releases/download/v4.3.4/cmake-4.3.4-"
_NINJA = _GH + "ninja-build/ninja/releases/download/v1.13.2/ninja-"

# The SDK is fetched as plain zips so git is not required. The library commits
# are the ones SDK 2.3.1 pins as submodules.
SDK_PARTS = [
    (_GH + "raspberrypi/pico-sdk/archive/079c6f39023649b154152db30f1d781e884879bc.zip", ""),
    (_GH + "hathach/tinyusb/archive/86ad6e56c1700e85f1c5678607a762cfe3aa2f47.zip", "lib/tinyusb"),
    (_GH + "georgerobotics/cyw43-driver/archive/055d64274b014dd7b1c2fc94d26e8a18face7124.zip", "lib/cyw43-driver"),
    (_GH + "lwip-tcpip/lwip/archive/77dcd25a72509eb83f72b033d219b1d40cd8eb95.zip", "lib/lwip"),
]
if WINDOWS:
    TOOLCHAIN_URL = _ARM + "mingw-w64-x86_64-arm-none-eabi.zip"
    PICOTOOL_URL = _RPI + "picotool-2.3.1-x64-win.zip"
    PIOASM_URL = _RPI + "pico-sdk-tools-2.3.1-x64-win.zip"
    CMAKE_URL = _CMAKE + "windows-x86_64.zip"
    NINJA_URL = _NINJA + "win.zip"
elif MAC:
    # ARM only publishes this compiler version for Apple Silicon.
    TOOLCHAIN_URL = (_ARM + "darwin-arm64-arm-none-eabi.tar.xz"
                     if platform.machine() == "arm64" else None)
    PICOTOOL_URL = _RPI + "picotool-2.3.1-mac.zip"
    PIOASM_URL = _RPI + "pico-sdk-tools-2.3.1-mac.zip"
    CMAKE_URL = _CMAKE + "macos-universal.tar.gz"
    NINJA_URL = _NINJA + "mac.zip"
else:
    TOOLCHAIN_URL = PICOTOOL_URL = PIOASM_URL = CMAKE_URL = NINJA_URL = None

# ------------------------------------------------------------ Wi-Fi settings
# The Pico W firmware reads its Wi-Fi name and password from the last 4 KB of
# flash (see src/wifi.c). flash.py writes them there by adding one block to the
# UF2 image. Flashing without Wi-Fi details leaves that area untouched, so the
# box keeps its settings across firmware updates.
WIFI_ADDRESS = 0x10000000 + 2 * 1024 * 1024 - 4096
USB_ID = (0xCAFE, 0x4005)       # vendor and product id in src/usb_descriptors.c
ENTER_BOOTSEL = 179             # op code in src/mm.h
WIFI_MAGIC = b"MMWF"
UF2_HEADER = "<8I"
UF2_MAGIC = (0x0A324655, 0x9E5D5157, 0x0AB16F30)


class Failure(Exception):
    """A problem to show the user without a traceback."""


def say(text=""):
    print(text, flush=True)


# ------------------------------------------------------------------ installing
def download(url, dest):
    say(f"  downloading {url.rsplit('/', 1)[-1]}")
    curl = shutil.which("curl")
    if curl:
        # curl uses the operating system's certificates, which avoids the
        # missing-certificate error of a fresh python.org install on macOS.
        cmd = [curl, "-fL", "--retry", "3", "--progress-bar", "-o", str(dest), url]
        if subprocess.run(cmd).returncode != 0:
            raise Failure(f"Download failed: {url}")
    else:
        try:
            urllib.request.urlretrieve(url, dest)
        except OSError as err:
            raise Failure(f"Download failed: {url}\n  {err}")


def _strip_count(names):
    """1 if every entry sits under one top-level folder, else 0."""
    paths = [PurePosixPath(n).parts for n in names]
    tops = {p[0] for p in paths if p}
    return 1 if len(tops) == 1 and any(len(p) > 1 for p in paths) else 0


def unpack(archive, dest):
    """Extract a zip or tar into dest, dropping a lone top-level folder."""
    dest.mkdir(parents=True, exist_ok=True)
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as z:
            infos = z.infolist()
            strip = _strip_count(i.filename for i in infos)
            for info in infos:
                parts = PurePosixPath(info.filename).parts[strip:]
                if not parts or ".." in parts:
                    continue
                out = dest.joinpath(*parts)
                mode = info.external_attr >> 16
                if info.is_dir():
                    out.mkdir(parents=True, exist_ok=True)
                    continue
                out.parent.mkdir(parents=True, exist_ok=True)
                if stat.S_ISLNK(mode):
                    if not WINDOWS:
                        os.symlink(z.read(info).decode(), out)
                    continue
                with z.open(info) as src, open(out, "wb") as dst:
                    shutil.copyfileobj(src, dst)
                if mode & 0o111:
                    out.chmod(0o755)
    else:
        with tarfile.open(archive) as t:
            members = t.getmembers()
            strip = _strip_count(m.name for m in members)
            keep = []
            for m in members:
                parts = PurePosixPath(m.name).parts[strip:]
                if not parts:
                    continue
                m.name = "/".join(parts)
                if m.islnk():
                    m.linkname = "/".join(PurePosixPath(m.linkname).parts[strip:])
                keep.append(m)
            extra = {"filter": "tar"} if hasattr(tarfile, "tar_filter") else {}
            t.extractall(dest, members=keep, **extra)


def install(name, dest, marker, parts, finish=None):
    """Make sure dest holds the given archives. Returns dest."""
    if (dest / marker).exists():
        return dest
    if dest.exists():
        raise Failure(f"{dest} exists but looks incomplete ({marker} is missing).\n"
                      "  Delete that folder and run this again.")
    if any(url is None for url, _ in parts):
        raise Failure(f"No {name} download is available for this computer.")
    say(f"Installing {name} into {dest}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    staging = dest.with_name(dest.name + ".partial")
    shutil.rmtree(staging, ignore_errors=True)
    archive = dest.with_name(dest.name + ".download")
    try:
        for url, sub in parts:
            download(url, archive)
            say("  unpacking")
            unpack(archive, staging / sub if sub else staging)
    finally:
        if archive.exists():
            archive.unlink()
    if finish:
        finish(staging)
    if not (staging / marker).exists():
        raise Failure(f"The {name} download did not contain {marker}.")
    for attempt in range(10):   # antivirus can hold new files open for a moment
        try:
            staging.rename(dest)
            break
        except PermissionError:
            if attempt == 9:
                raise
            time.sleep(1)
    return dest


def _mac_cmake_bin(folder):
    """The macOS download is an app bundle; expose its tools as bin/."""
    app_bin = Path("CMake.app") / "Contents" / "bin"
    if (folder / app_bin).is_dir() and not (folder / "bin").exists():
        os.symlink(app_bin, folder / "bin")     # relative, so it survives the rename


def find_program(name, folder, version, url, finish=None):
    """Prefer a copy already in ~/.pico-sdk, then PATH, then download one."""
    relative = Path("bin") / (name + EXE) if name == "cmake" else Path(name + EXE)
    installed = sorted((TOOLS / folder).glob(f"*/{relative.as_posix()}"))
    if installed:
        return installed[-1]
    on_path = shutil.which(name)
    if on_path:
        return Path(on_path)
    return install(name, TOOLS / folder / version, relative, [(url, "")], finish) / relative


def check_pins():
    text = (REPO / "CMakeLists.txt").read_text()
    for key, mine in (("sdkVersion", SDK_VERSION), ("toolchainVersion", TOOLCHAIN_VERSION),
                      ("picotoolVersion", PICOTOOL_VERSION)):
        found = re.search(rf"set\({key}\s+(\S+)\)", text)
        if found and found.group(1) != mine:
            raise Failure(f"CMakeLists.txt sets {key} to {found.group(1)} but flash.py is "
                          f"pinned to {mine}.\n  Update the versions and download links at "
                          "the top of flash.py to match.")


def ensure_tools():
    """Locate or install everything a build needs. Returns CMake arguments."""
    check_pins()
    sdk = install("Pico SDK", TOOLS / "sdk" / SDK_VERSION, "pico_sdk_init.cmake", SDK_PARTS)
    picotool = install("picotool", TOOLS / "picotool" / PICOTOOL_VERSION,
                       "picotool/picotoolConfig.cmake", [(PICOTOOL_URL, "")])
    pioasm = install("pioasm", TOOLS / "tools" / SDK_VERSION,
                     "pioasm/pioasmConfig.cmake", [(PIOASM_URL, "")])
    compiler = Path("bin") / ("arm-none-eabi-gcc" + EXE)
    toolchain = TOOLS / "toolchain" / TOOLCHAIN_VERSION
    if not (toolchain / compiler).exists() and TOOLCHAIN_URL is None:
        if not shutil.which("arm-none-eabi-gcc"):
            raise Failure("No ARM compiler download exists for this computer.\n"
                          "  Install arm-none-eabi-gcc and run this again.")
        toolchain = None          # CMake finds the one on PATH
    else:
        install("ARM compiler", toolchain, compiler, [(TOOLCHAIN_URL, "")])
    cmake = find_program("cmake", "cmake", CMAKE_VERSION, CMAKE_URL,
                         _mac_cmake_bin if MAC else None)
    ninja = find_program("ninja", "ninja", NINJA_VERSION, NINJA_URL)
    defines = {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_MAKE_PROGRAM": ninja,
        "PICO_SDK_PATH": sdk,
        "picotool_DIR": picotool / "picotool",
        "pioasm_DIR": pioasm / "pioasm",
    }
    if toolchain:
        defines["PICO_TOOLCHAIN_PATH"] = toolchain
    return cmake, defines


# -------------------------------------------------------------------- building
def run_quiet(cmd, what):
    result = subprocess.run([str(c) for c in cmd], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, errors="replace")
    if result.returncode != 0:
        say(result.stdout)
        raise Failure(f"{what} failed. The full output is above.")


def build(board, tools):
    cmake, defines = tools
    out = BUILD / board
    say(f"Compiling for {BOARDS[board]}...")
    configure = [cmake, "-S", REPO, "-B", out, "-G", "Ninja", f"-DPICO_BOARD={board}"]
    configure += [f"-D{k}={Path(v).as_posix() if isinstance(v, Path) else v}"
                  for k, v in defines.items()]
    run_quiet(configure, "Configuring the build")
    run_quiet([cmake, "--build", out], "Compiling")
    uf2 = out / f"{TARGET}.uf2"
    if not uf2.exists():
        raise Failure(f"The build finished but {uf2} is missing.")
    say(f"  built {uf2.relative_to(REPO)}")
    return uf2


def image_for(board, force_build):
    """Use the prebuilt image when there is one, otherwise compile."""
    prebuilt = PREBUILT / f"{TARGET}_{board}.uf2"
    if prebuilt.exists() and not force_build:
        say(f"Using prebuilt image {prebuilt.relative_to(REPO)}")
        return prebuilt
    return build(board, ensure_tools())


# -------------------------------------------------------------- Wi-Fi settings
def wifi_page(ssid, password):
    """The 256-byte settings page the firmware reads. Layout matches wifi.c."""
    name, key = ssid.encode(), password.encode()
    if not 1 <= len(name) <= 32:
        raise Failure("The Wi-Fi name must be 1 to 32 bytes long.")
    if key and not 8 <= len(key) <= 63:
        raise Failure("The Wi-Fi password must be 8 to 63 bytes long, or empty for an open network.")
    page = WIFI_MAGIC + name.ljust(33, b"\0") + key.ljust(65, b"\0")
    return page.ljust(256, b"\xff")


def add_wifi(uf2, ssid, password):
    """Return the UF2 image with one extra block holding the Wi-Fi settings."""
    if len(uf2) % 512 or not uf2:
        raise Failure("The firmware image is not a valid UF2 file.")
    blocks = [bytearray(uf2[i:i + 512]) for i in range(0, len(uf2), 512)]
    first = struct.unpack_from(UF2_HEADER, blocks[0])
    if (first[0], first[1]) != UF2_MAGIC[:2]:
        raise Failure("The firmware image is not a valid UF2 file.")
    extra = bytearray(512)
    struct.pack_into(UF2_HEADER, extra, 0, first[0], first[1], first[2],
                     WIFI_ADDRESS, 256, 0, 0, first[7])
    extra[32:32 + 256] = wifi_page(ssid, password)
    struct.pack_into("<I", extra, 508, UF2_MAGIC[2])
    blocks.append(extra)
    for number, block in enumerate(blocks):
        struct.pack_into("<2I", block, 20, number, len(blocks))
    return b"".join(blocks)


def ask_wifi(args):
    if args.ssid is not None:
        return args.ssid, args.password or ""
    if not sys.stdin.isatty():
        return None, None
    say()
    say("Wi-Fi setup. Press Enter at the name to keep the box's current settings.")
    ssid = input("  Wi-Fi network name: ").strip()
    if not ssid:
        return None, None
    return ssid, input("  Wi-Fi password: ")


# -------------------------------------------------------------------- flashing
def find_bootsel():
    """Return the drive of a Pico in BOOTSEL mode, or None."""
    if WINDOWS:
        import ctypes
        import string
        ctypes.windll.kernel32.SetErrorMode(1)    # no "insert a disk" pop-ups
        present = ctypes.windll.kernel32.GetLogicalDrives()
        drives = [Path(f"{letter}:/") for bit, letter in enumerate(string.ascii_uppercase)
                  if present >> bit & 1]
    elif MAC:
        drives = list(Path("/Volumes").iterdir())
    else:
        user = os.environ.get("USER", "")
        drives = [d for base in (f"/media/{user}", f"/run/media/{user}")
                  if Path(base).is_dir() for d in Path(base).iterdir()]
    for drive in drives:
        try:
            if (drive / "INFO_UF2.TXT").is_file():
                return drive
        except OSError:
            pass
    return None


def request_bootsel():
    """Ask a running box on USB to restart into BOOTSEL mode. True if asked."""
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        return False
    for port in list_ports.comports():
        if (port.vid, port.pid) != USB_ID:
            continue
        try:
            with serial.Serial(port.device, 115200, timeout=0.2) as link:
                link.write(bytes([ENTER_BOOTSEL]))
        except (OSError, serial.SerialException):
            pass                    # the port vanishes as the box restarts
        return True
    return False


def wait_for_bootsel(seconds=180):
    drive = find_bootsel()
    if not drive and request_bootsel():
        say("Asked the box to restart into flashing mode...")
        deadline = time.time() + 8
        while not drive and time.time() < deadline:
            time.sleep(0.5)
            drive = find_bootsel()
    if not drive:
        say()
        say("Waiting for the Pico. Hold its BOOTSEL button while plugging in the USB cable.")
        deadline = time.time() + seconds
        while not drive:
            if time.time() > deadline:
                raise Failure("No Pico appeared. Check that the cable carries data, not just power.")
            time.sleep(0.5)
            drive = find_bootsel()
    info = (drive / "INFO_UF2.TXT").read_text(errors="replace")
    if "RP2350" in info:
        raise Failure("That board is a Pico 2. This firmware is for the Pico and Pico W.")
    return drive


def flash(image, drive):
    say(f"Flashing {drive} ...")
    try:
        with open(drive / "firmware.uf2", "wb") as f:
            f.write(image)
            f.flush()
            os.fsync(f.fileno())
    except OSError:
        # The Pico reboots the moment the last block lands, which can pull the
        # drive away before the operating system finishes closing the file.
        time.sleep(2)
        if find_bootsel():
            raise
    deadline = time.time() + 15
    while find_bootsel() and time.time() < deadline:
        time.sleep(0.25)
    if find_bootsel():
        raise Failure("The Pico did not restart. Unplug it and try again.")
    say("Done. The Pico restarted with the new firmware.")


def choose_board():
    if not sys.stdin.isatty():
        raise Failure("Say which board to flash: pico or pico_w.")
    say("Which board is this?")
    keys = list(BOARDS)
    for number, key in enumerate(keys, 1):
        say(f"  {number}) {BOARDS[key]}")
    while True:
        answer = input("Enter 1 or 2: ").strip().lower()
        if answer in BOARDS:
            return answer
        if answer.isdigit() and 1 <= int(answer) <= len(keys):
            return keys[int(answer) - 1]


def main():
    parser = argparse.ArgumentParser(description="Build and flash the Multi-Metrics Pico firmware.")
    parser.add_argument("board", nargs="?", choices=[*BOARDS, "all"],
                        help="pico or pico_w ('all' only with --build-only)")
    parser.add_argument("--build", action="store_true",
                        help="compile src/ even when a prebuilt image exists")
    parser.add_argument("--build-only", action="store_true", help="compile but do not flash")
    parser.add_argument("--clean", action="store_true", help="delete the build folder first")
    parser.add_argument("--ssid", help="Wi-Fi network name to store on a Pico W")
    parser.add_argument("--password", help="Wi-Fi password to store on a Pico W")
    args = parser.parse_args()

    if args.clean:
        shutil.rmtree(BUILD, ignore_errors=True)
    if args.build_only:
        tools = ensure_tools()
        for board in (BOARDS if args.board in (None, "all") else [args.board]):
            build(board, tools)
        return
    if args.board == "all":
        raise Failure("'all' only works with --build-only.")

    board = args.board or choose_board()
    image = image_for(board, args.build).read_bytes()
    if board == "pico_w":
        ssid, password = ask_wifi(args)
        if ssid:
            image = add_wifi(image, ssid, password)
            say(f"The box will join '{ssid}'.")
        else:
            say("Leaving the box's Wi-Fi settings as they are.")
    flash(image, wait_for_bootsel())


if __name__ == "__main__":
    try:
        main()
    except Failure as problem:
        say(f"\nERROR: {problem}")
        sys.exit(1)
    except KeyboardInterrupt:
        say("\nCancelled.")
        sys.exit(1)
