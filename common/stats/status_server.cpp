/// Copyright 2016 Pinterest Inc.
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
/// http://www.apache.org/licenses/LICENSE-2.0

/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.


// Implementation of Status Server based on microhttpd.

#include "common/stats/status_server.h"

#include <sys/select.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <microhttpd.h>

#include <memory>
#include <string>
#include "common/stats/stats.h"

DEFINE_int32(
    http_status_port, 9999,
    "Port at which status information such as build info/stats is exported.");

namespace common {

namespace {

int ServeCallback(void* param, struct MHD_Connection* connection,
                  const char* url, const char* method, const char* version,
                  const char* upload_data, size_t* upload_data_size,
                  void** ptr) {
  /********* BOILERPLATE code **********/
  static int dummy;
  struct MHD_Response* response;
  int ret;

  if (0 != strcmp(method, "GET")) return MHD_NO; /* unexpected method */
  if (&dummy != *ptr) {
    /* The first time only the headers are valid,
       do not respond in the first round... */
    *ptr = &dummy;
    return MHD_YES;
  }
  if (0 != *upload_data_size) return MHD_NO; /* upload data in a GET!? */
  *ptr = NULL;                               /* clear context pointer */

  StatusServer* server = reinterpret_cast<StatusServer*>(param);
  auto str = server->GetPageContent(url);
  response = MHD_create_response_from_data(str.size(),
                                           const_cast<char*>(str.c_str()),
                                           MHD_NO, MHD_YES);
  ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
  MHD_destroy_response(response);
  return ret;
}
}  // namespace

void StatusServer::StartStatusServer(EndPointToOPMap op_map) {
  static StatusServer server(FLAGS_http_status_port, std::move(op_map));
}

void StatusServer::StartStatusServerOrDie(EndPointToOPMap op_map) {
  static StatusServer server(FLAGS_http_status_port, std::move(op_map));
  CHECK(server.Serving());
}

StatusServer::StatusServer(uint16_t port, EndPointToOPMap op_map)
    : port_(port), d_(nullptr), op_map_(std::move(op_map)) {

  op_map_.emplace("/stats.txt", [this]() {
    std::string ret = common::Stats::get()->DumpStatsAsText();
    auto itor = op_map_.find("/rocksdb_info.txt");
    if (itor != op_map_.end()) {
      ret += itor->second();
    }

    return ret;
  });

  if (!this->Serve()) {
    LOG(INFO) << "Failed to start status server at " << port;
    LOG(INFO) << strerror(errno);
  }
}

std::string StatusServer::GetPageContent(const std::string& end_point) {
  auto itor = op_map_.find(end_point);
  if (itor == op_map_.end()) {
    return "Unsupported http path.\n";
  }

  return itor->second();
}

void StatusServer::Stop() {
  if (d_) {
    MHD_stop_daemon(d_);
  }
}

bool StatusServer::Serve() {
  LOG(INFO) << "Starting status server at " << port_;
  d_ = MHD_start_daemon(MHD_USE_POLL_INTERNALLY, port_, NULL, NULL,
                        &ServeCallback, this, MHD_OPTION_END);
  return (d_ != nullptr);
}

bool StatusServer::Serving() {
  return (d_ != nullptr);
}
}  // namespace common
