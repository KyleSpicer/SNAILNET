# Changelog

User-visible changes to SnailNet, newest first. Versions follow
[Semantic Versioning](https://semver.org/).

## 1.0.0 - Unreleased

First release.

### Added

- A TCP relay that holds traffic heading back to the client for a fixed
  number of milliseconds. Traffic to the server is forwarded at once, so a
  request and response pays the latency once.
- A window for setting the listen address and port, the target address and
  port, and the latency, with Start and Stop buttons and a status block that
  counts active and total sessions. The window can be resized and scales
  cleanly on high-DPI displays.
- A command line mode in the same executable: `--listen-addr`,
  `--listen-port`, `--target-addr`, `--target-port`, `--latency`, and
  `--help`. Ctrl+C stops it, and the exit code is `0` for a clean stop and
  `1` for a bad argument or a failure.
- Several clients at once, each with its own connection to the target.
- Up to 1 MB of delayed data held per connection, so a slow client applies
  back pressure instead of growing memory without limit.
- A half close from either side is passed through, not turned into a full
  close.
- A User Guide, shipped as `userguide.pdf` with each release.
