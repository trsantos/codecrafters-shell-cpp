#include "execution/redirection.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <unistd.h>

namespace shell {

namespace {

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

RedirectionGuard::RedirectionGuard(std::span<const Redirection> redirections) {
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
        const int backup_fd = dup(target_fd);
        if (backup_fd == -1) {
            error_ = std::format("failed to save file descriptor {}: {}", target_fd, std::strerror(errno));
            return false;
        }

        saved_fds_.push_back(SavedFd{.target_fd = target_fd, .backup_fd = backup_fd});
    }

    const int redirected_fd = open(redirection.target.c_str(), open_flags_for(redirection.op), 0644);
    if (redirected_fd == -1) {
        error_ = std::format("failed to open '{}': {}", redirection.target, std::strerror(errno));
        return false;
    }

    if (dup2(redirected_fd, target_fd) == -1) {
        error_ = std::format("failed to redirect file descriptor {}: {}", target_fd, std::strerror(errno));
        close(redirected_fd);
        return false;
    }

    close(redirected_fd);
    return true;
}

void RedirectionGuard::restore() noexcept {
    for (auto it = saved_fds_.rbegin(); it != saved_fds_.rend(); ++it) {
        dup2(it->backup_fd, it->target_fd);
        close(it->backup_fd);
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
