"""Compile the actual sender method against a synchronous UDP recording model."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--baseline", action="store_true")
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
source_path = "sources/led-drivers/net/DriverNetWled.cpp"
source = subprocess.check_output(["git", "show", "v22.0.0.0:" + source_path], cwd=root, text=True) if args.baseline else (root / source_path).read_text()
start = source.index("int DriverNetWled::writeFiniteColors(")
brace = source.index("{", start)
depth = 1
end = brace + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
method = source[start:end]
build = root / "build-wled-packets" / (("baseline" if args.baseline else "optimized") + ("-sanitized" if args.sanitize else ""))
build.mkdir(parents=True, exist_ok=True)
harness = r'''
#include <array>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <new>
#include <image/ColorRgb.h>
static bool tracking = false;
static std::size_t allocations = 0;
void* operator new(std::size_t size) {
    if (tracking) ++allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
struct Datagram { unsigned size = 0; std::array<uint8_t, 1472> data{}; };
class DriverNetWled {
public:
    unsigned _ledCount = 0, _ledRGBCount = 0, packetCount = 0;
    int result = 0;
    std::array<uint8_t, 1472> _udpPacket{};
    std::array<Datagram, 140> packets{};
    void setLedCount(int count) { _ledCount = count; _ledRGBCount = count * sizeof(ColorRgb); }
    int writeBytes(unsigned size, const uint8_t* bytes) {
        if (size > 1472 || packetCount >= packets.size()) std::abort();
        auto& p = packets[packetCount++]; p.size = size;
        std::memcpy(p.data.data(), bytes, size);
        return result;
    }
    int writeFiniteColors(const std::vector<ColorRgb>& colors);
};
''' + method + r'''
static unsigned checks = 0;
static void require(bool okay) { ++checks; if (!okay) std::abort(); }
int main() {
    static_assert(sizeof(ColorRgb) == 3);
    DriverNetWled sender;
    for (unsigned count : {1u, 9u, 165u, 166u, 489u, 490u, 491u, 978u, 979u, 2000u, 65535u}) {
        for (unsigned black : {0u, 1u}) {
            std::vector<ColorRgb> colors(count);
            for (unsigned i = 0; i < count; ++i) if (!black)
                colors[i] = ColorRgb(uint8_t(i*7+3), uint8_t(i*11+5), uint8_t(i*13+9));
            sender.setLedCount(count); sender.packetCount = 0; sender.result = 0;
            int rc = sender.writeFiniteColors(colors);
            require(rc == (count <= 490 ? 0 : int(count*3)));
            require(sender.packetCount == (count <= 490 ? 1 : (count+488)/489));
            unsigned offset = 0;
            for (unsigned p = 0; p < sender.packetCount; ++p) {
                auto& packet = sender.packets[p];
                unsigned header = count <= 490 ? 2 : 4;
                require(packet.data[0] == (count <= 490 ? 2 : 4));
                require(packet.data[1] == 255);
                require((packet.size-header)%3 == 0);
                if (header == 4) require(((unsigned(packet.data[2])<<8)|packet.data[3]) == offset);
                unsigned pixels = (packet.size-header)/3;
                require(pixels <= count-offset);
                require(std::memcmp(packet.data.data()+header, colors.data()+offset, pixels*3) == 0);
                offset += pixels;
            }
            require(offset == count);
        }
    }
    std::vector<ColorRgb> sk(166, ColorRgb(0,0,0));
    sender.setLedCount(165); sender.packetCount = 0;
    require(sender.writeFiniteColors(sk) == 0 && sender.packetCount == 0 && sender._ledCount == 166);
    sender.result = -1; sender.packetCount = 0;
    require(sender.writeFiniteColors(sk) == -1);
    sender.result = 0; sender.packetCount = 0;
    require(sender.writeFiniteColors(sk) == 0 && sender.packets[0].size == 500);
    allocations = 0; tracking = true;
    for (unsigned i = 0; i < 1000; ++i) {
        sender.packetCount = 0;
        sender.writeFiniteColors(sk);
    }
    tracking = false;
    std::cout << "{\"checks\":" << checks << ",\"steady166FrameAllocationsPer1000\":" << allocations << "}\n";
}
'''
cpp = build / "actual-sender-model.cpp"
cpp.write_text(harness)
flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else []
command = ["g++", "-std=c++20", "-O2", "-I", str(root / "include"), *flags, str(cpp), "-o", str(build / "probe")]
subprocess.run(command, check=True)
result = json.loads(subprocess.check_output([str(build / "probe")], text=True))
expected = 1000 if args.baseline else 0
assert result["steady166FrameAllocationsPer1000"] == expected, result
result.update({"baseline": args.baseline, "sanitized": args.sanitize, "actualMethodSha256": hashlib.sha256(method.encode()).hexdigest(), "limits": "Host recording/allocator model; not live UDP delivery, TV CPU speedup, or an ESP output test."})
(build / "result.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
