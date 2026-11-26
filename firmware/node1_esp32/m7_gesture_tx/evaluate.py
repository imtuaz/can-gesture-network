import csv
import os
import re
import sys

import serial

LABELS = ["idle", "right", "left", "rotate"]
LINE_RE = re.compile(r"Cu chi:\s+(\w+)\s+xac suat\s+(\d+)%\s+\(tinh mat (\d+) us\)\s+->\s+(gui|bo qua)")
OUT = "eval_log.csv"


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in LABELS:
        print("Cach dung: python evaluate.py <COM> <nhan that>   nhan =", ", ".join(LABELS))
        sys.exit(1)
    port, truth = sys.argv[1], sys.argv[2]

    new_file = not os.path.exists(OUT)
    f = open(OUT, "a", newline="")
    w = csv.writer(f)
    if new_file:
        w.writerow(["truth", "pred", "prob", "us", "sent"])

    ser = serial.Serial(port, 115200, timeout=1)
    print(f"Lam cu chi '{truth}'. Ctrl+C de dung.")
    n = correct = wrong = rejected = 0
    try:
        while True:
            line = ser.readline().decode(errors="ignore").strip()
            if line.startswith("LOI"):
                print("  ", line)
            m = LINE_RE.search(line)
            if not m:
                continue
            pred, prob, us, action = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)
            sent = action == "gui"
            w.writerow([truth, pred, prob, us, int(sent)])
            f.flush()
            n += 1
            if not sent and pred == "idle":
                rejected += 1
                tag = "idle -> khong gui (DUNG neu ban khong lam cu chi)" if truth == "idle" else "doan idle -> khong gui"
            elif not sent:
                rejected += 1
                tag = "bo qua (chua chac)"
            elif pred == truth:
                correct += 1
                tag = "DUNG"
            else:
                wrong += 1
                tag = "SAI"
            print(f"  #{n:2d}  doan {pred:6s} {prob:3d}%  -> {tag}")
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        f.close()

    print(f"\nTong ket '{truth}': {n} lan ESP32 bat duoc cu chi")
    if truth == "idle":
        print(f"  Gui nham lenh (bao dong gia): {correct + wrong}   khong gui (dung): {rejected}")
    elif n:
        print(f"  Dung: {correct} ({correct / n * 100:.0f}%)   Sai: {wrong}   Bo qua: {rejected}")
    print("  (Neu ban lam 20 lan ma n < 20: so con lai la lan ESP32 khong bat duoc)")


if __name__ == "__main__":
    main()
