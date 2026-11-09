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

#pragma once

#include <sys/types.h>

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace redroid {

// Thin wrapper around the adb command line client for one device.
class Adb {
 public:
  Adb(std::string adb_path, std::string serial);

  const std::string& serial() const { return serial_; }

  // Makes the waiting loops (Connect, WaitForBoot) return early, e.g. on
  // shutdown.
  void set_cancelled_check(std::function<bool()> cancelled) {
    cancelled_ = std::move(cancelled);
  }

  // Runs `adb -s <serial> <args...>`, returns stdout if the exit code is 0.
  std::optional<std::string> Run(const std::vector<std::string>& args,
                                 bool quiet = false) const;
  std::optional<std::string> Shell(const std::string& command) const;

  // For network serials (host:port): `adb connect`, retried until timeout.
  bool Connect(std::chrono::seconds timeout) const;
  // Waits for sys.boot_completed=1.
  bool WaitForBoot(std::chrono::seconds timeout) const;
  std::optional<std::string> GetProp(const std::string& name) const;

  // Starts `adb -s <serial> <args...>` in the background, stdout/stderr go to
  // this process' stderr. Returns the pid, or -1.
  pid_t Spawn(const std::vector<std::string>& args) const;
  // Same, with a pipe to its stdin, returned in *stdin_fd. Returns the pid, or
  // -1.
  pid_t SpawnWithStdin(const std::vector<std::string>& args,
                       int* stdin_fd) const;

 private:
  std::vector<std::string> Command(const std::vector<std::string>& args) const;

  bool Cancelled() const { return cancelled_ && cancelled_(); }

  std::string adb_path_;
  std::string serial_;
  std::function<bool()> cancelled_;
};

// Runs a command, captures stdout. Returns nullopt if it fails to run or exits
// with a non zero code.
// quiet: discard the command's stderr.
std::optional<std::string> RunCommand(const std::vector<std::string>& argv,
                                      bool quiet = false);
pid_t SpawnCommand(const std::vector<std::string>& argv);

}  // namespace redroid
