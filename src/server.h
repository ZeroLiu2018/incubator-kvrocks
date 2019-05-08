#pragma once

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <map>
#include <list>
#include <string>
#include <vector>

#include "stats.h"
#include "storage.h"
#include "task_runner.h"
#include "replication.h"
#include "redis_metadata.h"

namespace Redis {
class Connection;
}

class WorkerThread;
class Worker;

struct DBScanInfo {
  time_t last_scan_time = 0;
  uint64_t n_key = 0;
  bool is_scanning = false;
};

struct SlowlogEntry {
  std::vector<std::string> args;
  uint64_t id;
  uint64_t duration;
  time_t time;
};

struct SlowLog {
  std::list<SlowlogEntry> entry_list;
  uint64_t id = 0;
  std::mutex mu;
};

// Used by master role, tracking slaves' info
struct SlaveInfo {
  std::string addr;
  uint32_t port;
  rocksdb::SequenceNumber seq = 0;
  SlaveInfo(std::string a, uint32_t p) : addr(std::move(a)), port(p) {}
};

struct ConnContext {
  Worker *owner;
  int fd;
  ConnContext(Worker *w, int fd) : owner(w), fd(fd) {}
};

class Server {
 public:
  explicit Server(Engine::Storage *storage, Config *config);
  ~Server();

  Status Start();
  void Stop();
  void Join();
  bool IsStopped() { return stop_; }
  bool IsLoading() { return is_loading_; }
  Config *GetConfig() { return config_; }

  using SlaveInfoPos = std::list<std::shared_ptr<SlaveInfo>>::iterator;

  Status AddMaster(std::string host, uint32_t port);
  Status RemoveMaster();
  bool IsSlave() { return !master_host_.empty(); }
  void RemoveSlave(const SlaveInfoPos &pos);
  SlaveInfoPos AddSlave(const std::string &addr, uint32_t port);
  void UpdateSlaveStats(const SlaveInfoPos &pos, rocksdb::SequenceNumber seq);
  void FeedMonitorConns(Redis::Connection *conn, const std::vector<std::string> &tokens);

  int PublishMessage(const std::string &channel, const std::string &msg);
  void SubscribeChannel(const std::string &channel, Redis::Connection *conn);
  void UnSubscribeChannel(const std::string &channel, Redis::Connection *conn);

  void AddBlockingKey(const std::string &key, Redis::Connection *conn);
  void UnBlockingKey(const std::string &key, Redis::Connection *conn);
  Status WakeupBlockingConns(const std::string &key, size_t n_conns);

  std::string GetLastRandomKeyCursor() { return last_random_key_cursor_; }
  void SetLastRandomKeyCursor(std::string cursor) { last_random_key_cursor_ = cursor; }

  void GetStatsInfo(std::string *info);
  void GetServerInfo(std::string *info);
  void GetMemoryInfo(std::string *info);
  void GetRocksDBInfo(std::string *info);
  void GetClientsInfo(std::string *info);
  void GetReplicationInfo(std::string *info);
  void GetCommandsStatsInfo(std::string *info);
  void GetInfo(const std::string &ns, const std::string &section, std::string *info);
  std::string GetRocksDBStatsJson();

  Status AsyncCompactDB();
  Status AsyncBgsaveDB();
  Status AsyncScanDBSize(const std::string &ns);
  uint64_t GetLastKeyNum(const std::string &ns);
  time_t GetLastScanTime(const std::string &ns);

  void SlowlogReset();
  uint32_t SlowlogLen();
  void CreateSlowlogReply(std::string *output, uint32_t count);
  void SlowlogPushEntryIfNeeded(const std::vector<std::string>* args, uint64_t duration);

  void DecrClients();
  Status IncrClients();
  int IncrMonitorClientNum();
  int DecrMonitorClientNum();
  std::string GetClientsStr();
  std::atomic<uint64_t> *GetClientID();
  void KillClient(int64_t *killed, std::string addr, uint64_t id, bool skipme, Redis::Connection *conn);

  Stats stats_;
  Engine::Storage *storage_;

 private:
  void cron();
  void delConnContext(ConnContext *c);

  bool stop_ = false;
  bool is_loading_ = false;
  time_t start_time_ = 0;
  std::mutex slaveof_mu_;
  std::string master_host_;
  uint32_t master_port_ = 0;
  Config *config_ = nullptr;
  std::string last_random_key_cursor_;

  // client counters
  std::atomic<uint64_t> client_id_{1};
  std::atomic<int> connected_clients_{0};
  std::atomic<int> monitor_clients_{0};
  std::atomic<uint64_t> total_clients_{0};

  // slave
  std::mutex slaves_info_mu_;
  std::list<std::shared_ptr<SlaveInfo>> slaves_info_;
  using slaves_info_iter_ = std::list<std::shared_ptr<SlaveInfo>>::iterator;

  std::mutex db_mu_;
  bool db_compacting_ = false;
  bool db_bgsave_ = false;
  std::map<std::string, DBScanInfo> db_scan_infos_;

  SlowLog slowlog_;
  std::map<std::string, std::list<Redis::Connection *>> pubsub_channels_;
  std::map<std::string, std::list<ConnContext *>> blocking_keys_;
  std::map<ConnContext *, bool> conn_ctxs_;
  std::mutex blocking_keys_mu_;

  // threads
  std::thread cron_thread_;
  TaskRunner *task_runner_;
  std::vector<WorkerThread *> worker_threads_;
  std::unique_ptr<ReplicationThread> replication_thread_;
};
