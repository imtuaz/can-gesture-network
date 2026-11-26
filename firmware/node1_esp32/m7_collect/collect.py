import csv
import os
import sys

import serial

LABELS = ["idle", "right", "left", "rotate"]
WINDOW = 100
OUT = "gestures_v2.csv"

GYRO_LSB_PER_DPS = 32.8   # MPU6050 o dai +-1000 do/s
MIN_PEAK_DPS = 150        # cu chi (tru idle) phai quay nhanh hon muc nay moi luu


def peak_gyro_dps(rows):
    """Toc do quay lon nhat trong cua so: max cua sqrt(gx^2 + gy^2 + gz^2), don vi do/s."""
    best = 0.0
    for v in rows:
        gx, gy, gz = v[3], v[4], v[5]
        mag = (gx * gx + gy * gy + gz * gz) ** 0.5 / GYRO_LSB_PER_DPS
        best = max(best, mag)
    return best


def next_sample_id(path):
    """So thu tu mau tiep theo = mau lon nhat da co + 1."""
    if not os.path.exists(path):
        return 0
    last = -1
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            last = max(last, int(row["sample"]))
    return last + 1


def count_label(path, label):
    if not os.path.exists(path):
        return 0
    seen = set()
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            if row["label"] == label:
                seen.add(row["sample"])
    return len(seen)


def main():
    if len(sys.argv) != 4 or sys.argv[2] not in LABELS:
        print("Cach dung: python collect.py <COM> <nhan> <session>   nhan =", ", ".join(LABELS))
        sys.exit(1)

    port, label, session = sys.argv[1], sys.argv[2], sys.argv[3]
    sample_id = next_sample_id(OUT)
    done = count_label(OUT, label)

    new_file = not os.path.exists(OUT)
    f = open(OUT, "a", newline="")
    writer = csv.writer(f)
    if new_file:
        writer.writerow(["sample", "session", "label", "i", "ax", "ay", "az", "gx", "gy", "gz"])

    ser = serial.Serial(port, 115200, timeout=1)
    print(f"Session {session}, nhan '{label}' (da co {done} mau). Nhan BOOT roi lam cu chi. Ctrl+C de dung.")

    rows = None
    try:
        while True:
            line = ser.readline().decode(errors="ignore").strip()
            if not line:
                continue
            if line.startswith("#"):
                print(line)
            elif line == "S":
                rows = []
            elif line == "E":
                if rows is not None and len(rows) == WINDOW:
                    peak = peak_gyro_dps(rows)
                    if label != "idle" and peak < MIN_PEAK_DPS:
                        print(f"  BO: dinh gyro {peak:.0f} do/s < {MIN_PEAK_DPS}. Lam nhanh, dut khoat hon")
                        rows = None
                        continue
                    for i, v in enumerate(rows):
                        writer.writerow([sample_id, session, label, i] + v)
                    f.flush()
                    sample_id += 1
                    done += 1
                    print(f"  Luu mau #{done} cua '{label}' (dinh gyro {peak:.0f} do/s)")
                else:
                    print("  Bo mau loi (thieu dong)")
                rows = None
            elif rows is not None:
                parts = line.split(",")
                if len(parts) == 6:
                    rows.append([int(p) for p in parts])
    except KeyboardInterrupt:
        print(f"\nDung. '{label}' co tong {done} mau trong {OUT}")
    finally:
        ser.close()
        f.close()


if __name__ == "__main__":
    main()
