"""Optional hardware smoke check. Build first; requires a connected ESP32-S3."""
from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_idf.utils import idf_parametrize


@idf_parametrize('target', ['esp32s3'], indirect=['target'])
def test_diagnostic_startup(dut: IdfDut) -> None:
    # A missing panel/touch/sensor must not prevent the console loop from running.
    dut.expect('DIAGNOSTICS READY:', timeout=45)
    dut.expect('LIVE uptime=', timeout=15)
