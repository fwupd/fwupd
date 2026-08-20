# SPDX-License-Identifier: LGPL-2.1-or-later

import sys
from pathlib import Path

# Add contrib/ to sys.path so "from fub.xxx import ..." works,
# matching the path setup in fub/__main__.py.
_contrib_dir = str(Path(__file__).resolve().parent.parent.parent)
if _contrib_dir not in sys.path:
    sys.path.insert(0, _contrib_dir)
