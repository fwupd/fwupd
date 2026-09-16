#!/usr/bin/env python3
#
# Copyright 2026 Advanced Micro Devices, Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Tests for fwupdtui."""

import asyncio

import pytest

from textual.widgets import Button, Tree

from fwupdtui import BiosSetting, Fwupd, FwupdTui


def make_setting(**kwargs) -> BiosSetting:
    values = {
        "key": "com.example.setting",
        "name": "Network Boot",
        "parent": "Boot/Network",
        "description": "Choose the network boot protocol",
        "current_value": "HTTP",
        "kind": Fwupd.BiosSettingKind.ENUMERATION,
        "read_only": False,
        "possible_values": ["HTTP", "PXE"],
        "lower_bound": 0,
        "upper_bound": 0,
        "scalar_increment": 1,
    }
    values.update(kwargs)
    return BiosSetting(**values)


class TestBiosSetting:
    def test_from_fwupd(self) -> None:
        raw = Fwupd.BiosSetting.new("NetworkBoot", None)
        raw.set_id("com.example.network-boot")
        raw.set_parent("Boot/Network")
        raw.set_description("Choose the network boot protocol")
        raw.set_kind(Fwupd.BiosSettingKind.ENUMERATION)
        raw.set_current_value("HTTP")
        raw.add_possible_value("HTTP")
        raw.add_possible_value("PXE")

        setting = BiosSetting.from_fwupd(raw)
        assert setting.key == "com.example.network-boot"
        assert setting.parent == "Boot/Network"
        assert setting.possible_values == ["HTTP", "PXE"]

    def test_from_fwupd_integer(self) -> None:
        raw = Fwupd.BiosSetting.new("ThreadCount", None)
        raw.set_id("com.example.thread-count")
        raw.set_kind(Fwupd.BiosSettingKind.INTEGER)
        raw.set_lower_bound(1)
        raw.set_upper_bound(128)
        raw.set_scalar_increment(1)
        raw.set_current_value("16")

        setting = BiosSetting.from_fwupd(raw)
        assert setting.possible_values == []
        assert setting.lower_bound == 1
        assert setting.upper_bound == 128

    def test_enumeration(self) -> None:
        setting = make_setting()
        assert setting.validate("PXE") == "PXE"
        with pytest.raises(ValueError, match="available values"):
            setting.validate("invalid")

    def test_integer(self) -> None:
        setting = make_setting(
            kind=Fwupd.BiosSettingKind.INTEGER,
            current_value="0x10",
            possible_values=[],
            lower_bound=0,
            upper_bound=32,
            scalar_increment=4,
        )
        assert setting.validate("0x10") == "16"
        with pytest.raises(ValueError, match="increment"):
            setting.validate("17")
        with pytest.raises(ValueError, match="between"):
            setting.validate("36")

    def test_string(self) -> None:
        setting = make_setting(
            kind=Fwupd.BiosSettingKind.STRING,
            current_value="fwupd",
            possible_values=[],
            lower_bound=3,
            upper_bound=8,
        )
        assert setting.validate("firmware") == "firmware"
        with pytest.raises(ValueError, match="length"):
            setting.validate("no")


class TestFwupdTui:
    def test_grouping_and_pending_change(self) -> None:
        setting = make_setting()
        app = FwupdTui([setting])

        async def run_test() -> None:
            async with app.run_test(size=(100, 30)) as pilot:
                await pilot.pause()
                tree = app.query_one("#settings-tree", Tree)
                assert tree.root.children[0].label.plain == "Boot"
                assert tree.root.children[0].children[0].label.plain == "Network"
                app.current_key = setting.key
                app.edit_complete("PXE")
                assert app.pending == {setting.key: "PXE"}
                assert not app.query_one("#apply", Button).disabled

        asyncio.run(run_test())
