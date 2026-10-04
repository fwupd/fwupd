# SPDX-License-Identifier: LGPL-2.1-or-later

import argparse
import os
import sys
from unittest.mock import Mock, patch

import pytest

from fub import cmd_init
from fub import cli
from fub.cmd_setup import generate_fub_completions
from fub.directories import directories
from fub.osprofile import OsName


def test_print_completion_is_hidden(capsys):
    shtab = Mock()
    shtab.complete.return_value = "completion"

    with patch.dict(sys.modules, {"shtab": shtab}):
        assert cli.main(["--print-completion", "bash"]) == 0

    assert capsys.readouterr().out == "completion\n"
    with pytest.raises(SystemExit) as help_result:
        cli.main(["--help"])
    assert help_result.value.code == 0
    assert "--print-completion" not in capsys.readouterr().out


def test_generate_fub_completions_uses_venv_python(tmp_path):
    python = tmp_path / "bin" / "python3"
    with patch("fub.cmd_setup.RunCmd") as run_cmd:
        run_cmd.return_value.success = True
        run_cmd.return_value.stdout = "completion\n"

        generate_fub_completions(tmp_path, python)

    assert [call.args[0] for call in run_cmd.call_args_list] == [
        [python, "-m", "fub", "--print-completion", "bash"],
        [python, "-m", "fub", "--print-completion", "fish"],
    ]
    assert (tmp_path / "completion" / "fub.bash").read_text(
        encoding="utf-8"
    ) == "completion\n"
    assert (tmp_path / "completion" / "fub.fish").read_text(
        encoding="utf-8"
    ) == "completion\n"


def test_init_uses_system_dependency_setup(tmp_path, monkeypatch):
    build_root = tmp_path / "build"
    monkeypatch.setattr(directories, "repopulate", lambda _: None)
    monkeypatch.setattr(directories, "build_root", lambda: build_root)

    with patch("fub.cmd_setup.setup_system_deps", return_value=23) as setup_deps:
        result = cmd_init.run(
            argparse.Namespace(
                build_root=build_root,
                directory=None,
                wipe=False,
                osname="arch",
                deps=True,
            )
        )

    assert result == 23
    setup_deps.assert_called_once_with(OsName.ARCH)
