/*
 * launcher_defines.hpp
 */

#pragma once

#include <fc/crypto/private_key.hpp>
#include <fc/crypto/public_key.hpp>
#include <fc/io/json.hpp>
#include <fc/network/ip.hpp>
#include <fc/reflect/variant.hpp>

#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/host_name.hpp>

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <sys/types.h>
#include <ifaddrs.h>

namespace bfs = boost::filesystem;

using private_key_type = fc::crypto::private_key;
using public_key_type = fc::crypto::public_key;

namespace eosio
{
namespace launcher
{

  const std::string BLOCK_DIR = "blocks";
  const std::string SHARED_MEM_DIR = "shared_mem";

  class eosd_def;

  enum launch_modes {
    LM_NONE,
    LM_LOCAL,
    LM_REMOTE,
    LM_NAMED,
    LM_ALL,
    LM_VERIFY
  };

  enum allowed_connection : char {
    PC_NONE = 0,
    PC_PRODUCERS = 1 << 0,
    PC_SPECIFIED = 1 << 1,
    PC_ANY = 1 << 2
  };

  struct tn_node_def {
    std::string          name;
    std::vector<private_key_type> keys;
    std::vector<std::string>  peers;
    std::vector<std::string>  producers;
    eosd_def*       instance;
    std::string          gelf_endpoint;
  };

  struct local_identity {
    std::vector <fc::ip::address> addrs;
    std::vector <std::string> names;

    void initialize () {
      names.push_back ("localhost");
      names.push_back ("127.0.0.1");

      boost::system::error_code ec;
      std::string hn = boost::asio::ip::host_name (ec);
      if (ec.value() != boost::system::errc::success) {
        std::cerr << "unable to retrieve host name: " << ec.message() << std::endl;
      }
      else {
        names.push_back (hn);
        if (hn.find ('.') != std::string::npos) {
          names.push_back (hn.substr (0,hn.find('.')));
        }
      }

      ifaddrs *ifap = 0;
      if (::getifaddrs (&ifap) == 0) {
        for (ifaddrs *p_if = ifap; p_if != 0; p_if = p_if->ifa_next) {
          if (p_if->ifa_addr != 0 &&
              p_if->ifa_addr->sa_family == AF_INET &&
              (p_if->ifa_flags & IFF_UP) == IFF_UP) {
            sockaddr_in *ifaddr = reinterpret_cast<sockaddr_in *>(p_if->ifa_addr);
            int32_t in_addr = ntohl(ifaddr->sin_addr.s_addr);

            if (in_addr != 0) {
              fc::ip::address ifa(in_addr);
              addrs.push_back (ifa);
            }
          }
        }
        ::freeifaddrs (ifap);
      }
      else {
        std::cerr << "unable to query local ip interfaces" << std::endl;
        addrs.push_back (fc::ip::address("127.0.0.1"));
      }
    }

    bool contains (const std::string &name) const {
      try {
        fc::ip::address test(name);
        for (const auto &a : addrs) {
          if (a == test)
            return true;
        }
      }
      catch (...) {
        // not an ip address
        for (const auto n : names) {
          if (n == name)
            return true;
        }
      }
      return false;
    }

  };

  struct remote_deploy {
    std::string ssh_cmd = "/usr/bin/ssh";
    std::string scp_cmd = "/usr/bin/scp";
    std::string ssh_identity;
    std::string ssh_args;
    bfs::path local_config_file = "temp_config";
  };

  struct testnet_def {
    std::string name;
    remote_deploy ssh_helper;
    std::map<std::string,tn_node_def> nodes;
  };

  struct prodkey_def {
    std::string producer_name;
    public_key_type block_signing_key;
  };

  struct producer_set_def {
    uint32_t version;
    std::vector<prodkey_def> producers;
  };

  struct server_name_def {
    std::string ipaddr;
    std::string name;
    bool has_bios;
    std::string eosio_home;
    uint16_t instances;
    server_name_def () : ipaddr(), name(), has_bios(false), eosio_home(), instances(1) {}
  };

  struct server_identities {
    std::vector<server_name_def> producer;
    std::vector<server_name_def> nonprod;
    std::vector<std::string> db;
    std::string default_eosio_home;
    remote_deploy ssh;
  };

  struct node_rt_info {
    bool remote;
    std::string pid_file;
    std::string kill_cmd;
  };

  struct last_run_def {
    std::vector <node_rt_info> running_nodes;
  };

}
}


FC_REFLECT( eosio::launcher::remote_deploy,
              (ssh_cmd)(scp_cmd)(ssh_identity)(ssh_args) )

FC_REFLECT( eosio::launcher::prodkey_def,
              (producer_name)(block_signing_key))

FC_REFLECT( eosio::launcher::producer_set_def,
              (version)(producers))

FC_REFLECT( eosio::launcher::tn_node_def, (name)(keys)(peers)(producers) )

FC_REFLECT( eosio::launcher::testnet_def, (name)(ssh_helper)(nodes) )

FC_REFLECT( eosio::launcher::server_name_def, (ipaddr) (name) (has_bios) (eosio_home) (instances) )

FC_REFLECT( eosio::launcher::server_identities, (producer) (nonprod) (db) (default_eosio_home) (ssh) )

FC_REFLECT( eosio::launcher::node_rt_info, (remote)(pid_file)(kill_cmd) )

FC_REFLECT( eosio::launcher::last_run_def, (running_nodes) )






