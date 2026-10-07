// Exercise the actual Chromium GPU seccomp policy in an isolated child.
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <memory>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "sandbox/linux/seccomp-bpf/sandbox_bpf.h"
#include "sandbox/linux/syscall_broker/broker_command.h"
#include "sandbox/policy/linux/bpf_gpu_policy_linux.h"
#include "sandbox/policy/linux/sandbox_linux.h"

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  const pid_t child = fork();
  if (child < 0) {
    perror("fork");
    return 1;
  }
  if (child == 0) {
    auto* linux_sandbox = sandbox::policy::SandboxLinux::GetInstance();
    linux_sandbox->PreinitializeSandbox();
    // GPU policy compilation expects a broker. Give this test broker no file
    // permissions and no allowed commands; the tested queries do not use it.
    linux_sandbox->StartBrokerProcess({}, {}, {});
    sandbox::SandboxBPF filter(
        std::make_unique<sandbox::policy::GpuProcessPolicy>(
            sandbox::policy::MremapPolicy::kBlock));
    if (!filter.StartSandbox(
            sandbox::SandboxBPF::SeccompLevel::SINGLE_THREADED)) {
      _exit(10);
    }
    // Verify the filter really entered the kernel, rather than only evaluating
    // expression trees in userspace.
    if (prctl(PR_GET_SECCOMP) != 2)
      _exit(11);
    constexpr int calls[] = {__NR_sched_get_priority_min,
                             __NR_sched_get_priority_max};
    constexpr int policies[] = {SCHED_OTHER, SCHED_FIFO, SCHED_RR, -1, 123456};
    int index = 0;
    for (int call : calls) {
      for (int policy : policies) {
        errno = 0;
        const long result = syscall(call, policy);
        const int error = errno;
        if (policy == SCHED_OTHER ? (result != 0 || error != 0)
                                  : (result != -1 || error != EINVAL)) {
          _exit(20 + index);
        }
        ++index;
      }
    }
    // Avoid teardown making unrelated calls after installing the GPU policy.
    _exit(0);
  }
  int status = 0;
  if (waitpid(child, &status, 0) != child) {
    perror("waitpid");
    return 1;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "FAIL: sandbox child wait status=%d (exit=%d, signal=%d)\n",
            status, WIFEXITED(status) ? WEXITSTATUS(status) : -1,
            WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    return 1;
  }
  puts("PASS: actual GPU seccomp filter; min/max OTHER=0; FIFO/RR/invalid=EINVAL (10 cases)");
  return 0;
}
