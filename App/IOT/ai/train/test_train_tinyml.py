"""Testcase cho TODO 1-5 của train_tinyml.py (issue #30). Không cần TensorFlow, chạy vài giây.

Chạy (gốc repo):
    pytest App/IOT/ai/train/test_train_tinyml.py -v
Chạy 1 nhóm:
    pytest App/IOT/ai/train/test_train_tinyml.py -v -k norm
"""

import re
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import train_tinyml as tt  # noqa: E402

NORM = {"temp_mean": 29.0, "temp_std": 1.0, "hum_mean": 50.0, "hum_std": 10.0}


# ---------- TODO 1: split_by_file ----------
@pytest.fixture
def three_files(tmp_path):
    (tmp_path / "a.csv").write_text("29.0,45.0,0\n29.1,46.0,0\n")
    (tmp_path / "b.csv").write_text("31.0,95.0,1\n")
    (tmp_path / "c.csv").write_text("30.0,80.0,1\n")
    return [tmp_path / "a.csv", tmp_path / "b.csv", tmp_path / "c.csv"]


def test_split_dung_file(three_files):
    train, test = tt.split_by_file(three_files, "b.csv")
    assert len(train) == 3 and len(test) == 1
    assert test.iloc[0]["humidity"] == 95.0


def test_split_khong_mat_dong_dau(three_files):
    # file không có tiêu đề: dòng đầu là dữ liệu, phải giữ
    train, _ = tt.split_by_file(three_files, "b.csv")
    assert 45.0 in train["humidity"].values


def test_split_index_lien_tuc(three_files):
    train, _ = tt.split_by_file(three_files, "b.csv")
    assert list(train.index) == [0, 1, 2]  # ignore_index=True


def test_split_nhan_path_dang_str(three_files):
    train, test = tt.split_by_file([str(p) for p in three_files], "c.csv")
    assert len(train) == 3 and len(test) == 1


def test_split_khong_co_file_test(three_files):
    with pytest.raises(ValueError):
        tt.split_by_file(three_files, "x.csv")


def test_split_khong_con_file_train(three_files):
    with pytest.raises(ValueError):
        tt.split_by_file(three_files[1:2], "b.csv")


# ---------- TODO 2: compute_norm ----------
def test_norm_vi_du():
    n = tt.compute_norm([[28, 40], [30, 60]])
    assert n == pytest.approx(NORM)


def test_norm_kieu_float_python():
    n = tt.compute_norm([[28, 40], [30, 60]])
    assert set(n) == set(NORM)
    assert all(type(v) is float for v in n.values())  # float của Python: norm.json gọn, so sánh dễ


def test_norm_cot_hang_so():
    n = tt.compute_norm([[29.0, 40], [29.0, 60]])
    assert n["temp_mean"] == pytest.approx(29.0)
    assert n["temp_std"] == 1.0


# ---------- TODO 3: normalize ----------
def test_normalize_vi_du():
    out = tt.normalize([[30, 70]], NORM)
    assert out == pytest.approx(np.array([[1.0, 2.0]]))


def test_normalize_float32_shape():
    out = tt.normalize([[29, 50], [28, 30], [31, 100]], NORM)
    assert out.dtype == np.float32
    assert out.shape == (3, 2)
    assert out[1] == pytest.approx([-1.0, -2.0])
    assert out[2] == pytest.approx([2.0, 5.0])


def test_normalize_roi_compute_norm_ra_mean0_std1():
    rng = np.random.default_rng(0)
    X = np.column_stack([rng.normal(29, 0.5, 200), rng.normal(60, 20, 200)])
    Z = tt.normalize(X, tt.compute_norm(X))
    assert Z.mean(axis=0) == pytest.approx([0, 0], abs=1e-5)
    assert Z.std(axis=0) == pytest.approx([1, 1], abs=1e-4)


# ---------- TODO 4: class_weights ----------
def test_weights_vi_du():
    w = tt.class_weights([0, 1, 1, 1])
    assert w[0] == pytest.approx(2.0)
    assert w[1] == pytest.approx(4 / 6)


def test_weights_du_lieu_1911():
    y = [0] * 233 + [1] * 750
    w = tt.class_weights(y)
    assert w[0] == pytest.approx(983 / 466)
    assert w[1] == pytest.approx(983 / 1500)


def test_weights_can_bang():
    assert tt.class_weights([0, 1]) == pytest.approx({0: 1.0, 1: 1.0})


def test_weights_thieu_nhan():
    with pytest.raises(ValueError):
        tt.class_weights([1, 1, 1])


# ---------- TODO 5: to_c_header ----------
FAKE = bytes(range(30))  # 30 byte giả: 2 dòng đủ 12 + 1 dòng 6


def _array_bytes(h):
    body = h[h.index("{") + 1: h.index("};")]
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body))


def test_header_dau_file():
    h = tt.to_c_header(FAKE, NORM, 0.5)
    assert h.startswith("#pragma once\n")
    assert "#include <stdint.h>" in h


def test_header_hang_so():
    h = tt.to_c_header(FAKE, NORM, 0.5)
    for line in ["#define TINYML_TEMP_MEAN 29.000000f",
                 "#define TINYML_TEMP_STD 1.000000f",
                 "#define TINYML_HUM_MEAN 50.000000f",
                 "#define TINYML_HUM_STD 10.000000f",
                 "#define TINYML_THRESHOLD 0.500000f"]:
        assert line in h.splitlines(), line


def test_header_hang_so_le():
    n = {"temp_mean": 31.043871, "temp_std": 1.414366, "hum_mean": 84.187419, "hum_std": 18.239861}
    h = tt.to_c_header(FAKE, n, 0.5)
    assert "#define TINYML_HUM_MEAN 84.187419f" in h.splitlines()


def test_header_khai_bao_mang():
    h = tt.to_c_header(FAKE, NORM, 0.5)
    assert "alignas(16) const unsigned char dht_anomaly_model_tflite[] = {" in h.splitlines()


def test_header_doi_ten_mang():
    h = tt.to_c_header(FAKE, NORM, 0.5, array_name="m2")
    assert "alignas(16) const unsigned char m2[] = {" in h
    assert "const unsigned int m2_len = 30;" in h


def test_header_byte_dung_het():
    h = tt.to_c_header(FAKE, NORM, 0.5)
    assert _array_bytes(h) == FAKE


def test_header_12_byte_moi_dong():
    h = tt.to_c_header(FAKE, NORM, 0.5)
    rows = [l for l in h.splitlines() if l.strip().startswith("0x")]
    assert [len(re.findall(r"0x[0-9a-f]{2}", r)) for r in rows] == [12, 12, 6]
    assert "0x0a" in h and "0x0A" not in h  # chữ thường, 2 chữ số


def test_header_do_dai():
    h = tt.to_c_header(FAKE, NORM, 0.5)
    assert "const unsigned int dht_anomaly_model_tflite_len = 30;" in h.splitlines()


def test_header_thu_tu():
    h = tt.to_c_header(FAKE, NORM, 0.5)
    assert h.index("#pragma once") < h.index("#define TINYML_TEMP_MEAN") \
        < h.index("alignas(16)") < h.index("};") < h.index("_len =")


def test_header_dong_mang_dung():
    h = tt.to_c_header(FAKE, NORM, 0.5)
    assert "};" in h.splitlines()
    assert ",\n};" not in h  # không có dấu phẩy thừa sau byte cuối
