"""
The station's views: every module in this folder that defines a View subclass
with a `name`.  Nothing to register - drop a file in and it is found.
"""

import importlib
import pkgutil

from .base import View

_views = {}


def _load():
    for m in pkgutil.iter_modules(__path__):
        if m.name == "base":
            continue
        mod = importlib.import_module(f"{__name__}.{m.name}")
        for obj in vars(mod).values():
            if isinstance(obj, type) and issubclass(obj, View) and obj is not View and obj.name != "base":
                _views[obj.name] = obj


def all_views():
    if not _views:
        _load()
    return dict(_views)


def for_frame(name):
    """The view a frame name selects, or None."""
    for v in all_views().values():
        if name in v.frames:
            return v
    return None
