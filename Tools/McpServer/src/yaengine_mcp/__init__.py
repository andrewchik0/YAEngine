"""MCP server for the YAEngine editor agent bridge."""

from importlib.metadata import PackageNotFoundError, version

try:
    __version__ = version("yaengine-mcp")
except PackageNotFoundError:
    __version__ = "0.0.0"
