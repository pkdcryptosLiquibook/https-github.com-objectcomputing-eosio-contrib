/*
 * host_def.hpp
 */
#pragma once

#include <string>
#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/algorithm/string.hpp>

#include <eosio/launcher_plugin/eosd_def.hpp>
#include <eosio/launcher_plugin/launcher_defines.hpp>

namespace eosio
{
namespace launcher
{

class host_def {
public:
  host_def ();
  ~host_def();

  uint16_t p2p_port();
  uint16_t http_port();
  uint16_t p2p_bios_port();
  uint16_t http_bios_port();
  bool is_local( );
  const std::string &dot_label ();
  void set_local_id(boost::shared_ptr<local_identity> id_ptr);

public:
  std::string           genesis;
  std::string           ssh_identity;
  std::string           ssh_args;
  std::string           eosio_home;
  std::string           host_name;
  std::string           public_name;
  std::string           listen_addr;
  uint16_t         base_p2p_port;
  uint16_t         base_http_port;
  uint16_t         def_file_size;
  std::vector<eosd_def> instances;

private:
  uint16_t p2p_count;
  uint16_t http_count;
  std::string   dot_label_str;
  boost::shared_ptr<local_identity> local_id_ptr;

protected:
  void mk_dot_label();
};

}
}

FC_REFLECT( eosio::launcher::host_def,
            (genesis)(ssh_identity)(ssh_args)(eosio_home)
            (host_name)(public_name)
            (base_p2p_port)(base_http_port)(def_file_size)
            (instances) )

