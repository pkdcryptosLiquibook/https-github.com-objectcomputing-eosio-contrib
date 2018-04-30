/*
 * eosd_def.hpp
 */

#pragma once

#include <string>
#include <sys/types.h>

#include <eosio/launcher_plugin/launcher_defines.hpp>

namespace eosio
{
namespace launcher
{

class host_def;

class eosd_def {
public:
  eosd_def();
  ~eosd_def();

  void set_host(host_def* h, bool is_bios);

  void mk_dot_label();

  const std::string &dot_label();

public:
  std::string       config_dir_name;
  std::string       data_dir_name;
  uint16_t     p2p_port;
  uint16_t     http_port;
  uint16_t     file_size;
  bool         has_db;
  std::string       name;
  tn_node_def* node;
  std::string       host;
  std::string       p2p_endpoint;

private:
  std::string dot_label_str;
};

}
}

FC_REFLECT( eosio::launcher::eosd_def,
            (name)(config_dir_name)(data_dir_name)(has_db)
            (p2p_port)(http_port)(file_size) )


