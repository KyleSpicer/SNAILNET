# SNAILNET

SnailNet is a lightweight Win32 TCP proxy that injects configurable latency into a connection. It listens on a local address and port, forwards traffic to a target address and port, and delays the relayed stream by a user-specified number of milliseconds. Useful for testing how applications behave over slow or high-latency links without touching the network stack.