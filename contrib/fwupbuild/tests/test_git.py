# SPDX-License-Identifier: LGPL-2.1-or-later

from pathlib import Path
from unittest.mock import MagicMock, call, patch

import pytest
from fwupbuild.git import GitRepo, GitSha


class TestGitSha:
    def test_eq(self):
        """The same commit in a clone does not equal the one in the original."""
        original = GitSha(sha="abc123", _repo=GitRepo(root=Path("/repo")))
        clone = GitSha(sha="abc123", _repo=GitRepo(root=Path("/clone/repo")))
        assert original != clone
        assert original != GitSha(sha="def456", _repo=GitRepo(root=Path("/repo")))
        assert original == GitSha(sha="abc123", _repo=GitRepo(root=Path("/repo")))

    def test_hashable(self):
        """Equal shas must collapse in a set"""
        repo = GitRepo(root=Path("/repo"))
        other_repo = GitRepo(root=Path("/other/repo"))
        assert (
            len({GitSha(sha="abc123", _repo=repo), GitSha(sha="abc123", _repo=repo)})
            == 1
        )
        assert (
            len(
                {
                    GitSha(sha="abc123", _repo=other_repo),
                    GitSha(sha="abc123", _repo=repo),
                }
            )
            == 2
        )

    def test_not_equal_to_str(self):
        """A GitSha is not its string; use .sha or str() for that."""
        sha = GitSha(sha="abc123", _repo=GitRepo(root=Path("/repo")))
        assert sha != "abc123"
        assert str(sha) == "abc123"


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
        with patch("fwupbuild.git.RunCmd", return_value=mock_cmd) as mock_runcmd:
            assert repo.current_ref == expected
            mock_runcmd.assert_called_once_with(
                ["git", "rev-parse", "--abbrev-ref", "HEAD"],
                cwd=Path("/repo"),
                check=True,
            )

    def test_detached_head(self):
        """When --abbrev-ref returns 'HEAD' there is no named ref."""
        repo = GitRepo(root=Path("/repo"))

        mock_cmd = MagicMock()
        mock_cmd.stdout = "HEAD\n"
        with patch("fwupbuild.git.RunCmd", return_value=mock_cmd) as mock_runcmd:
            assert repo.current_ref is None
            mock_runcmd.assert_called_once_with(
                ["git", "rev-parse", "--abbrev-ref", "HEAD"],
                cwd=Path("/repo"),
                check=True,
            )

    def test_current_sha(self):
        """current_sha always resolves HEAD to a sha, named ref or not."""
        repo = GitRepo(root=Path("/repo"))

        mock_cmd = MagicMock()
        mock_cmd.stdout = "abc123def456\n"
        with patch("fwupbuild.git.RunCmd", return_value=mock_cmd) as mock_runcmd:
            sha = repo.current_sha
            mock_runcmd.assert_called_once_with(
                ["git", "rev-parse", "HEAD"], cwd=Path("/repo"), check=True
            )

        assert sha.sha == "abc123def456"
        assert str(sha) == "abc123def456"

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
        with patch("fwupbuild.git.RunCmd", return_value=mock_cmd) as mock_runcmd:
            result = repo.as_sha(ref)
            assert result.sha == sha.strip()
            mock_runcmd.assert_called_once_with(
                ["git", "rev-parse", ref], cwd=Path("/repo"), check=True
            )


class TestRepo:
    def test_calls_find_repo_root(self):
        with patch(
            "fwupbuild.git.directories.repository_root",
            return_value=Path("/found/root"),
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
        with patch("fwupbuild.git.RunCmd") as mock_runcmd:
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
        with patch("fwupbuild.git.RunCmd"):
            repo.clone_into(destdir)

        assert destdir.exists()

    def test_destdir_already_exists(self, tmp_path):
        """clone_into should not fail if destdir already exists."""
        src = tmp_path / "myrepo"
        src.mkdir()
        destdir = tmp_path / "dest"
        destdir.mkdir()

        repo = GitRepo(root=src)
        with patch("fwupbuild.git.RunCmd"):
            result = repo.clone_into(destdir)
            assert result.root == destdir / "myrepo"

    def test_returned_repo_name_matches_source(self, tmp_path):
        """The cloned repo directory name comes from the source repo root name."""
        src = tmp_path / "fwupd"
        src.mkdir()
        destdir = tmp_path / "clones"

        repo = GitRepo(root=src)
        with patch("fwupbuild.git.RunCmd"):
            result = repo.clone_into(destdir)
            assert result.root.name == "fwupd"

    def test_checkout_and_restore(self):
        """checkout() should switch to the given sha, then restore the original."""
        repo = GitRepo(root=Path("/repo"))

        runcmd_calls = []

        def fake_runcmd(args, **kwargs):
            cmd = MagicMock()
            if args[1] == "rev-parse":
                if "--abbrev-ref" in args:
                    # current_ref: we're on a branch before the checkout
                    cmd.stdout = "original-branch\n"
                else:
                    # as_sha("target-sha") and any current_sha after checkout
                    cmd.stdout = "1234target-sha\n"
            runcmd_calls.append(args)
            return cmd

        with patch("fwupbuild.git.RunCmd", side_effect=fake_runcmd):
            sha = repo.as_sha("target-sha")
            with repo.checkout(sha):
                pass

        # Verify the checkout sequence: get current ref, checkout the resolved
        # target sha, checkout back
        checkout_cmds = [c for c in runcmd_calls if c[1] == "checkout"]
        assert checkout_cmds == [
            ["git", "checkout", "1234target-sha"],
            ["git", "checkout", "original-branch"],
        ]

    def test_checkout_restores_on_exception(self):
        """checkout() should restore the original branch even if the body raises."""
        repo = GitRepo(root=Path("/repo"))

        runcmd_calls = []

        def fake_runcmd(args, **kwargs):
            cmd = MagicMock()
            if args[1] == "rev-parse":
                if "--abbrev-ref" in args:
                    cmd.stdout = "original\n"
                else:
                    cmd.stdout = "target-sha\n"
            runcmd_calls.append(args)
            return cmd

        with patch("fwupbuild.git.RunCmd", side_effect=fake_runcmd):
            sha = repo.as_sha("target")
            with pytest.raises(RuntimeError, match="boom"):
                with repo.checkout(sha):
                    raise RuntimeError("boom")

        # The restore checkout must still have been called
        checkout_cmds = [c for c in runcmd_calls if c[1] == "checkout"]
        assert ["git", "checkout", "original"] in checkout_cmds

    @pytest.mark.parametrize(
        "ref, resolved",
        [
            ("abc123", "abc123def456"),
            ("main", "1111111111aa"),
            ("v1.0.0", "2222222222bb"),
            ("HEAD~1", "3333333333cc"),
        ],
        ids=[
            "commit-hash",
            "branch-name",
            "tag",
            "relative-ref",
        ],
    )
    def test_checkout_target_variations(self, ref, resolved):
        """checkout() passes the resolved sha, not the ref, to git checkout."""
        repo = GitRepo(root=Path("/repo"))

        def fake_runcmd(args, **kwargs):
            cmd = MagicMock()
            if args[1] == "rev-parse":
                if "--abbrev-ref" in args:
                    cmd.stdout = "main\n"
                else:
                    cmd.stdout = f"{resolved}\n"
            return cmd

        with patch("fwupbuild.git.RunCmd", side_effect=fake_runcmd) as mock_runcmd:
            sha = repo.as_sha(ref)
            with repo.checkout(sha):
                pass

        # Find the first checkout call
        checkout_calls = [
            c for c in mock_runcmd.call_args_list if c[0][0][1] == "checkout"
        ]
        assert checkout_calls[0] == call(
            ["git", "checkout", resolved], cwd=Path("/repo"), check=True
        )
