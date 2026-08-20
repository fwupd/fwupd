# SPDX-License-Identifier: LGPL-2.1-or-later

from pathlib import Path
from unittest.mock import MagicMock, call, patch

import pytest
from fub.git import GitRepo


class TestShas:
    @pytest.mark.parametrize(
        "abbrev_stdout, expected",
        [
            ("main\n", "main"),
            ("feature/foo\n", "feature/foo"),
            ("v1.2.3\n", "v1.2.3"),
        ],
        ids=[
            "on-branch-main",
            "on-feature-branch",
            "on-tag-branch",
        ],
    )
    def test_on_branch(self, abbrev_stdout, expected):
        """When rev-parse --abbrev-ref returns a branch name, use it directly."""
        repo = GitRepo(root=Path("/repo"))

        mock_cmd = MagicMock()
        mock_cmd.stdout = abbrev_stdout
        with patch("fub.git.RunCmd", return_value=mock_cmd) as mock_runcmd:
            assert repo.current_sha == expected
            mock_runcmd.assert_called_once_with(
                ["git", "rev-parse", "--abbrev-ref", "HEAD"],
                cwd=Path("/repo"),
                check=True,
            )

    def test_detached_head(self):
        """When --abbrev-ref returns 'HEAD', fall back to full rev-parse."""
        repo = GitRepo(root=Path("/repo"))

        calls = []

        def fake_runcmd(args, **kwargs):
            cmd = MagicMock()
            if "--abbrev-ref" in args:
                cmd.stdout = "HEAD\n"
            else:
                cmd.stdout = "abc123def456\n"
            calls.append(args)
            return cmd

        with patch("fub.git.RunCmd", side_effect=fake_runcmd):
            assert repo.current_sha == "abc123def456"

        assert calls == [
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            ["git", "rev-parse", "HEAD"],
        ]

    @pytest.mark.parametrize(
        "ref, sha",
        [
            ("HEAD", "abc123\n"),
            ("main", "def456\n"),
            ("v1.2.3", "789aaa\n"),
            ("HEAD~3", "bbb111\n"),
        ],
        ids=[
            "HEAD",
            "branch-name",
            "tag",
            "relative-ref",
        ],
    )
    def test_resolves_ref(self, ref, sha):
        repo = GitRepo(root=Path("/repo"))

        mock_cmd = MagicMock()
        mock_cmd.stdout = sha
        with patch("fub.git.RunCmd", return_value=mock_cmd) as mock_runcmd:
            result = repo.as_sha(ref)
            assert result == sha.strip()
            mock_runcmd.assert_called_once_with(
                ["git", "rev-parse", ref], cwd=Path("/repo"), check=True
            )


class TestRepo:
    def test_calls_find_repo_root(self):
        with patch(
            "fub.git.directories.repository_root", return_value=Path("/found/root")
        ):
            repo = GitRepo.default()
            assert repo.root == Path("/found/root")

    @pytest.mark.parametrize(
        "depth, expected_extra_args",
        [
            (0, []),
            (1, ["--depth=1"]),
            (10, ["--depth=10"]),
        ],
        ids=[
            "no-depth",
            "depth-1",
            "depth-10",
        ],
    )
    def test_clone_args(self, tmp_path, depth, expected_extra_args):
        src = tmp_path / "myrepo"
        src.mkdir()
        destdir = tmp_path / "dest"

        repo = GitRepo(root=src)
        with patch("fub.git.RunCmd") as mock_runcmd:
            result = repo.clone_into(destdir, depth=depth)

            expected_cmd = ["git", "clone", str(src)] + expected_extra_args
            mock_runcmd.assert_called_once_with(expected_cmd, cwd=destdir, check=True)
            assert result.root == destdir / "myrepo"

    def test_creates_destdir(self, tmp_path):
        """clone_into should create the destination directory if it doesn't exist."""
        src = tmp_path / "myrepo"
        src.mkdir()
        destdir = tmp_path / "nested" / "dest"

        repo = GitRepo(root=src)
        with patch("fub.git.RunCmd"):
            repo.clone_into(destdir)

        assert destdir.exists()

    def test_destdir_already_exists(self, tmp_path):
        """clone_into should not fail if destdir already exists."""
        src = tmp_path / "myrepo"
        src.mkdir()
        destdir = tmp_path / "dest"
        destdir.mkdir()

        repo = GitRepo(root=src)
        with patch("fub.git.RunCmd"):
            result = repo.clone_into(destdir)
            assert result.root == destdir / "myrepo"

    def test_returned_repo_name_matches_source(self, tmp_path):
        """The cloned repo directory name comes from the source repo root name."""
        src = tmp_path / "fwupd"
        src.mkdir()
        destdir = tmp_path / "clones"

        repo = GitRepo(root=src)
        with patch("fub.git.RunCmd"):
            result = repo.clone_into(destdir)
            assert result.root.name == "fwupd"

    def test_checkout_and_restore(self):
        """checkout() should switch to the given sha, then restore the original."""
        repo = GitRepo(root=Path("/repo"))

        runcmd_calls = []
        call_count = 0

        def fake_runcmd(args, **kwargs):
            nonlocal call_count
            cmd = MagicMock()
            if args[1] == "rev-parse":
                # First call to current_sha (before checkout): return "original"
                # After checkout: return target sha
                # The context manager calls current_sha once before, and
                # assert checks current_sha == sha inside the block
                if "--abbrev-ref" in args:
                    if call_count < 1:
                        cmd.stdout = "original-branch\n"
                    else:
                        cmd.stdout = "target-sha\n"
                    call_count += 1
            runcmd_calls.append(args)
            return cmd

        with patch("fub.git.RunCmd", side_effect=fake_runcmd):
            with repo.checkout("target-sha"):
                pass

        # Verify the checkout sequence: get current sha, checkout target, checkout back
        checkout_cmds = [c for c in runcmd_calls if c[1] == "checkout"]
        assert checkout_cmds == [
            ["git", "checkout", "target-sha"],
            ["git", "checkout", "original-branch"],
        ]

    def test_checkout_restores_on_exception(self):
        """checkout() should restore the original branch even if the body raises."""
        repo = GitRepo(root=Path("/repo"))

        runcmd_calls = []
        call_count = 0

        def fake_runcmd(args, **kwargs):
            nonlocal call_count
            cmd = MagicMock()
            if args[1] == "rev-parse":
                if "--abbrev-ref" in args:
                    if call_count < 1:
                        cmd.stdout = "original\n"
                    else:
                        cmd.stdout = "target\n"
                    call_count += 1
            runcmd_calls.append(args)
            return cmd

        with patch("fub.git.RunCmd", side_effect=fake_runcmd):
            with pytest.raises(RuntimeError, match="boom"):
                with repo.checkout("target"):
                    raise RuntimeError("boom")

        # The restore checkout must still have been called
        checkout_cmds = [c for c in runcmd_calls if c[1] == "checkout"]
        assert ["git", "checkout", "original"] in checkout_cmds

    @pytest.mark.parametrize(
        "target_sha",
        [
            "abc123",
            "main",
            "v1.0.0",
            "HEAD~1",
        ],
        ids=[
            "commit-hash",
            "branch-name",
            "tag",
            "relative-ref",
        ],
    )
    def test_checkout_target_variations(self, target_sha):
        """checkout() passes the target ref through to git checkout."""
        repo = GitRepo(root=Path("/repo"))
        call_count = 0

        def fake_runcmd(args, **kwargs):
            nonlocal call_count
            cmd = MagicMock()
            if args[1] == "rev-parse" and "--abbrev-ref" in args:
                if call_count < 1:
                    cmd.stdout = "main\n"
                else:
                    cmd.stdout = f"{target_sha}\n"
                call_count += 1
            return cmd

        with patch("fub.git.RunCmd", side_effect=fake_runcmd) as mock_runcmd:
            with repo.checkout(target_sha):
                pass

        # Find the first checkout call
        checkout_calls = [
            c for c in mock_runcmd.call_args_list if c[0][0][1] == "checkout"
        ]
        assert checkout_calls[0] == call(
            ["git", "checkout", target_sha], cwd=Path("/repo"), check=True
        )
