/*
 * eosd_def.cpp
 *
 */

#include <eosio/launcher_plugin/eosd_def.hpp>
#include <eosio/launcher_plugin/host_def.hpp>

namespace eosio
{
namespace launcher
{
  eosd_def::eosd_def()
      : config_dir_name (),
        data_dir_name (),
        p2p_port(),
        http_port(),
        file_size(),
        has_db(false),
        name(),
        node(),
        host(),
        p2p_endpoint(),
        dot_label_str() {

  }

  eosd_def::~eosd_def() {

  }

  void eosd_def::mk_dot_label () {
    dot_label_str = name + "\\nprod=";
    if (node == 0 || node->producers.empty()) {
      dot_label_str += "<none>";
    }
    else {
      bool docomma = false;
      for (auto &prod: node->producers) {
        if (docomma)
          dot_label_str += ",";
        else
          docomma = true;
        dot_label_str += prod;
      }
    }
  }

  void eosd_def::set_host(host_def* h, bool is_bios ) {
    host = h->host_name;
    p2p_port = is_bios ? h->p2p_bios_port() : h->p2p_port();
    http_port = is_bios ? h->http_bios_port() : h->http_port();
    file_size = h->def_file_size;
    p2p_endpoint = h->public_name + ":" + boost::lexical_cast<std::string, uint16_t>(p2p_port);
  }

  const std::string& eosd_def::dot_label()
  {
    if (dot_label_str.empty() ) {
        mk_dot_label();
    }
    return dot_label_str;
  }
}
}




