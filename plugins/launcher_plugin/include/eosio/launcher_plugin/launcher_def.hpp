/*
 * launcher_def.hpp
 */

#pragma once

#include <string>
#include <vector>
#include <utility>

#include <eosio/launcher_plugin/launcher_defines.hpp>
#include <eosio/launcher_plugin/eosd_def.hpp>
#include <eosio/launcher_plugin/host_def.hpp>

#include <boost/program_options.hpp>
#include <boost/filesystem/path.hpp>

namespace bfs = boost::filesystem;

namespace eosio
{
namespace launcher
{

class launcher_def {
public:
  launcher_def();

  ~launcher_def();

  void set_local_id(boost::shared_ptr<local_identity> local_id_ptr);
  void set_options (boost::program_options::options_description& cli);
  void initialize (const boost::program_options::variables_map &vmap);
  void launch (eosd_def &node, std::string &gts);
  void kill (launch_modes mode, std::string sig_opt);
  void bounce (const std::string& node_numbers);
  void down (const std::string& node_numbers);
  void roll (const std::string& host_names);
  bool generate ();
  void start_all (std::string &gts, launch_modes mode);
  void ignite ();

  std::string get_launch_name() {
    return launch_name;
  }

  bfs::path get_output_dir() {
    return output;
  }

private:
  void assign_name (eosd_def &node, bool is_bios);
  void init_genesis ();
  void load_servers ();
  void define_network ();
  void bind_nodes ();
  host_def* find_host (const std::string &name);
  host_def* find_host_by_name_or_address (const std::string &name);
  host_def* deploy_config_files (tn_node_def &node);
  std::string compose_scp_command (const host_def &host, const bfs::path &source,
                              const bfs::path &destination);
  void write_config_file (tn_node_def &node);
  void write_logging_config_file (tn_node_def &node);
  void write_genesis_file (tn_node_def &node);
  void write_setprods_file ();
  void write_bios_boot ();

  std::pair<host_def, eosd_def> find_node(uint16_t node_num);
  std::vector<std::pair<host_def, eosd_def>> get_nodes(const std::string& node_number_list);

  bool   is_bios_ndx (size_t ndx);
  size_t start_ndx();
  bool   next_ndx(size_t &ndx);
  size_t skip_ndx (size_t from, size_t offset);

  void make_ring ();
  void make_star ();
  void make_mesh ();
  void make_custom ();
  void write_dot_file ();
  void format_ssh (const std::string &cmd, const std::string &host_name, std::string &ssh_cmd_line);
  bool do_ssh (const std::string &cmd, const std::string &host_name);
  void prep_remote_config_dir (eosd_def &node, host_def *host);

  bool force_overwrite;
  size_t total_nodes;
  size_t prod_nodes;
  size_t producers;
  size_t next_node;
  std::string shape;
  allowed_connection allowed_connections = PC_NONE;
  bfs::path genesis;
  bfs::path output;
  bfs::path host_map_file;
  bfs::path server_ident_file;
  bfs::path stage;

  std::string erd;
  bfs::path config_dir_base;
  bfs::path data_dir_base;
  bool skip_transaction_signatures = false;
  std::string eosd_extra_args;
  testnet_def network;
  std::string gelf_endpoint;
  std::vector<std::string> aliases;
  std::vector<host_def> bindings;
  int per_host = 0;
  last_run_def last_run;
  int start_delay = 0;
  bool gelf_enabled;
  bool nogen;
  bool boot;
  bool add_enable_stale_production = false;
  std::string launch_name;
  std::string launch_time;
  server_identities servers;
  producer_set_def producer_set;
  std::vector<std::string> genesis_block;
  std::string start_temp;
  std::string start_script;
  boost::shared_ptr<local_identity> id_ptr;
};

}
}
