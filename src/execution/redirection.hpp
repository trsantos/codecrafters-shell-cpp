#pragma once

#include <span>
#include <string>
#include <vector>

#include "core/command.hpp"

namespace shell {

struct RedirectionSyscalls {
    int (*dup_fn)(int);
    int (*open_fn)(const char *path, int flags, unsigned int mode);
    int (*dup2_fn)(int old_fd, int new_fd);
    int (*close_fn)(int fd);
};

class RedirectionGuard {
  public:
    explicit RedirectionGuard(
        std::span<const Redirection> redirections, const RedirectionSyscalls *syscalls = nullptr);
    ~RedirectionGuard();

    RedirectionGuard(const RedirectionGuard &) = delete;
    RedirectionGuard &operator=(const RedirectionGuard &) = delete;

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] const std::string &error() const noexcept;

  private:
    struct SavedFd {
        int target_fd;
        int backup_fd;
    };

    std::vector<SavedFd> saved_fds_;
    bool valid_{true};
    std::string error_;
    const RedirectionSyscalls *syscalls_;

    [[nodiscard]] bool apply_redirection(const Redirection &redirection);
    void restore() noexcept;
    [[nodiscard]] int find_backup_fd(int target_fd) const noexcept;
};

} // namespace shell
