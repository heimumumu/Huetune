import hashlib
import json
import runpy
from pathlib import Path
from types import SimpleNamespace

import pytest


@pytest.fixture
def setup(monkeypatch, tmp_path):
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    return runpy.run_path(str(Path(__file__).resolve().parents[1] / "tools/huetune-setup.py"))["install"].__globals__


def port(pid, device, serial="AA:BB:CC:DD:EE:FF"):
    return SimpleNamespace(pid=pid, device=device, serial_number=serial)


def test_rejects_damaged_payload(setup, tmp_path):
    setup["ROOT"] = tmp_path
    image = tmp_path / "media_controller.bin"
    image.write_bytes(b"actual")
    digest = hashlib.sha256(b"expected").hexdigest()
    (tmp_path / "manifest.json").write_text(json.dumps({"sha256": {image.name: digest}}))
    with pytest.raises(RuntimeError, match=image.name):
        setup["verify_payload"]()


@pytest.mark.parametrize("upgrade", [True, False])
def test_install_writes_expected_regions_and_checks_new_version(setup, upgrade):
    runtime, rom = port(setup["RUNTIME"], "COM11"), port(0x1001, "COM10")
    target = runtime if upgrade else rom
    calls = []

    def flash(current, command, *args, **kwargs):
        assert current is rom
        calls.append((command, args))
        return "Detected flash size: 16MB"

    def probe(current, install_version=None):
        assert current is runtime
        calls.append(("probe", install_version))
        return "0.2.14"

    manifest = {"firmware": "0.2.14", "companion": "0.3.6"}
    setup.update(verify_payload=lambda: manifest, stop_companion=lambda: None, ports=lambda: [target],
                 wait_for=lambda seconds, poll: poll(),
                 same_board=lambda target, pids: runtime if pids == (setup["RUNTIME"],) else rom,
                 esptool=flash, backup_flash=lambda device: calls.append(("backup", device)),
                 runtime_version=probe, install_companion=lambda value: calls.append(("start", value)))
    result = setup["install"](target, lambda _: None)
    assert "0.2.14" in result and "0.3.6" in result
    assert [call[0] for call in calls] == (["probe"] if upgrade else ["flash-id", "backup"]) + ["write-flash", "read-mac", "probe", "start"]
    regions = [arg for arg in next(args for action, args in calls if action == "write-flash")
               if isinstance(arg, str) and arg.startswith("0x")]
    assert regions == (["0x10000"] if upgrade else ["0x0", "0x8000", "0x10000"])


def test_second_board_stops_before_flash(setup):
    target = port(0x1001, "COM10")
    setup.update(verify_payload=lambda: {"firmware": "0.2.14"}, stop_companion=lambda: None,
                 ports=lambda: [target, port(0x1001, "COM3", "001122334455")],
                 esptool=lambda *_, **__: pytest.fail("must not start the flasher"))
    with pytest.raises(RuntimeError, match="设备连接已变化"):
        setup["install"](target, lambda _: None)
