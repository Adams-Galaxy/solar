from __future__ import annotations

import os
import re
from pathlib import Path


DOCS_DIR = Path(__file__).resolve().parent
ROOT_DIR = DOCS_DIR.parent


def solar_version() -> str:
    cmake = (ROOT_DIR / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(solar VERSION ([0-9]+\.[0-9]+\.[0-9]+)", cmake)
    if match is None:
        raise RuntimeError("unable to derive Solar version from CMakeLists.txt")
    return match.group(1)


project = "Solar"
author = "Solar contributors"
copyright = "2026, Solar contributors"
version = solar_version()
release = version

extensions = [
    "breathe",
    "myst_parser",
    "sphinx.ext.graphviz",
    "sphinx.ext.intersphinx",
    "sphinx_copybutton",
    "sphinx_design",
]

source_suffix = {".md": "markdown"}
master_doc = "index"
exclude_patterns = [
    "_build",
    "development-docs/**",
]

myst_enable_extensions = [
    "colon_fence",
    "deflist",
    "fieldlist",
    "substitution",
]
myst_heading_anchors = 3

breathe_projects = {
    "Solar": os.environ.get("SOLAR_DOXYGEN_XML", str(DOCS_DIR / "_build/doxygen/xml"))
}
breathe_default_project = "Solar"
breathe_default_members = ("members", "undoc-members")

intersphinx_mapping = {
    "python": ("https://docs.python.org/3", None),
    "zephyr": ("https://docs.zephyrproject.org/latest", None),
}
intersphinx_disabled_reftypes = ["std:doc"]

nitpicky = True
nitpick_ignore = [
    # Breathe emits this namespace qualifier while rendering solar::version,
    # but Doxygen does not create a standalone namespace target for it.
    ("cpp:identifier", "solar"),
]

html_theme = "furo"
html_title = f"Solar {release}"
html_static_path = ["_static"]
html_css_files = ["solar.css"]
html_theme_options = {
    "navigation_with_keys": True,
    "top_of_page_button": "edit",
    "source_repository": "https://github.com/Adams-Galaxy/solar/",
    "source_branch": "main",
    "source_directory": "docs/",
}

copybutton_prompt_text = r"^\$ |^> "
copybutton_prompt_is_regexp = True

graphviz_output_format = "svg"
suppress_warnings = []
