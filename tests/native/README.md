# GPU scheduler seccomp check

Run against an already compiled Chromium **component** build containing the
GPU scheduler policy change:

```bash
python3 tests/native/run_gpu_scheduler_policy_test.py \
  --chromium /path/to/chromium/src --build-dir out/Vsr
```

The runner obtains the policy object's existing compiler command from Ninja,
compiles only the harness into a temporary directory, and links the existing
Chromium component libraries. It does not rebuild or modify Chromium objects
or libraries. The loaded `libsandbox_policy.so` must already reflect the source
being validated; this check does not prove source/binary freshness.

The child installs the complete `GpuProcessPolicy` using Chromium's
`SandboxBPF`, with an empty broker that allows no commands or file paths. It
verifies kernel seccomp mode 2 and executes both scheduler range queries for
`SCHED_OTHER`, `SCHED_FIFO`, `SCHED_RR`, `-1`, and `123456`. Only `SCHED_OTHER`
must return zero; every other case must return `-1` with `EINVAL`. The parent
reports crashes and unexpected results as failures.

Child exit 10 means sandbox initialization failed; 11 means seccomp mode was
unexpected. Exits 20–29 identify a failed case in the query/policy order above.
This validates the syscall restriction, not browser playback or GPU thread
sandbox coverage.
