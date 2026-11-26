import re
import sys
import zipfile


def parse_rate(text):
    m = re.search(r"samplerate=([\d.]+)\s*([kMG]?)Hz", text)
    scale = {"": 1, "k": 1e3, "M": 1e6, "G": 1e9}[m.group(2)]
    return int(float(m.group(1)) * scale)


def main():
    if len(sys.argv) < 3:
        print("usage: python sr2csv.py in.sr out.csv [D0 D1 ...] [--pad-idle-us N]")
        sys.exit(1)
    args = sys.argv[1:]
    pad_us = 0.0
    if "--pad-idle-us" in args:
        k = args.index("--pad-idle-us")
        pad_us = float(args[k + 1])
        del args[k:k + 2]
    src, dst, wanted = args[0], args[1], args[2:]

    z = zipfile.ZipFile(src)
    meta = z.read("metadata").decode()
    rate = parse_rate(meta)
    names = dict(re.findall(r"probe(\d+)=(\S+)", meta))            # '1' -> 'D0'
    chunks = sorted((n for n in z.namelist() if n.startswith("logic-1")),
                    key=lambda n: int(n.rsplit("-", 1)[1]))
    data = b"".join(z.read(n) for n in chunks)                     # 1 byte / mau

    bit_of = {names[k]: int(k) - 1 for k in names}                 # 'D0' -> bit 0
    if not wanted:                                                 # kenh co doi muc
        seen = 0
        for b in set(data):
            seen |= b ^ data[0]
        wanted = [n for n, bit in sorted(bit_of.items(), key=lambda x: x[1]) if seen >> bit & 1]
    bits = [bit_of[n] for n in wanted]
    mask = sum(1 << b for b in bits)

    rows = 0
    with open(dst, "w") as f:
        f.write(f"# sample_rate={rate}\n")
        f.write("time_s," + ",".join(wanted) + "\n")
        prev = None
        last = len(data) - 1
        t0 = pad_us * 1e-6
        if pad_us > 0:                                             # bus ranh truoc capture
            f.write("0.000000000," + ",".join("1" for _ in bits) + "\n")
            rows += 1
        for i, b in enumerate(data):
            b &= mask
            if b != prev or i == last:
                f.write(f"{t0 + i / rate:.9f}," + ",".join(str(b >> k & 1) for k in bits) + "\n")
                prev = b
                rows += 1
    print(f"{src}: {len(data)} mau @ {rate} Hz ({len(data) / rate * 1e3:.1f} ms), "
          f"kenh {wanted} -> {dst} ({rows} dong)" + (f", them {pad_us:g} us bus ranh" if pad_us else ""))


if __name__ == "__main__":
    main()
