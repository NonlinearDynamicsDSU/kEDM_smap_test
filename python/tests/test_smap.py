import kedm
import numpy as np
import pytest


@pytest.mark.parametrize("i", range(15))
def test_smap(pytestconfig, i):
    E, tau, Tp = 2, 1, 1
    theta = [0.0, 0.01, 0.1, 0.3, 0.5, 0.75, 1, 2, 3, 4, 5, 6, 7, 8, 9][i]

    ts = np.loadtxt(pytestconfig.rootdir / "test/logistic_map.csv", skiprows=1)
    rho_valid = np.loadtxt(pytestconfig.rootdir / "test/logistic_map_validation.csv",
                           delimiter=",", skiprows=1, usecols=1)

    library = ts[:100]
    target = ts[100:200]

    prediction = kedm.smap(library, target, E, tau, Tp, theta)

    rho = np.corrcoef(prediction[:-1], target[(E-1)*tau+Tp:])[0][1]

    assert rho == pytest.approx(rho_valid[i], abs=1e-2)
