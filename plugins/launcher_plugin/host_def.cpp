/*
 * host_def.cpp
 *
 */

#include <eosio/launcher_plugin/host_def.hpp>

namespace eosio
{
namespace launcher
{

host_def::host_def ()
    : genesis("genesis.json"),
      ssh_identity (""),
      ssh_args (""),
      eosio_home(),
      host_name("127.0.0.1"),
      public_name("localhost"),
      listen_addr("0.0.0.0"),
      base_p2p_port(9876),
      base_http_port(8888),
      def_file_size(8192),
      instances(),
      p2p_count(0),
      http_count(0),
      dot_label_str() {

}

host_def::~host_def() {

}

uint16_t host_def::p2p_port() {
  return base_p2p_port + p2p_count++;
}

uint16_t host_def::http_port() {
  return base_http_port + http_count++;
}

uint16_t host_def::p2p_bios_port() {
  return base_p2p_port - 100;
}

uint16_t host_def::http_bios_port() {
   return base_http_port - 100;
}

bool host_def::is_local( ) {
  if(local_id_ptr)
    return local_id_ptr->contains(host_name);
  else
    return false;
}

const std::string& host_def::dot_label () {
  if (dot_label_str.empty() ) {
      mk_dot_label();
  }
  return dot_label_str;
}

void host_def::set_local_id(boost::shared_ptr<local_identity> id_ptr) {
    local_id_ptr = id_ptr;
}

void host_def::mk_dot_label() {
  if (public_name.empty()) {
    dot_label_str = host_name;
  }
  else if (boost::iequals(public_name,host_name)) {
    dot_label_str = public_name;
  }
  else
    dot_label_str = public_name + "/" + host_name;
}

}
}
