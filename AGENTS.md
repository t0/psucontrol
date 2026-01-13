# Ingredients

- Zephyr RTOS (https://www.zephyrproject.org/)
- PMBus / I2C
- mDNS / DNS-SD

# Development Style

- Configuration changes to the host operating system are discouraged if they
  would also require similar changes to other users' host operating systems.
  For example, changing /etc/nsswitch.conf (to switch out mdns4_minimal) might
  alter the behaviour of the development system in a positive way, but this
  would be counterproductive if it also requires this change on all Linux
  users' machines.

# Code Style

- Comments are to clarify the code in places where its behaviour or design are
  not obvious. Do not document the development process ("changed from B to A")
  unless it is well motivated. Do not write comments that just reiterate what
  the code does in a trivial manner.

- For application code, do not follow Zephyr's habit of using configurable
  feature switches unless there are actually build configurations likely to
  exercise more than one path. Switches that are stuck in one position are just
  clutter.

- Do not overuse unicode in user-facing messages (e.g. ✓ and ✗)

# Hints

- Zephyr makes use of statically allocated buffers with sizes configured at
  compile time. When these are exceeded, the resulting error path is often
  easy to confuse with memory exhaustion (e.g. both may report ENOMEM). It
  is worth tracing the specific code path associated with what looks like an
  out-of-memory scenario in order to determine if it is heap/stack exhaustion
  or just a static small allocation that needs to be nudged upwards.

- Zephyr has a limited log buffer and will emit warnings when messages are
  dropped. ("--- 3 messages dropped---") These are log messages, not network
  packets.

- BUGS.md contains useful constraints.
