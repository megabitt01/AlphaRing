"""What the offset tools share: where things are, and the offset headers' DefOffsets (no third-party imports)."""
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_MCC = r"C:\Program Files (x86)\Steam\steamapps\common\Halo The Master Chief Collection"

# header suffix -> the binary under the MCC folder
MODULES = {
    "halo1": r"halo1\halo1.dll",
    "halo2": r"halo2\halo2.dll",
    "halo3": r"halo3\halo3.dll",
    "halo3odst": r"halo3odst\halo3odst.dll",
    "halo4": r"halo4\halo4.dll",
    "haloreach": r"haloreach\haloreach.dll",
    "groundhog": r"groundhog\groundhog.dll",
    "mcc": r"mcc\binaries\win64\MCC-Win64-Shipping.exe",
}


def version():
    """The MCC version the offset headers are for (CMakeLists.txt VERSION)."""
    with open(os.path.join(ROOT, "CMakeLists.txt")) as f:
        return re.search(r'set\(VERSION "([^"]+)"\)', f.read()).group(1)


def inc_dir(ver=None):
    return os.path.join(ROOT, "lib", "game", "inc", ver or version())


def read_offsets(header):
    """[(name, value)] of the header's DefOffsets."""
    with open(header) as f:
        return [(m.group(1), int(m.group(2), 0))
                for m in re.finditer(r"^\s*DefOffset\(\s*(OFFSET_\w+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\)", f.read(), re.M)]
