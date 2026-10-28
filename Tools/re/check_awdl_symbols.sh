#!/bin/bash
# Check the exact AWDL/Skywalk linker symbols needed by the prototype.
# Every required symbol must occur in its owning KC dump with nlist type 0f.
# Usage: Tools/re/check_awdl_symbols.sh [dump_dir]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DUMP="${1:-$ROOT/Docs/re/io80211_25.6.0}"
IO80211="$DUMP/syms.txt"
SKYWALK="$DUMP/syms_com.apple.iokit.IOSkywalkFamily.txt"

python3 - "$IO80211" "$SKYWALK" <<'PY'
import pathlib
import sys

io_path, sky_path = map(pathlib.Path, sys.argv[1:])
required = [
    (io_path, "IO80211AWDLProtocol gMetaClass", "__ZN19IO80211AWDLProtocol10gMetaClassE"),
    (io_path, "IO80211AWDLProtocol ctor", "__ZN19IO80211AWDLProtocolC2EPK11OSMetaClass"),
    (io_path, "IO80211AWDLProtocol vtable", "__ZTV19IO80211AWDLProtocol"),
    (io_path, "IO80211VirtualInterface gMetaClass", "__ZN23IO80211VirtualInterface10gMetaClassE"),
    (io_path, "IO80211VirtualInterface ctor", "__ZN23IO80211VirtualInterfaceC2EPK11OSMetaClass"),
    (io_path, "IO80211VirtualInterface vtable", "__ZTV23IO80211VirtualInterface"),
    (io_path, "IO80211Controller::getAwdlInterface", "__ZN17IO80211Controller16getAwdlInterfaceEv"),
    (io_path, "IO80211Controller::setAwdlInterface", "__ZN17IO80211Controller16setAwdlInterfaceEP23IO80211VirtualInterface"),
    (sky_path, "IOSkywalkEthernetInterface::initRegistrationInfo", "__ZN26IOSkywalkEthernetInterface20initRegistrationInfoEPNS_16RegistrationInfoEjm"),
    (sky_path, "IOSkywalkEthernetInterface::registerEthernetInterface (logical link)", "__ZN26IOSkywalkEthernetInterface25registerEthernetInterfaceEPKNS_16RegistrationInfoEP20IOSkywalkLogicalLinkP25IOSkywalkPacketBufferPoolS6_j"),
    (sky_path, "IOSkywalkEthernetInterface::registerEthernetInterface (packet queues)", "__ZN26IOSkywalkEthernetInterface25registerEthernetInterfaceEPKNS_16RegistrationInfoEPP20IOSkywalkPacketQueuejP25IOSkywalkPacketBufferPoolS7_j"),
]

symbols = {}
for path in {p for p, _, _ in required}:
    if not path.is_file():
        print(f"ERROR missing dump: {path}", file=sys.stderr)
        sys.exit(2)
    entries = {}
    for line in path.read_text(errors="replace").splitlines():
        fields = line.split()
        if len(fields) >= 3:
            entries.setdefault(fields[2], []).append((fields[1], fields[0]))
    symbols[path] = entries

missing = []
print("AWDL/Skywalk required symbols (nlist type 0f):")
for path, label, name in required:
    matches = [(kind, address) for kind, address in symbols[path].get(name, []) if kind == "0f"]
    if matches:
        kind, address = matches[0]
        print(f"PASS {label}: 0f {address} {name}")
    else:
        kinds = ",".join(kind for kind, _ in symbols[path].get(name, [])) or "absent"
        print(f"FAIL {label}: expected 0f, found {kinds} ({name})")
        missing.append(name)

print(f"Summary: {len(required) - len(missing)}/{len(required)} required symbols have type 0f")
sys.exit(1 if missing else 0)
PY
