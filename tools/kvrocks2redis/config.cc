#include "config.h"
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <glog/logging.h>
#include <rocksdb/env.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "../../src/util.h"
#include "../../src/status.h"
#include "../../src/config.h"

namespace Kvrocks2redis {

static const char *kLogLevels[] = {"info", "warning", "error", "fatal"};
static const size_t kNumLogLevel = sizeof(kLogLevels) / sizeof(kLogLevels[0]);

int Config::yesnotoi(std::string input) {
  if (strcasecmp(input.data(), "yes") == 0) {
    return 1;
  } else if (strcasecmp(input.data(), "no") == 0) {
    return 0;
  }
  return -1;
}

Status Config::parseRocksdbOption(std::string key, std::string value) {
  int32_t n;
  try {
    n = std::stoi(value);
  } catch (std::exception &e) {
    return Status(Status::NotOK, e.what());
  }
  if (key == "max_open_files") {
    rocksdb_options.max_open_files = n;
  } else {
    return Status(Status::NotOK, "Bad directive or wrong number of arguments");
  }
  return Status::OK();
}

Status Config::parseConfigFromString(std::string input) {
  std::vector<std::string> args;
  Util::Split(input, " \t\r\n", &args);
  // omit empty line and comment
  if (args.empty() || args[0].front() == '#') return Status::OK();

  size_t size = args.size();
  if (size == 2 && args[0] == "workers") {
    workers = std::stoi(args[1]);
    if (workers < 1 || workers > 1024) {
      return Status(Status::NotOK, "too many worker threads");
    }
  } else if (size == 2 && args[0] == "daemonize") {
    int i;
    if ((i = yesnotoi(args[1])) == -1) {
      return Status(Status::NotOK, "argument must be 'yes' or 'no'");
    }
    daemonize = (i == 1);
  } else if (size == 2 && args[0] == "dir") {
    dir = args[1];
    db_dir = dir + "/db";
    next_seq_file_path = dir + "/last_next_seq.txt";
  } else if (size == 2 && args[0] == "db-name") {
    db_name = args[1];
  } else if (size == 2 && args[0] == "kvrocksauth") {
    kvrocks_auth = args[1];
  } else if (size == 2 && args[0] == "requirepass") {
    requirepass = args[1];
  } else if (size == 2 && args[0] == "pidfile") {
    pidfile = args[1];
  } else if (size == 2 && args[0] == "loglevel") {
    for (size_t i = 0; i < kNumLogLevel; i++) {
      if (Util::ToLower(args[1]) == kLogLevels[i]) {
        loglevel = static_cast<int>(i);
        break;
      }
    }
  } else if (size == 3 && args[0] == "kvrocks") {
    if (args[1] != "no" && args[2] != "one") {
      kvrocks_host = args[1];
      // we use port + 1 as repl port, so incr the kvrocks port here
      kvrocks_port = std::stoi(args[2]) + 1;
      if (kvrocks_port <= 0 || kvrocks_port >= 65535) {
        return Status(Status::NotOK, "kvrocks port range should be between 0 and 65535");
      }
    }
  } else if (size == 2 && !strncasecmp(args[0].data(), "rocksdb.", 8)) {
    return parseRocksdbOption(args[0].substr(8, args[0].size() - 8), args[1]);
  } else if (size == 2 && !strncasecmp(args[0].data(), "namespace.", 10)) {
    std::string ns = args[0].substr(10, args.size() - 10);
    if (ns.size() > INT8_MAX) {
      return Status(Status::NotOK, std::string("namespace size exceed limit ") + std::to_string(INT8_MAX));
    }
    tokens[args[1]] = ns;
  } else {
    return Status(Status::NotOK, "Bad directive or wrong number of arguments");
  }
  return Status::OK();
}

Status Config::Load(std::string path) {
  path_ = std::move(path);
  std::ifstream file(path_);
  if (!file.is_open()) {
    return Status(Status::NotOK, strerror(errno));
  }

  std::string line;
  int line_num = 1;
  while (!file.eof()) {
    std::getline(file, line);
    line = Util::ToLower(line);
    Status s = parseConfigFromString(line);
    if (!s.IsOK()) {
      file.close();
      return Status(Status::NotOK, "at line: #L" + std::to_string(line_num) + ", err: " + s.Msg());
    }
    line_num++;
  }

  auto s = rocksdb::Env::Default()->CreateDirIfMissing(dir);
  if (!s.ok()) return Status(Status::NotOK, s.ToString());

  if (requirepass.empty()) {
    file.close();
    return Status(Status::NotOK, "requirepass cannot be empty");
  }
  tokens[requirepass] = kDefaultNamespace;
  file.close();
  return Status::OK();
}

void Config::GetNamespace(const std::string &ns, std::string *token) {
  for (auto iter = tokens.begin(); iter != tokens.end(); iter++) {
    if (iter->second == ns) {
      *token = iter->first;
    }
  }
}

}  // namespace Kvrocks2redis

