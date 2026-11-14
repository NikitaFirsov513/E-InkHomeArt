"""Optional smoke check for either firmware mode; requires an ESP32-S3."""
from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_idf.utils import idf_parametrize


@idf_parametrize('target', ['esp32s3'], indirect=['target'])
def test_startup(dut: IdfDut) -> None:
    if dut.app.sdkconfig.get('APP_DISPLAY_TOUCH_TEST', False):
        dut.expect('DISPLAY TOUCH TEST READY:', timeout=30)
        dut.expect('LIVE uptime=', timeout=15)
    elif dut.app.sdkconfig.get('APP_LED_TEST', False):
        # UART message confirms app_main ran; inspect the physical LED separately.
        dut.expect('LED TEST READY: D6 BLINK, 500 ms on / 500 ms off', timeout=30)
    else:
        # Missing panel/touch/sensor must not prevent the console loop from running.
        dut.expect('DIAGNOSTICS READY:', timeout=45)
        dut.expect('LIVE uptime=', timeout=15)
