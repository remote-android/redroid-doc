/*
 * Copyright (C) 2026 The redroid Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/frontend/redroid_webrtc/adb.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <thread>

#include "absl/log/log.h"

extern char** environ;

namespace redroid {
namespace {

std::vector<char*> ToArgv(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  for (const auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);
  return argv;
}

std::string Trim(std::string s) {
  while (!s.empty() &&
         (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) {
    s.pop_back();
  }
  return s;
}

}  // namespace

std::optional<std::string> RunCommand(const std::vector<std::string>& args,
                                      bool quiet) {
  int pipe_fds[2];
  if (pipe2(pipe_fds, O_CLOEXEC) != 0) {
    PLOG(ERROR) << "pipe2";
    return std::nullopt;
  }
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
  posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null",
                                   O_RDONLY, 0);
  if (quiet) {
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                     O_WRONLY, 0);
  }
  auto argv = ToArgv(args);
  pid_t pid;
  int err =
      posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(pipe_fds[1]);
  if (err != 0) {
    LOG(ERROR) << "Failed to run " << args[0] << ": " << strerror(err);
    close(pipe_fds[0]);
    return std::nullopt;
  }
  std::string output;
  char buf[4096];
  ssize_t n;
  while ((n = TEMP_FAILURE_RETRY(read(pipe_fds[0], buf, sizeof(buf)))) > 0) {
    output.append(buf, n);
  }
  close(pipe_fds[0]);
  int status;
  if (TEMP_FAILURE_RETRY(waitpid(pid, &status, 0)) != pid ||
      !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    return std::nullopt;
  }
  return output;
}

pid_t SpawnCommand(const std::vector<std::string>& args) {
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null",
                                   O_RDONLY, 0);
  posix_spawn_file_actions_adddup2(&actions, STDERR_FILENO, STDOUT_FILENO);
  auto argv = ToArgv(args);
  pid_t pid;
  int err =
      posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (err != 0) {
    LOG(ERROR) << "Failed to spawn " << args[0] << ": " << strerror(err);
    return -1;
  }
  return pid;
}

Adb::Adb(std::string adb_path, std::string serial)
    : adb_path_(std::move(adb_path)), serial_(std::move(serial)) {}

std::vector<std::string> Adb::Command(
    const std::vector<std::string>& args) const {
  std::vector<std::string> cmd = {adb_path_, "-s", serial_};
  cmd.insert(cmd.end(), args.begin(), args.end());
  return cmd;
}

std::optional<std::string> Adb::Run(const std::vector<std::string>& args,
                                    bool quiet) const {
  return RunCommand(Command(args), quiet);
}

std::optional<std::string> Adb::Shell(const std::string& command) const {
  return Run({"shell", command});
}

pid_t Adb::Spawn(const std::vector<std::string>& args) const {
  return SpawnCommand(Command(args));
}

pid_t Adb::SpawnWithStdin(const std::vector<std::string>& args,
                          int* stdin_fd) const {
  int fds[2];
  if (pipe2(fds, O_CLOEXEC) != 0) {
    return -1;
  }
  pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    return -1;
  }
  if (pid == 0) {
    dup2(fds[0], STDIN_FILENO);
    std::vector<std::string> cmd = Command(args);
    std::vector<char*> argv;
    for (auto& arg : cmd) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  close(fds[0]);
  *stdin_fd = fds[1];
  return pid;
}

bool Adb::Connect(std::chrono::seconds timeout) const {
  if (serial_.find(':') == std::string::npos) {
    return true;  // USB / emulator serial, nothing to connect.
  }
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline && !Cancelled()) {
    auto out = RunCommand({adb_path_, "connect", serial_});
    if (out && out->find("connected to") != std::string::npos) {
      // `adb connect` succeeds before the device is usable, check it.
      auto state = Run({"get-state"});
      if (state && Trim(*state) == "device") {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
  return false;
}

std::optional<std::string> Adb::GetProp(const std::string& name) const {
  auto out = Shell("getprop " + name);
  if (!out) {
    return std::nullopt;
  }
  return Trim(*out);
}

bool Adb::WaitForBoot(std::chrono::seconds timeout) const {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline && !Cancelled()) {
    if (GetProp("sys.boot_completed") == "1") {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
  return false;
}

}  // namespace redroid
