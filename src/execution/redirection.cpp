#include "execution/redirection.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <unistd.h>

namespace shell {

namespace {

int posix_dup(int fd) { return dup(fd); }

int posix_open(const char *path, int flags, unsigned int mode) { return open(path, flags, mode); }

int posix_dup2(int old_fd, int new_fd) { return dup2(old_fd, new_fd); }

int posix_close(int fd) { return close(fd); }

const RedirectionSyscalls default_syscalls{
    .dup_fn = &posix_dup,
    .open_fn = &posix_open,
    .dup2_fn = &posix_dup2,
    .close_fn = &posix_close,
};

[[nodiscard]] int target_fd_for(RedirectionOp op) {
    switch (op) {
    case RedirectionOp::StdoutTruncate:
    case RedirectionOp::StdoutAppend:
        return STDOUT_FILENO;
    case RedirectionOp::StderrTruncate:
    case RedirectionOp::StderrAppend:
        return STDERR_FILENO;
    }

    return STDOUT_FILENO;
}

[[nodiscard]] int open_flags_for(RedirectionOp op) {
    switch (op) {
    case RedirectionOp::StdoutTruncate:
    case RedirectionOp::StderrTruncate:
        return O_WRONLY | O_CREAT | O_TRUNC;
    case RedirectionOp::StdoutAppend:
    case RedirectionOp::StderrAppend:
        return O_WRONLY | O_CREAT | O_APPEND;
    }

    return O_WRONLY | O_CREAT | O_TRUNC;
}

} // namespace

RedirectionGuard::RedirectionGuard(std::span<const Redirection> redirections, const RedirectionSyscalls *syscalls)
    : syscalls_(syscalls != nullptr ? syscalls : &default_syscalls) {
    for (const auto &redirection : redirections) {
        if (!apply_redirection(redirection)) {
            valid_ = false;
            restore();
            return;
        }
    }
}

RedirectionGuard::~RedirectionGuard() { restore(); }

bool RedirectionGuard::is_valid() const noexcept { return valid_; }

const std::string &RedirectionGuard::error() const noexcept { return error_; }

bool RedirectionGuard::apply_redirection(const Redirection &redirection) {
    const int target_fd = target_fd_for(redirection.op);

    if (find_backup_fd(target_fd) == -1) {
        const int backup_fd = syscalls_->dup_fn(target_fd);
        if (backup_fd == -1) {
            error_ = std::format("failed to save file descriptor {}: {}", target_fd, std::strerror(errno));
            return false;
        }

        saved_fds_.push_back(SavedFd{.target_fd = target_fd, .backup_fd = backup_fd});
    }

    const int redirected_fd = syscalls_->open_fn(redirection.target.c_str(), open_flags_for(redirection.op), 0644);
    if (redirected_fd == -1) {
        error_ = std::format("failed to open '{}': {}", redirection.target, std::strerror(errno));
        return false;
    }

    if (syscalls_->dup2_fn(redirected_fd, target_fd) == -1) {
        error_ = std::format("failed to redirect file descriptor {}: {}", target_fd, std::strerror(errno));
        syscalls_->close_fn(redirected_fd);
        return false;
    }

    syscalls_->close_fn(redirected_fd);
    return true;
}

void RedirectionGuard::restore() noexcept {
    for (auto it = saved_fds_.rbegin(); it != saved_fds_.rend(); ++it) {
        syscalls_->dup2_fn(it->backup_fd, it->target_fd);
        syscalls_->close_fn(it->backup_fd);
    }

    saved_fds_.clear();
}

int RedirectionGuard::find_backup_fd(int target_fd) const noexcept {
    for (const auto &saved_fd : saved_fds_) {
        if (saved_fd.target_fd == target_fd) {
            return saved_fd.backup_fd;
        }
    }

    return -1;
}

} // namespace shell
