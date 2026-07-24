"""Minimal process launcher for a Solar Station host."""

from __future__ import annotations

import argparse
import asyncio
import logging
import sys
from collections.abc import Sequence
from pathlib import Path

from .config import StationConfig, default_database_path, default_socket_path
from .errors import StationError
from .server import StationHost

LOGGER = logging.getLogger("solar_station")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        prog="solar-stationd",
        description="Run a persistent Solar Remote connection and local Station API",
    )
    result.add_argument("--socket", type=Path, default=default_socket_path())
    result.add_argument("--database", type=Path, default=default_database_path())
    result.add_argument("--remote", default="auto", help="Remote target URL or auto")
    result.add_argument(
        "--console", default="auto", help="Console target URL, auto, or none"
    )
    result.add_argument("--sim", action="store_true", help="Use local simulator ports")
    result.add_argument("--vid", type=lambda value: int(value, 0))
    result.add_argument("--pid", type=lambda value: int(value, 0))
    result.add_argument(
        "--log-level",
        choices=("debug", "info", "warning", "error"),
        default="info",
    )
    return result


def configure_logging(level: str) -> None:
    logging.basicConfig(
        level=getattr(logging, level.upper()),
        format="%(asctime)s %(levelname)-7s %(name)s: %(message)s",
        datefmt="%H:%M:%S",
    )


async def async_main(argv: Sequence[str] | None = None) -> int:
    options = parser().parse_args(sys.argv[1:] if argv is None else argv)
    configure_logging(options.log_level)
    remote = "tcp://127.0.0.1:47000" if options.sim else options.remote
    console = "tcp://127.0.0.1:47001" if options.sim else options.console
    if console.lower() in ("none", "off", "disabled"):
        console = None
    config = StationConfig(
        socket_path=options.socket.expanduser(),
        database_path=options.database.expanduser(),
        remote_target=remote,
        console_target=console,
        usb_vid=options.vid,
        usb_pid=options.pid,
    )
    LOGGER.info("Launching host (remote=%s, console=%s)", remote, console or "disabled")
    await StationHost(config).run()
    return 0


def main() -> None:
    try:
        raise SystemExit(asyncio.run(async_main()))
    except StationError as error:
        print(f"solar-stationd: {error}", file=sys.stderr)
        raise SystemExit(1) from error
    except KeyboardInterrupt:
        raise SystemExit(130) from None
