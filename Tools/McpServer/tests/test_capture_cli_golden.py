import json

import pytest

from capture_golden import CASES, GOLDEN_FILE, build_capture, run_case
from yaengine_mcp.tools.capture import ANALYSIS_SCRIPT

GOLDEN = json.loads(GOLDEN_FILE.read_text(encoding="utf-8"))["cases"]


@pytest.fixture(scope="module")
def locations(tmp_path_factory):
    return build_capture(tmp_path_factory.mktemp("golden"))


def test_every_case_has_a_recording():
    assert sorted(GOLDEN) == sorted(case["id"] for case in CASES)


@pytest.mark.parametrize("case", CASES, ids=[case["id"] for case in CASES])
def test_cli_prints_what_the_committed_script_printed(locations, case):
    assert run_case(ANALYSIS_SCRIPT, locations, case) == GOLDEN[case["id"]]
