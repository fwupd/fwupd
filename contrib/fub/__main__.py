#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later

import sys
from pathlib import Path

# inject the contrib/ directory into the syspath
_contrib_dir = str(Path(__file__).absolute().parent.parent)
if _contrib_dir not in sys.path:
    sys.path.insert(0, _contrib_dir)

from fub.cli import main  # noqa:E402

sys.exit(main())
