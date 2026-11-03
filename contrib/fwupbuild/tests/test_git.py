# SPDX-License-Identifier: LGPL-2.1-or-later

from pathlib import Path
from unittest.mock import MagicMock, PropertyMock, call, patch

import pytest
from fwupbuild.git import GitRepo, GitSha
from fwupbuild.logger import printer


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


class TestClone:
    """Tests for GitRepo.clone() class method."""

    def test_basic_no_depth_no_cwd(self, tmp_path):
        """clone() without depth or cwd calls git clone --quiet <url>."""
        url = "https://example.com/myrepo.git"
        (tmp_path / "myrepo").mkdir()

        with (
            patch("fwupbuild.git.RunCmd") as mock_runcmd,
            patch("pathlib.Path.cwd", return_value=tmp_path),
        ):
            result = GitRepo.clone(url)
            mock_runcmd.assert_called_once_with(
                ["git", "clone", "--quiet", url], check=True
            )
        assert result.root == tmp_path / "myrepo"

    def test_depth_zero_omits_depth_arg(self, tmp_path):
        """clone() with depth=0 (the default) does not pass --depth to git."""
        url = "https://example.com/myrepo.git"
        (tmp_path / "myrepo").mkdir()

        with (
            patch("fwupbuild.git.RunCmd") as mock_runcmd,
            patch("pathlib.Path.cwd", return_value=tmp_path),
        ):
            GitRepo.clone(url, depth=0)
            mock_runcmd.assert_called_once_with(
                ["git", "clone", "--quiet", url], check=True
            )

    @pytest.mark.parametrize(
        "depth",
        [1, 10],
        ids=["depth-1", "depth-10"],
    )
    def test_depth_positive_adds_depth_arg(self, tmp_path, depth):
        """clone() with depth > 0 appends --depth <n> to the git command."""
        url = "https://example.com/myrepo.git"
        (tmp_path / "myrepo").mkdir()

        with (
            patch("fwupbuild.git.RunCmd") as mock_runcmd,
            patch("pathlib.Path.cwd", return_value=tmp_path),
        ):
            GitRepo.clone(url, depth=depth)
            mock_runcmd.assert_called_once_with(
                ["git", "clone", "--quiet", "--depth", str(depth), url], check=True
            )

    def test_cwd_passed_as_str_kwarg(self, tmp_path):
        """clone() with cwd passes cwd=str(cwd) to RunCmd."""
        url = "https://example.com/myrepo.git"
        cwd = tmp_path / "dest"
        cwd.mkdir()
        (cwd / "myrepo").mkdir()

        with patch("fwupbuild.git.RunCmd") as mock_runcmd:
            result = GitRepo.clone(url, cwd=cwd)
            mock_runcmd.assert_called_once_with(
                ["git", "clone", "--quiet", url], check=True, cwd=str(cwd)
            )
        assert result.root == cwd / "myrepo"

    def test_cwd_and_depth_combined(self, tmp_path):
        """clone() passes both cwd and --depth when both are given."""
        url = "https://example.com/myrepo.git"
        cwd = tmp_path / "dest"
        cwd.mkdir()
        (cwd / "myrepo").mkdir()

        with patch("fwupbuild.git.RunCmd") as mock_runcmd:
            GitRepo.clone(url, depth=5, cwd=cwd)
            mock_runcmd.assert_called_once_with(
                ["git", "clone", "--quiet", "--depth", "5", url],
                check=True,
                cwd=str(cwd),
            )

    def test_strips_git_suffix_from_url(self, tmp_path):
        """clone() strips a trailing .git to derive the local directory name."""
        url = "https://example.com/fwupd.git"
        (tmp_path / "fwupd").mkdir()

        with patch("fwupbuild.git.RunCmd"):
            result = GitRepo.clone(url, cwd=tmp_path)
        assert result.root == tmp_path / "fwupd"

    def test_url_without_git_suffix(self, tmp_path):
        """clone() uses the bare URL path component when the URL has no .git suffix."""
        url = "https://example.com/fwupd"
        (tmp_path / "fwupd").mkdir()

        with patch("fwupbuild.git.RunCmd"):
            result = GitRepo.clone(url, cwd=tmp_path)
        assert result.root == tmp_path / "fwupd"

    def test_raises_if_repo_dir_absent_after_clone(self, tmp_path):
        """clone() raises FileNotFoundError when the expected repo directory is missing."""
        url = "https://example.com/myrepo.git"

        with patch("fwupbuild.git.RunCmd"):
            with pytest.raises(FileNotFoundError):
                GitRepo.clone(url, cwd=tmp_path)

    def test_returns_gitrepo_with_correct_root(self, tmp_path):
        """clone() returns a GitRepo whose root points at the cloned directory."""
        url = "https://example.com/myrepo.git"
        (tmp_path / "myrepo").mkdir()

        with patch("fwupbuild.git.RunCmd"):
            result = GitRepo.clone(url, cwd=tmp_path)
        assert isinstance(result, GitRepo)
        assert result.root == tmp_path / "myrepo"


class TestFindFiles:
    """
    Tests for GitRepo.find_files().

    The function is supposed to return files that:
      - have one of the given suffixes
      - are NOT in any of the exclude_directories
      - ARE in one of the include_directories (when specified)
    """

    @pytest.fixture
    def repo(self, tmp_path):
        root = tmp_path / "repo"
        root.mkdir()
        return GitRepo(root=root)

    def test_suffix_filter_returns_matching(self, repo):
        """Only files whose suffix is in the list are returned."""
        files = [Path("src/foo.c"), Path("src/bar.h"), Path("README.md")]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(suffixes=[".c"])
        assert result == [Path("src/foo.c")]

    def test_no_suffix_argument_returns_all_files(self, repo):
        """Omitting suffixes entirely returns all tracked files."""
        files = [Path("src/foo.c"), Path("src/bar.h"), Path("README.md")]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files()
        assert sorted(result) == [
            Path("README.md"),
            Path("src/bar.h"),
            Path("src/foo.c"),
        ]

    def test_multiple_suffixes_all_match(self, repo):
        """Files matching any one of several suffixes are all returned."""
        files = [Path("src/foo.c"), Path("src/bar.h"), Path("tests/test.py")]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(suffixes=[".c", ".h"])
        assert sorted(result) == [Path("src/bar.h"), Path("src/foo.c")]

    def test_suffix_no_match_returns_empty(self, repo):
        """Returns empty list when no files match the suffix."""
        files = [Path("src/foo.c"), Path("src/bar.h")]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(suffixes=[".py"])
        assert result == []

    def test_empty_repo_returns_empty(self, repo):
        """Returns empty list when there are no tracked files."""
        with patch.object(GitRepo, "files", new_callable=PropertyMock, return_value=[]):
            result = repo.find_files(suffixes=[".c"])
        assert result == []

    def test_exclude_directories_none_does_not_raise(self, repo):
        """exclude_directories=None must not raise TypeError."""
        files = [Path("src/foo.c")]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(suffixes=[".c"], exclude_directories=None)
        assert result == [Path("src/foo.c")]

    def test_include_directories_none_returns_all_suffix_matches(self, repo):
        """include_directories=None imposes no directory restriction."""
        files = [Path("src/foo.c"), Path("lib/bar.c")]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(suffixes=[".c"], include_directories=None)
        assert sorted(result) == [Path("lib/bar.c"), Path("src/foo.c")]

    def test_both_dir_args_none_returns_all_suffix_matches(self, repo):
        """With both directory args None, all suffix-matching files are returned."""
        files = [Path("a/foo.c"), Path("b/bar.c"), Path("c/baz.h")]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(suffixes=[".c"])
        assert sorted(result) == [Path("a/foo.c"), Path("b/bar.c")]

    def test_exclude_directory_removes_files_inside_it(self, repo):
        """Files inside an excluded directory are omitted from results."""
        root = repo.root
        # Use absolute paths so f.resolve() gives the right answer
        files = [root / "src/foo.c", root / "vendor/ext.c"]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(
                suffixes=[".c"], exclude_directories=[root / "vendor"]
            )
        assert result == [root / "src/foo.c"]

    def test_exclude_relative_directory_resolved_against_repo_root(self, repo):
        """A relative exclude path is resolved against the repo root."""
        root = repo.root
        files = [root / "src/foo.c", root / "vendor/ext.c"]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(
                suffixes=[".c"], exclude_directories=[Path("vendor")]
            )
        assert result == [root / "src/foo.c"]

    def test_exclude_nested_subdirectory(self, repo):
        """Files nested under an excluded directory are also excluded."""
        root = repo.root
        files = [root / "src/foo.c", root / "src/generated/auto.c"]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(
                suffixes=[".c"], exclude_directories=[root / "src/generated"]
            )
        assert result == [root / "src/foo.c"]

    def test_exclude_does_not_affect_files_outside_it(self, repo):
        """Files outside the excluded directory are unaffected."""
        root = repo.root
        files = [root / "src/foo.c", root / "src/bar.c"]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(
                suffixes=[".c"], exclude_directories=[root / "vendor"]
            )
        assert sorted(result) == [root / "src/bar.c", root / "src/foo.c"]

    def test_multiple_exclude_directories(self, repo):
        """Files inside any one of several excluded directories are omitted."""
        root = repo.root
        files = [root / "src/foo.c", root / "vendor/a.c", root / "third_party/b.c"]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(
                suffixes=[".c"],
                exclude_directories=[root / "vendor", root / "third_party"],
            )
        assert result == [root / "src/foo.c"]

    def test_include_directory_limits_results(self, repo):
        """Only files inside an included directory are returned."""
        root = repo.root
        files = [root / "src/foo.c", root / "lib/bar.c", root / "tests/test.c"]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(
                suffixes=[".c"], include_directories=[root / "src"]
            )
        assert result == [root / "src/foo.c"]

    def test_include_relative_directory_resolved_against_repo_root(self, repo):
        """A relative include path is resolved against the repo root."""
        root = repo.root
        files = [root / "src/foo.c", root / "lib/bar.c"]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(suffixes=[".c"], include_directories=[Path("src")])
        assert result == [root / "src/foo.c"]

    def test_multiple_include_directories(self, repo):
        """Files in any of several include directories are all returned."""
        root = repo.root
        files = [root / "src/foo.c", root / "lib/bar.c", root / "tests/test.c"]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(
                suffixes=[".c"],
                include_directories=[root / "src", root / "lib"],
            )
        assert sorted(result) == [root / "lib/bar.c", root / "src/foo.c"]

    def test_file_outside_all_include_directories_is_excluded(self, repo):
        """When include_directories is set, files outside it are not returned."""
        root = repo.root
        files = [root / "src/foo.c", root / "other/bar.c"]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(
                suffixes=[".c"], include_directories=[root / "src"]
            )
        assert result == [root / "src/foo.c"]

    def test_exclude_takes_priority_over_include(self, repo):
        """A file that is in both an include and an exclude directory is excluded."""
        root = repo.root
        files = [root / "src/foo.c", root / "src/vendor/lib.c"]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(
                suffixes=[".c"],
                include_directories=[root / "src"],
                exclude_directories=[root / "src/vendor"],
            )
        assert result == [root / "src/foo.c"]

    def test_suffix_and_include_combined(self, repo):
        """Only files with the right suffix AND inside an include directory are returned."""
        root = repo.root
        files = [root / "src/foo.c", root / "src/bar.h", root / "lib/baz.c"]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(
                suffixes=[".c"], include_directories=[root / "src"]
            )
        assert result == [root / "src/foo.c"]

    def test_suffixes_none_returns_all_files(self, repo):
        """suffixes=None imposes no suffix restriction."""
        files = [Path("src/foo.c"), Path("src/bar.h"), Path("README.md")]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(suffixes=None)
        assert sorted(result) == [
            Path("README.md"),
            Path("src/bar.h"),
            Path("src/foo.c"),
        ]

    def test_as_absolute_paths_converts_relative(self, repo):
        """as_absolute_paths=True returns paths rooted at repo.root, not CWD."""
        root = repo.root
        files = [Path("src/foo.c"), Path("lib/bar.c")]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(suffixes=[".c"], as_absolute_paths=True)
        assert result == [root / "src/foo.c", root / "lib/bar.c"]

    def test_as_absolute_paths_false_keeps_relative(self, repo):
        """as_absolute_paths=False (default) leaves relative paths unchanged."""
        files = [Path("src/foo.c"), Path("lib/bar.c")]
        with patch.object(
            GitRepo, "files", new_callable=PropertyMock, return_value=files
        ):
            result = repo.find_files(suffixes=[".c"])
        assert result == [Path("src/foo.c"), Path("lib/bar.c")]


class TestResolveFiles:
    @pytest.fixture
    def repo(self, tmp_path):
        root = tmp_path / "repo"
        root.mkdir()
        return GitRepo(root=root)

    def test_relative_str_returns_relative_path(self, repo):
        """A relative string input is returned as a relative Path."""
        (repo.root / "plugins" / "foo").mkdir(parents=True)
        (repo.root / "plugins" / "foo" / "bar.c").touch()
        with patch.object(
            GitRepo,
            "files",
            new_callable=PropertyMock,
            return_value=[Path("plugins/foo/bar.c")],
        ):
            assert repo.resolve_files(["plugins/foo/bar.c"]) == [
                Path("plugins/foo/bar.c")
            ]

    def test_absolute_input_inside_root(self, repo):
        """An absolute path inside the root is returned as a relative Path."""
        abs_path = repo.root / "plugins" / "foo" / "bar.c"
        abs_path.parent.mkdir(parents=True)
        abs_path.touch()
        with patch.object(
            GitRepo,
            "files",
            new_callable=PropertyMock,
            return_value=[Path("plugins/foo/bar.c")],
        ):
            assert repo.resolve_files([abs_path]) == [Path("plugins/foo/bar.c")]

    def test_as_absolute_paths(self, repo):
        """as_absolute_paths=True returns absolute paths."""
        (repo.root / "plugins" / "foo").mkdir(parents=True)
        (repo.root / "plugins" / "foo" / "bar.c").touch()
        with patch.object(
            GitRepo,
            "files",
            new_callable=PropertyMock,
            return_value=[Path("plugins/foo/bar.c")],
        ):
            result = repo.resolve_files(["plugins/foo/bar.c"], as_absolute_paths=True)
        assert result == [repo.root / "plugins" / "foo" / "bar.c"]

    def test_multiple_files(self, repo):
        """Multiple files are all resolved and returned in order."""
        (repo.root / "a").mkdir()
        (repo.root / "d").mkdir()
        (repo.root / "a" / "b.c").touch()
        (repo.root / "d" / "e.c").touch()
        with patch.object(
            GitRepo,
            "files",
            new_callable=PropertyMock,
            return_value=[Path("a/b.c"), Path("d/e.c")],
        ):
            assert repo.resolve_files(["a/b.c", "d/e.c"]) == [
                Path("a/b.c"),
                Path("d/e.c"),
            ]

    def test_empty_list(self, repo):
        """An empty input returns an empty list."""
        with patch.object(GitRepo, "files", new_callable=PropertyMock, return_value=[]):
            assert repo.resolve_files([]) == []

    def test_outside_root_raises(self, repo, tmp_path):
        """A path outside the repo root raises FileNotFoundError."""
        outside = tmp_path / "other" / "file.c"
        with patch.object(GitRepo, "files", new_callable=PropertyMock, return_value=[]):
            with pytest.raises(FileNotFoundError, match="is not inside"):
                repo.resolve_files([outside])

    def test_path_traversal_raises(self, repo):
        """A traversal path that escapes the repo root raises FileNotFoundError."""
        with patch.object(GitRepo, "files", new_callable=PropertyMock, return_value=[]):
            with pytest.raises(FileNotFoundError, match="is not inside"):
                repo.resolve_files(["../escape/file.c"])

    def test_untracked_file_emits_warning(self, repo):
        """A file not in git's tracked list causes a warning."""
        (repo.root / "src").mkdir()
        (repo.root / "src" / "new.c").touch()
        with patch.object(GitRepo, "files", new_callable=PropertyMock, return_value=[]):
            with patch.object(printer, "warning") as mock_warning:
                repo.resolve_files(["src/new.c"])
                mock_warning.assert_called_once_with("src/new.c is not tracked by git")

    def test_tracked_file_emits_no_warning(self, repo):
        """A file in git's tracked list causes no warning."""
        (repo.root / "src").mkdir()
        (repo.root / "src" / "foo.c").touch()
        with patch.object(
            GitRepo,
            "files",
            new_callable=PropertyMock,
            return_value=[Path("src/foo.c")],
        ):
            with patch.object(printer, "warning") as mock_warning:
                repo.resolve_files(["src/foo.c"])
                mock_warning.assert_not_called()
