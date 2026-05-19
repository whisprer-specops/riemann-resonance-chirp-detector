import numpy as np

from riemann_chirp_overlay.entropy import permutation_entropy, renyi_sharpness, shannon_entropy


def test_entropy_ranges():
    x = np.array([0, 0, 1, 1, 2, 3, 5, 8], dtype=float)
    assert 0.0 <= shannon_entropy(x) <= 1.0
    assert 0.0 <= renyi_sharpness(x) <= 1.0
    assert 0.0 <= permutation_entropy(x, order=3) <= 1.0
