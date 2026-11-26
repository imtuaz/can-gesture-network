import collections
import csv
import os

import numpy as np
from sklearn.metrics import accuracy_score, confusion_matrix
from sklearn.model_selection import train_test_split
from sklearn.neural_network import MLPClassifier
from sklearn.preprocessing import StandardScaler

CSV_FILE = "gestures_v2.csv"
LABELS = ["idle", "right", "left", "rotate"]   # vi tri = ma cu chi gui qua CAN (0..3)
HIDDEN = 16


def load(path):
    windows = collections.defaultdict(list)
    label_of = {}
    session_of = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            s = int(row["sample"])
            windows[s].append([int(row[k]) for k in ["ax", "ay", "az", "gx", "gy", "gz"]])
            label_of[s] = row["label"]
            session_of[s] = row.get("session", "s1")
    ids = sorted(windows)
    X = np.array([windows[s] for s in ids], dtype=float)      # (so mau, 100, 6)
    y = np.array([LABELS.index(label_of[s]) for s in ids])    # 0..3
    sess = np.array([session_of[s] for s in ids])
    return X, y, sess


def stats(part):
    """part: (n, 6) -> 24 so: mean, std ('chia N', giong ban C), min, max cua tung truc."""
    return np.concatenate([part.mean(0), part.std(0), part.min(0), part.max(0)])


def features(w):
    """w: (100, 6) -> 48 so: 24 cua nua dau, 24 cua nua sau."""
    half = len(w) // 2
    return np.concatenate([stats(w[:half]), stats(w[half:])])


def make_model():
    return MLPClassifier(hidden_layer_sizes=(HIDDEN,), activation="relu",
                         alpha=1e-3, max_iter=5000, random_state=0)


def c_float(v):
    s = f"{v:.7g}"
    if "." not in s and "e" not in s:
        s += ".0"                      # 189 -> 189.0f (C khong chap nhan 189f)
    return s + "f"


def c_array(name, a):
    a = np.asarray(a)
    if a.ndim == 1:
        body = ", ".join(c_float(v) for v in a)
    else:                              # mang 2 chieu: moi hang 1 cap ngoac
        body = ",\n    ".join("{ " + ", ".join(c_float(v) for v in row) + " }" for row in a)
    dims = "".join(f"[{d}]" for d in a.shape)
    return f"static const float {name}{dims} = {{\n    {body}\n}};\n"


def main():
    X, y, sess = load(CSV_FILE)
    F = np.array([features(w) for w in X])
    print("So mau:", {LABELS[k]: int(v) for k, v in sorted(collections.Counter(y).items())})
    print("Session:", {str(k): int(v) for k, v in collections.Counter(sess).items()})

    # 1. Danh gia
    sessions = sorted(str(x) for x in set(sess))
    if len(sessions) >= 2:
        test_s = sessions[-1]
        print(f"Train tren {sessions[:-1]}, test tren session '{test_s}' (lan deo model chua thay)")
        Ftr, Fte, ytr, yte = F[sess != test_s], F[sess == test_s], y[sess != test_s], y[sess == test_s]
    else:
        print("Chi co 1 session: test tren 25% ngau nhien (lac quan hon thuc te)")
        Ftr, Fte, ytr, yte = train_test_split(F, y, test_size=0.25, stratify=y, random_state=0)
    sc = StandardScaler().fit(Ftr)
    clf = make_model().fit(sc.transform(Ftr), ytr)
    pred = clf.predict(sc.transform(Fte))
    print(f"\nDo chinh xac tap test: {accuracy_score(yte, pred) * 100:.1f}% ({len(yte)} mau)")
    print("Ma tran nham lan (hang = that, cot = doan):", LABELS)
    print(confusion_matrix(yte, pred, labels=range(len(LABELS))))

    # 2. Model cuoi: train tren toan bo du lieu de dua xuong ESP32
    sc = StandardScaler().fit(F)
    clf = make_model().fit(sc.transform(F), y)

    # Vector kiem tra: ESP32 chay lai mau nay luc khoi dong, so ket qua voi Python
    test_feat = F[0]
    h = np.maximum(0, sc.transform([test_feat])[0] @ clf.coefs_[0] + clf.intercepts_[0])
    test_logits = h @ clf.coefs_[1] + clf.intercepts_[1]

    n_param = sum(w.size for w in clf.coefs_) + sum(b.size for b in clf.intercepts_)
    out_h = os.path.join("..", "m7_gesture_tx", "gesture_model.h")
    with open(out_h, "w") as f:
        f.write("/* Tu dong tao boi train.py - khong sua tay */\n")
        f.write(f"/* {len(y)} mau, {n_param} tham so, nhan: {', '.join(LABELS)} */\n\n")
        f.write(f"#define N_FEAT {F.shape[1]}\n#define N_HID {HIDDEN}\n#define N_CLASS {len(LABELS)}\n\n")
        f.write(c_array("FEAT_MEAN", sc.mean_))
        f.write(c_array("FEAT_SCALE", sc.scale_))
        f.write(c_array("NN_W1", clf.coefs_[0]))           # [24][16]
        f.write(c_array("NN_B1", clf.intercepts_[0]))      # [16]
        f.write(c_array("NN_W2", clf.coefs_[1]))           # [16][4]
        f.write(c_array("NN_B2", clf.intercepts_[1]))      # [4]
        f.write(c_array("TEST_FEAT", test_feat))
        f.write(c_array("TEST_LOGITS", test_logits))
    print(f"\nDa ghi {out_h} ({n_param} tham so)")


if __name__ == "__main__":
    main()
