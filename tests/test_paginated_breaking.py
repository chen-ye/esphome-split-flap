import os
import subprocess
import tempfile
import pytest


def test_cpp_smart_breaking():
    """Compile and execute C++ smart breaking unit tests."""
    cpp_file = os.path.join(
        os.path.dirname(__file__), "test_smart_breaking.cpp"
    )
    with tempfile.TemporaryDirectory() as tmpdir:
        binary = os.path.join(tmpdir, "test_smart_breaking")
        compile_cmd = ["clang++", "-std=c++20", cpp_file, "-o", binary]
        comp_res = subprocess.run(
            compile_cmd, capture_output=True, text=True, check=False
        )
        assert (
            comp_res.returncode == 0
        ), f"C++ compilation failed:\n{comp_res.stderr}"

        run_res = subprocess.run(
            [binary], capture_output=True, text=True, check=False
        )
        assert (
            run_res.returncode == 0
        ), f"C++ test execution failed:\n{run_res.stdout}\n{run_res.stderr}"
