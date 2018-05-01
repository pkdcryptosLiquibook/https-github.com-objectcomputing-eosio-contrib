/*
 * launcher_def.cpp
 */

#include <eosio/launcher_plugin/launcher_def.hpp>

#include <string>
#include <vector>
#include <math.h>
#include <sstream>
#include <regex>

#include <boost/algorithm/string.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/program_options.hpp>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
#include <boost/process/child.hpp>
#pragma GCC diagnostic pop
#include <boost/process/system.hpp>
#include <boost/process/io.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>

#include <fc/crypto/private_key.hpp>
#include <fc/crypto/public_key.hpp>
#include <fc/io/json.hpp>
#include <fc/network/ip.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/log/logger_config.hpp>
#include <fc/log/logger.hpp>

#include <eosio/chain/contracts/genesis_state.hpp>

namespace bfs = boost::filesystem;
namespace bp = boost::process;
namespace bpo = boost::program_options;

namespace eosio
{
namespace launcher
{

launcher_def::launcher_def()
: force_overwrite(false)
, total_nodes(0)
, prod_nodes(0)
, producers(0)
, next_node(0)
, shape()
, allowed_connections(PC_NONE)
, genesis()
, output()
, host_map_file()
, server_ident_file()
, stage()
, erd()
, config_dir_base()
, data_dir_base()
, skip_transaction_signatures(false)
, eosd_extra_args()
, network()
, gelf_endpoint()
, aliases()
, bindings()
, per_host(0)
, last_run()
, start_delay(0)
, gelf_enabled(false)
, nogen(false)
, boot(false)
, add_enable_stale_production(false)
, launch_name()
, launch_time()
, servers()
, producer_set()
, genesis_block()
, start_temp()
, start_script() {

}

launcher_def::~launcher_def() {

}

void launcher_def::set_local_id(boost::shared_ptr<local_identity> local_id_ptr) {
  id_ptr = local_id_ptr;
}

void launcher_def::set_options (bpo::options_description &cli) {

}

template<class enum_type, class=typename std::enable_if<std::is_enum<enum_type>::value>::type>
inline enum_type& operator|=(enum_type&lhs, const enum_type& rhs)
{
  using T = std::underlying_type_t <enum_type>;
  return lhs = static_cast<enum_type>(static_cast<T>(lhs) | static_cast<T>(rhs));
}

void launcher_def::initialize (const bpo::variables_map &vmap) {

  // assign values from config, these have default values
  force_overwrite = vmap["force"].as<bool>();
  total_nodes = vmap["nodes"].as<size_t>();
  prod_nodes = vmap["pnodes"].as<size_t>();
  producers = vmap["producers"].as<size_t>();
  shape = vmap["shape"].as<std::string>();
  genesis = vmap["genesis"].as<bfs::path>();
  skip_transaction_signatures = vmap["skip-signature"].as<bool>();
  start_delay = vmap["delay"].as<int>();
  boot = vmap["boot"].as<bool>();
  nogen = vmap["nogen"].as<bool>();
  host_map_file = vmap["host-map"].as<bfs::path>();
  server_ident_file = vmap["servers"].as<bfs::path>();
  per_host = vmap["per-host"].as<int>();
  network.name = vmap["network-name"].as<std::string>();
  gelf_enabled = vmap["enable-gelf-logging"].as<bool>();
  gelf_endpoint = vmap["gelf-endpoint"].as<std::string>();
  start_temp = vmap["template"].as<std::string>();
  start_script = vmap["script"].as<std::string>();

  if(vmap.count("nodeos")) {
    eosd_extra_args = vmap.at("nodeos").as<std::string>();
  }

  if(vmap.count("output")) {
    output = vmap.at("output").as<bfs::path>();
  }

  if (vmap.count("mode")) {
    const std::vector<std::string> modes = vmap["mode"].as<std::vector<std::string>>();
    for(const std::string&m : modes)
    {
      if (boost::iequals(m, "any"))
        allowed_connections |= PC_ANY;
      else if (boost::iequals(m, "producers"))
        allowed_connections |= PC_PRODUCERS;
      else if (boost::iequals(m, "specified"))
        allowed_connections |= PC_SPECIFIED;
      else if (boost::iequals(m, "none"))
        allowed_connections = PC_NONE;
      else {
	elog("Unrecognized connection mode: ${mode}", ("mode", m));
        exit (-1);
      }
    }
  }

  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
  std::time_t now_c = std::chrono::system_clock::to_time_t(now);
  std::ostringstream dstrm;
  dstrm << std::put_time(std::localtime(&now_c), "%Y_%m_%d_%H_%M_%S");
  launch_time = dstrm.str();

  if ( ! (shape.empty() ||
          boost::iequals( shape, "ring" ) ||
          boost::iequals( shape, "star" ) ||
          boost::iequals( shape, "mesh" )) &&
       host_map_file.empty()) {
    bfs::path src = shape;
    host_map_file = src.stem().string() + "_hosts.json";
  }

  if( !host_map_file.empty() ) {
    try {
      fc::json::from_file(host_map_file).as<std::vector<host_def>>(bindings);
      for (auto &binding : bindings) {
	binding.set_local_id(id_ptr);
        for (auto &eosd : binding.instances) {
          eosd.host = binding.host_name;
          eosd.p2p_endpoint = binding.public_name + ":" + boost::lexical_cast<std::string,uint16_t>(eosd.p2p_port);

          aliases.push_back (eosd.name);
        }
      }
    } catch (...) { // this is an optional feature, so an exception is OK
    }
  }

  config_dir_base = "etc/eosio";
  data_dir_base = "var/lib";
  next_node = 0;
  ++prod_nodes; // add one for the bios node
  ++total_nodes;

  load_servers ();

  if (prod_nodes > (producers + 1))
    prod_nodes = producers;
  if (prod_nodes > total_nodes)
    total_nodes = prod_nodes;

  char* erd_env_var = getenv ("EOSIO_HOME");
  if (erd_env_var == nullptr || std::string(erd_env_var).empty()) {
     erd_env_var = getenv ("PWD");
  }

  if (erd_env_var != nullptr) {
     erd = erd_env_var;
  } else {
     erd.clear();
  }

  stage = bfs::path(erd);
  if (!bfs::exists(stage)) {
    elog("${erd} is not a valid path.", ("erd", stage.string()));
    exit (-1);
  }
  stage /= bfs::path("staging");
  bfs::create_directories (stage);
  if (bindings.empty()) {
    define_network ();
  }

}

void
launcher_def::load_servers () {

  ilog("loading servers");

  if (!server_ident_file.empty()) {
    try {
      fc::json::from_file(server_ident_file).as<server_identities>(servers);
      prod_nodes = 0;
      for (auto &s : servers.producer) {
         prod_nodes += s.instances;
      }

      total_nodes = prod_nodes;
      for (auto &s : servers.nonprod) {
         total_nodes += s.instances;
      }

      per_host = 1;
      network.ssh_helper = servers.ssh;
    }
    catch (...) {
      elog("Unable to load server identity file: ${server_ident}", ("server_ident", server_ident_file.string()));
      exit (-1);
    }
  }
}


void
launcher_def::assign_name (eosd_def &node, bool is_bios) {
   std::string node_cfg_name;

   if (is_bios) {
      node.name = "bios";
      node_cfg_name = "node_bios";
   }
   else {
      std::string dex = next_node < 10 ? "0":"";
      dex += boost::lexical_cast<std::string,int>(next_node++);
      node.name = network.name + dex;
      node_cfg_name = "node_" + dex;
   }
  node.config_dir_name = (config_dir_base / node_cfg_name).string();
  node.data_dir_name = (data_dir_base / node_cfg_name).string();
}

bool
launcher_def::generate () {
  if (boost::iequals (shape,"ring")) {
    make_ring ();
  }
  else if (boost::iequals (shape, "star")) {
    make_star ();
  }
  else if (boost::iequals (shape, "mesh")) {
    make_mesh ();
  }
  else {
    make_custom ();
  }

  if( !nogen ) {
     write_setprods_file();
     write_bios_boot();
     init_genesis();
     for (auto &node : network.nodes) {
        write_config_file(node.second);
        write_logging_config_file(node.second);
        write_genesis_file(node.second);
     }
  }
  write_dot_file ();

  if (!output.empty()) {
   bfs::path savefile = output;
    {
      bfs::ofstream sf (savefile);
      ilog("Writing network config output to: ${output}", ("output", savefile.string()));
      sf << fc::json::to_pretty_string (network) << std::endl;
      sf.close();
    }
    if (host_map_file.empty()) {
      savefile = bfs::path (output.stem().string() + "_hosts.json");
    }
    else {
      savefile = bfs::path (host_map_file);
    }

    {
      bfs::ofstream sf (savefile);
      ilog("Writing host map file to: ${host_map}", ("host_map", savefile.string()));
      sf << fc::json::to_pretty_string (bindings) << std::endl;
      sf.close();
    }
     return false;
  }
  return true;
}

void
launcher_def::write_dot_file () {
  bfs::ofstream df ("testnet.dot");
  df << "digraph G\n{\nlayout=\"circo\";\n";
  for (auto &node : network.nodes) {
    for (const auto &p : node.second.peers) {
      std::string pname=network.nodes.find(p)->second.instance->dot_label();
      df << "\"" << node.second.instance->dot_label ()
         << "\"->\"" << pname
         << "\" [dir=\"forward\"];" << std::endl;
    }
  }
  df << "}\n";
}

void
launcher_def::define_network () {

  if (per_host == 0) {
    host_def local_host;
    local_host.set_local_id(id_ptr);
    local_host.eosio_home = erd;
    local_host.genesis = genesis.string();
    for (size_t i = 0; i < (total_nodes); i++) {
      eosd_def eosd;
      assign_name(eosd, i == 0);
      aliases.push_back(eosd.name);
      eosd.set_host (&local_host, i == 0);
      local_host.instances.emplace_back(std::move(eosd));
    }
    bindings.emplace_back(std::move(local_host));
  }
  else {
    int ph_count = 0;
    host_def *lhost = nullptr;
    size_t host_ndx = 0;
    size_t num_prod_addr = servers.producer.size();
    size_t num_nonprod_addr = servers.nonprod.size();
    for (size_t i = total_nodes; i > 0; i--) {
       bool do_bios = false;
      if (ph_count == 0) {
        if (lhost) {
          bindings.emplace_back(std::move(*lhost));
          delete lhost;
        }
        lhost = new host_def;
        lhost->set_local_id(id_ptr);
        lhost->genesis = genesis.string();
        if (host_ndx < num_prod_addr ) {
           do_bios = servers.producer[host_ndx].has_bios;
          lhost->host_name = servers.producer[host_ndx].ipaddr;
          lhost->public_name = servers.producer[host_ndx].name;
          ph_count = servers.producer[host_ndx].instances;
        }
        else if (host_ndx - num_prod_addr < num_nonprod_addr) {
          size_t ondx = host_ndx - num_prod_addr;
           do_bios = servers.nonprod[ondx].has_bios;
          lhost->host_name = servers.nonprod[ondx].ipaddr;
          lhost->public_name = servers.nonprod[ondx].name;
          ph_count = servers.nonprod[ondx].instances;
        }
        else {
          std::string ext = host_ndx < 10 ? "0" : "";
          ext += boost::lexical_cast<std::string,int>(host_ndx);
          lhost->host_name = "pseudo_" + ext;
          lhost->public_name = lhost->host_name;
          ph_count = 1;
        }
        lhost->eosio_home =
          (id_ptr->contains (lhost->host_name) || servers.default_eosio_home.empty()) ?
          erd : servers.default_eosio_home;
        host_ndx++;
      } // ph_count == 0

      eosd_def eosd;
      assign_name(eosd, do_bios);
      eosd.has_db = false;

      if (servers.db.size()) {
        for (auto &dbn : servers.db) {
          if (lhost->host_name == dbn) {
            eosd.has_db = true;
            break;
         }
        }
      }
      aliases.push_back(eosd.name);
      eosd.set_host (lhost, do_bios);
      do_bios = false;
      lhost->instances.emplace_back(std::move(eosd));
      --ph_count;
    } // for i
    bindings.emplace_back(std::move(*lhost) );
    delete lhost;
  }
}


void
launcher_def::bind_nodes () {
   if (prod_nodes < 2) {
      elog("Unable to allocate producers due to insufficient prod_nodes = ${num_nodes}", ("num_nodes", prod_nodes));
      exit (10);
   }
   int non_bios = prod_nodes - 1;
   int per_node = producers / non_bios;
   int extra = producers % non_bios;
   producer_set.version = 1;
   unsigned int i = 0;
   for (auto &h : bindings) {
      for (auto &inst : h.instances) {
         bool is_bios = inst.name == "bios";
         tn_node_def node;
         node.name = inst.name;
         node.instance = &inst;
         auto kp = is_bios ?
            private_key_type(std::string("5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3")) :
            private_key_type::generate();
         auto pubkey = kp.get_public_key();
         node.keys.emplace_back (std::move(kp));
         if (is_bios) {
            std::string prodname = "eosio";
            node.producers.push_back(prodname);
            producer_set.producers.push_back({prodname,pubkey});
         }
        else {
           if (i < non_bios) {
              int count = per_node;
              if (extra ) {
                 ++count;
                 --extra;
              }
              char ext = 'a' + i;
              std::string pname = "init";
              while (count--) {
                 std::string prodname = pname+ext;
                 node.producers.push_back(prodname);
                 producer_set.producers.push_back({prodname,pubkey});
                 ext += non_bios;
              }
           }
        }
        node.gelf_endpoint = gelf_endpoint;
        network.nodes[node.name] = std::move(node);
        inst.node = &network.nodes[inst.name];
        if (!is_bios) i++;
      }
  }
}

host_def *
launcher_def::find_host (const std::string &name)
{
  host_def *host = nullptr;
  for (auto &h : bindings) {
    if (h.host_name == name) {
      host = &h;
      break;
    }
  }
  if (host == 0) {
    elog("Could not find host for ${name}", ("name", name));
    exit(-1);
  }
  return host;
}

host_def *
launcher_def::find_host_by_name_or_address (const std::string &host_id)
{
  host_def *host = nullptr;
  for (auto &h : bindings) {
    if ((h.host_name == host_id) || (h.public_name == host_id)) {
      host = &h;
      break;
    }
  }
  if (host == 0) {
    elog("Could not find host for ${host_id}", ("host_id", host_id));
    exit(-1);
  }
  return host;
}

host_def *
launcher_def::deploy_config_files (tn_node_def &node) {
  boost::system::error_code ec;
  eosd_def &instance = *node.instance;
  host_def *host = find_host (instance.host);

  bfs::path source = stage / instance.config_dir_name / "config.ini";
  bfs::path logging_source = stage / instance.config_dir_name / "logging.json";
  bfs::path genesis_source = stage / instance.config_dir_name / "genesis.json";

  if (host->is_local()) {
    bfs::path cfgdir = bfs::path(host->eosio_home) / instance.config_dir_name;
    bfs::path dd = bfs::path(host->eosio_home) / instance.data_dir_name;

    if (!bfs::exists (cfgdir)) {
       if (!bfs::create_directories (cfgdir, ec) && ec.value()) {
	  elog("Could not create new directory: ${dir} ErrNo: ${errno} ${errstr}",
	       ("dir", instance.config_dir_name)
	       ("errno", ec.value())
	       ("errstr", strerror(ec.value())));
          exit (-1);
       }
    }
    else if (bfs::exists (cfgdir / "config.ini") && !force_overwrite) {
       elog("${dir}/config.ini exists. Use -f|--force to overwrite configuration.", ("dir", cfgdir.string()));
       exit (-1);
    }

    if (!bfs::exists (dd)) {
       if (!bfs::create_directories (dd, ec) && ec.value()) {
	  elog("Could not create new directory: ${dir} ${errno} ${errstr}",
	       ("dir", instance.config_dir_name)
	       ("errno", ec.value())
	       ("errstr", strerror(ec.value())));
          exit (-1);
       }
    }
    else if (force_overwrite) {
       int64_t count =  bfs::remove_all (dd / BLOCK_DIR, ec);
       if (ec.value() != 0) {
	  elog("count = ${count} could not remove old directory: ${dir}/${block_dir} ${errstr}",
	       ("count", count)
	       ("dir", dd.string())
	       ("block_dir", BLOCK_DIR)
	       ("errstr", strerror(ec.value())));
          exit (-1);
        }
        count = bfs::remove_all (dd / SHARED_MEM_DIR, ec);
        if (ec.value() != 0) {
            elog("count = ${count} could not remove old directory: ${dir}/${mem_dir} ${errstr}",
            	       ("count", count)
            	       ("dir", dd.string())
            	       ("mem_dir", SHARED_MEM_DIR)
            	       ("errstr", strerror(ec.value())));
          exit (-1);
        }
    }
    else if (bfs::exists (dd/ BLOCK_DIR) || bfs::exists (dd / SHARED_MEM_DIR)) {
       elog("Either ${block_dir} or ${mem_dir} exist in ${dir}. Use -f|--force to erase blockchain data.",
	    ("block_dir", BLOCK_DIR)
	    ("mem_dir", SHARED_MEM_DIR)
	    ("dir", dd.string()));
        exit (-1);
    }

    bfs::copy_file (genesis_source, cfgdir / "genesis.json", bfs::copy_option::overwrite_if_exists);
    bfs::copy_file (logging_source, cfgdir / "logging.json", bfs::copy_option::overwrite_if_exists);
    bfs::copy_file (source, cfgdir / "config.ini", bfs::copy_option::overwrite_if_exists);
  }
  else {
    prep_remote_config_dir (instance, host);

    bfs::path rfile = bfs::path (host->eosio_home) / instance.config_dir_name / "config.ini";
    auto scp_cmd_line = compose_scp_command(*host, source, rfile);

    ilog("cmdline = ${scp_cmd}", ("scp_cmd", scp_cmd_line));

    int res = boost::process::system(scp_cmd_line);
    if (res != 0) {
      elog("Unable to scp config file to host: ${host_name}", ("host_name", host->host_name));
      exit(-1);
    }

    rfile = bfs::path (host->eosio_home) / instance.config_dir_name / "logging.json";

    scp_cmd_line = compose_scp_command(*host, logging_source, rfile);

    res = boost::process::system (scp_cmd_line);
    if (res != 0) {
      elog("Unable to scp logging config file to host: ${host_name}", ("host_name", host->host_name));
      exit(-1);
    }

    rfile = bfs::path (host->eosio_home) / instance.config_dir_name / "genesis.json";

    scp_cmd_line = compose_scp_command(*host, genesis_source, rfile);

    res = boost::process::system (scp_cmd_line);
    if (res != 0) {
       elog("Unable to scp genesis.json file to host: ${host_name}", ("host_name", host->host_name));
       exit(-1);
    }
  }
  return host;
}

std::string launcher_def::compose_scp_command (const host_def& host, const bfs::path& source, const bfs::path& destination) {
  std::string scp_cmd_line = network.ssh_helper.scp_cmd + " ";
  const std::string &args = host.ssh_args.length() ? host.ssh_args : network.ssh_helper.ssh_args;
  if (args.length()) {
    scp_cmd_line += args + " ";
  }
  scp_cmd_line += source.string() + " ";

  const std::string &uid = host.ssh_identity.length() ? host.ssh_identity : network.ssh_helper.ssh_identity;
  if (uid.length()) {
    scp_cmd_line += uid + "@";
  }

  scp_cmd_line += host.host_name + ":" + destination.string();

  return scp_cmd_line;
}

void
launcher_def::write_config_file (tn_node_def &node) {
   bool is_bios = (node.name == "bios");
   bfs::path filename;
   eosd_def &instance = *node.instance;
   host_def *host = find_host (instance.host);

   bfs::path dd = stage / instance.config_dir_name;
   if (!bfs::exists(dd)) {
      try {
         bfs::create_directories (dd);
      } catch (const bfs::filesystem_error &ex) {
	 elog("write_config_files threw error: ${errstr}", ("errstr", ex.what()));
         exit (-1);
      }
   }

   filename = dd / "config.ini";

   bfs::ofstream cfg(filename);
   if (!cfg.good()) {
      elog("Unable to open ${filename} ${errstr}", ("filename", filename.string())("errstr", strerror(errno)));
      exit (-1);
   }

   cfg << "genesis-json = " << host->genesis << "\n";
   cfg << "block-log-dir = " << BLOCK_DIR << "\n";
   cfg << "readonly = 0\n";
   cfg << "send-whole-blocks = true\n";
   cfg << "http-server-address = " << host->host_name << ":" << instance.http_port << "\n";
   cfg << "p2p-listen-endpoint = " << host->listen_addr << ":" << instance.p2p_port << "\n";
   cfg << "p2p-server-address = " << host->public_name << ":" << instance.p2p_port << "\n";

   if (is_bios) {
    cfg << "enable-stale-production = true\n";
  }
  if (allowed_connections & PC_ANY) {
    cfg << "allowed-connection = any\n";
  }
  else if (allowed_connections == PC_NONE) {
    cfg << "allowed-connection = none\n";
  }
  else
  {
    if (allowed_connections & PC_PRODUCERS) {
      cfg << "allowed-connection = producers\n";
    }
    if (allowed_connections & PC_SPECIFIED) {
      cfg << "allowed-connection = specified\n";
      cfg << "peer-key = \"" << std::string(node.keys.begin()->get_public_key()) << "\"\n";
      cfg << "peer-private-key = [\"" << std::string(node.keys.begin()->get_public_key())
          << "\",\"" << std::string(*node.keys.begin()) << "\"]\n";
    }
  }

  if(!is_bios) {
     auto &bios_node = network.nodes["bios"];
     cfg << "p2p-peer-address = " << bios_node.instance->p2p_endpoint<< "\n";
  }
  for (const auto &p : node.peers) {
    cfg << "p2p-peer-address = " << network.nodes.find(p)->second.instance->p2p_endpoint << "\n";
  }
  if (instance.has_db || node.producers.size()) {
    cfg << "required-participation = 33\n";
    for (const auto &kp : node.keys ) {
       cfg << "private-key = [\"" << std::string(kp.get_public_key())
           << "\",\"" << std::string(kp) << "\"]\n";
    }
    for (auto &p : node.producers) {
      cfg << "producer-name = " << p << "\n";
    }
    cfg << "plugin = eosio::producer_plugin\n";
  }
  if( instance.has_db ) {
    cfg << "plugin = eosio::mongo_db_plugin\n";
  }
  cfg << "plugin = eosio::chain_api_plugin\n"
      << "plugin = eosio::account_history_api_plugin\n";
  cfg.close();
}

void
launcher_def::write_logging_config_file(tn_node_def &node) {
  bfs::path filename;
  eosd_def &instance = *node.instance;

  bfs::path dd = stage / instance.config_dir_name;
  if (!bfs::exists(dd)) {
    bfs::create_directories(dd);
  }

  filename = dd / "logging.json";

  bfs::ofstream cfg(filename);
  if (!cfg.good()) {
    elog("Unable to open ${filename} ${errstr}", ("filename", filename.string())("errstr", strerror(errno)));
    exit (9);
  }

  auto log_config = fc::logging_config::default_config();
  if(gelf_enabled) {
    log_config.appenders.push_back(
          fc::appender_config( "net", "gelf",
              fc::mutable_variant_object()
                  ( "endpoint", node.gelf_endpoint )
                  ( "host", instance.name )
             ) );
    log_config.loggers.front().appenders.push_back("net");
  }

  auto str = fc::json::to_pretty_string( log_config, fc::json::stringify_large_ints_and_doubles );
  cfg.write( str.c_str(), str.size() );
  cfg.close();
}

void
launcher_def::init_genesis () {
  bfs::path genesis_path = bfs::current_path() / "genesis.json";
   bfs::ifstream src(genesis_path);
   if (!src.good()) {
      ilog("Generating default genesis file: ${genesis_path}", ("genesis_path", genesis_path.string()));
      eosio::chain::contracts::genesis_state_type default_genesis;
      fc::json::save_to_file( default_genesis, genesis_path, true );
      src.open(genesis_path);
   }
   std::string bioskey = std::string(network.nodes["bios"].keys[0].get_public_key());
   std::string str;
   std::string prefix("initial_key");
   while(getline(src,str)) {
      size_t pos = str.find(prefix);
      if (pos != std::string::npos) {
         size_t cut = str.find("EOS",pos);
         genesis_block.push_back(str.substr(0,cut) + bioskey + "\",");
      }
      else {
         genesis_block.push_back(str);
      }
   }
}

void
launcher_def::write_genesis_file(tn_node_def &node) {
  bfs::path filename;
  eosd_def &instance = *node.instance;

  bfs::path dd = stage / instance.config_dir_name;
  if (!bfs::exists(dd)) {
    bfs::create_directories(dd);
  }

  filename = dd / "genesis.json";
  bfs::ofstream gf ( dd / "genesis.json");
  for (auto &line : genesis_block) {
     gf << line << "\n";
  }
}

void
launcher_def::write_setprods_file() {
   bfs::path filename = bfs::current_path() / "setprods.json";
   bfs::ofstream psfile (filename);
   if(!psfile.good()) {
      elog("Unable to open ${filename} ErrorNo: ${errno}", ("filename", filename.string())("errno", strerror(errno)));
    exit (9);
  }
   producer_set_def no_bios;
   for (auto &p : producer_set.producers) {
      if (p.producer_name != "eosio")
         no_bios.producers.push_back(p);
   }
   no_bios.version = 1;
  auto str = fc::json::to_pretty_string( no_bios, fc::json::stringify_large_ints_and_doubles );
  psfile.write( str.c_str(), str.size() );
  psfile.close();
}

void
launcher_def::write_bios_boot () {
   bfs::ifstream src(bfs::path(config_dir_base) / "launcher" / start_temp);
   if(!src.good()) {
      elog("Unable to open ${config_base}/launcher/${template_file} ErrorNo: ${errno}",
	   ("config_base", config_dir_base.string())
	   ("template_file", start_temp)
	   ("errno", strerror(errno)));
    exit (9);
  }

   bfs::ofstream brb (bfs::current_path() / start_script);
   if(!brb.good()) {
      elog("Unable to open ${current_path}/${start_script} ErrorNo: ${errno}",
	   ("current_path", bfs::current_path().string())
	   ("start_script", start_script)
	   ("errno", strerror(errno)));
    exit (9);
  }

   auto &bios_node = network.nodes["bios"];
   uint16_t biosport = bios_node.instance->http_port;
   std::string bhost = bios_node.instance->host;
   std::string line;
   std::string prefix = "###INSERT ";
   size_t len = prefix.length();
   while (getline(src,line)) {
      if (line.substr(0,len) == prefix) {
         std::string key = line.substr(len);
         if (key == "envars") {
            brb << "bioshost=" << bhost << "\nbiosport=" << biosport << "\n";
         }
         else if (key == "prodkeys" ) {
            for (auto &node : network.nodes) {
               brb << "wcmd import -n ignition " << std::string(node.second.keys[0]) << "\n";
            }
         }
         else if (key == "cacmd") {
            for (auto &p : producer_set.producers) {
               if (p.producer_name == "eosio") {
                  continue;
               }
               brb << "cacmd " << p.producer_name
                   << " " << std::string(p.block_signing_key) << " " << std::string(p.block_signing_key) << "\n";
            }
         }
      }
      brb << line << "\n";
   }
   src.close();
   brb.close();
}

bool launcher_def::is_bios_ndx (size_t ndx) {
   return aliases[ndx] == "bios";
}

size_t launcher_def::start_ndx() {
   return is_bios_ndx(0) ? 1 : 0;
}

bool launcher_def::next_ndx(size_t &ndx) {
   ++ndx;
   bool loop = ndx == total_nodes;
   if (loop)
      ndx = start_ndx();
   else
      if (is_bios_ndx(ndx)) {
         loop = next_ndx(ndx);
      }
   return loop;
}

size_t launcher_def::skip_ndx (size_t from, size_t offset) {
   size_t ndx = (from + offset) % total_nodes;
   if (total_nodes > 2) {
      size_t attempts = total_nodes - 1;
      while (--attempts && (is_bios_ndx(ndx) || ndx == from)) {
         next_ndx(ndx);
      }
   }
   return ndx;
}

void
launcher_def::make_ring () {
  bind_nodes();
  size_t non_bios = total_nodes - 1;
  if (non_bios > 2) {
     bool loop = false;
     for (size_t i = start_ndx(); !loop; loop = next_ndx(i)) {
        size_t front = i;
        loop = next_ndx (front);
        network.nodes.find(aliases[i])->second.peers.push_back (aliases[front]);
     }
  }
  else if (non_bios == 2) {
     size_t n0 = start_ndx();
     size_t n1 = n0;
     next_ndx(n1);
    network.nodes.find(aliases[n0])->second.peers.push_back (aliases[n1]);
    network.nodes.find(aliases[n1])->second.peers.push_back (aliases[n0]);
  }
}

void
launcher_def::make_star () {
  size_t non_bios = total_nodes - 1;
  if (non_bios < 4) {
    make_ring ();
    return;
  }
  bind_nodes();

  size_t links = 3;
  if (non_bios > 12) {
    links = static_cast<size_t>(sqrt(non_bios)) + 2;
  }
  size_t gap = non_bios > 6 ? 3 : (non_bios - links)/2 +1;
  while (non_bios % gap == 0) {
    ++gap;
  }
  // use to prevent duplicates since all connections are bidirectional
  std::map <std::string, std::set<std::string>> peers_to_from;
  bool loop = false;
  for (size_t i = start_ndx(); !loop; loop = next_ndx(i)) {
     const auto& iter = network.nodes.find(aliases[i]);
    auto &current = iter->second;
    const auto& current_name = iter->first;
    size_t ndx = i;
    for (size_t l = 1; l <= links; l++) {
       ndx = skip_ndx(ndx, l * gap);
       auto &peer = aliases[ndx];
      for (bool found = true; found; ) {
        found = false;
        for (auto &p : current.peers) {
          if (p == peer) {
             next_ndx(ndx);
             if (ndx == i) {
                next_ndx(ndx);
             }
            peer = aliases[ndx];
            found = true;
            break;
          }
        }
      }
      // if already established, don't add to list
      if (peers_to_from[peer].count(current_name) < 2) {
        current.peers.push_back(peer); // current_name -> peer
        // keep track of bidirectional relationships to prevent duplicates
        peers_to_from[current_name].insert(peer);
      }
    }
  }
}

void
launcher_def::make_mesh () {
  size_t non_bios = total_nodes - 1;
  bind_nodes();
  // use to prevent duplicates since all connections are bidirectional
  std::map <std::string, std::set<std::string>> peers_to_from;
  bool loop = false;
  for (size_t i = start_ndx();!loop; loop = next_ndx(i)) {
    const auto& iter = network.nodes.find(aliases[i]);
    auto &current = iter->second;
    const auto& current_name = iter->first;

    for (size_t j = 1; j < non_bios; j++) {
       size_t ndx = skip_ndx(i,j);
      const auto& peer = aliases[ndx];
      // if already established, don't add to list
      if (peers_to_from[peer].count(current_name) < 2) {
        current.peers.push_back (peer);
        // keep track of bidirectional relationships to prevent duplicates
        peers_to_from[current_name].insert(peer);
      }
    }
  }
}

void
launcher_def::make_custom () {
  bfs::path source = shape;
  fc::json::from_file(source).as<testnet_def>(network);
  producer_set.version = 1;
  for (auto &h : bindings) {
    for (auto &inst : h.instances) {
      tn_node_def *node = &network.nodes[inst.name];
      for (auto &p : node->producers) {
         producer_set.producers.push_back({p,node->keys[0].get_public_key()});
      }
      node->instance = &inst;
      inst.node = node;
    }
  }
}

void
launcher_def::format_ssh (const std::string &cmd,
                          const std::string &host_name,
                          std::string & ssh_cmd_line) {

  ssh_cmd_line = network.ssh_helper.ssh_cmd + " ";
  if (network.ssh_helper.ssh_args.length()) {
    ssh_cmd_line += network.ssh_helper.ssh_args + " ";
  }
  if (network.ssh_helper.ssh_identity.length()) {
    ssh_cmd_line += network.ssh_helper.ssh_identity + "@";
  }
  ssh_cmd_line += host_name + " \"" + cmd + "\"";
  elog("cmdline =  ${ssh_cmd}", ("ssh_cmd", ssh_cmd_line));
}

bool
launcher_def::do_ssh (const std::string &cmd, const std::string &host_name) {
  std::string ssh_cmd_line;
  format_ssh (cmd, host_name, ssh_cmd_line);
  int res = boost::process::system (ssh_cmd_line);
  return (res == 0);
}

void
launcher_def::prep_remote_config_dir (eosd_def &node, host_def *host) {
  bfs::path abs_config_dir = bfs::path(host->eosio_home) / node.config_dir_name;
  bfs::path abs_data_dir = bfs::path(host->eosio_home) / node.data_dir_name;

  std::string acd = abs_config_dir.string();
  std::string add = abs_data_dir.string();
  std::string cmd = "cd " + host->eosio_home;

  cmd = "cd " + host->eosio_home;
  if (!do_ssh(cmd, host->host_name)) {
    elog("Unable to switch to path: ${eosio_home} on host ${host_name}",
	 ("eosio_home", host->eosio_home)
	 ("host_name", host->host_name));
    exit (-1);
  }

  cmd = "cd " + acd;
  if (!do_ssh(cmd,host->host_name)) {
     cmd = "mkdir -p " + acd;
     if (!do_ssh (cmd, host->host_name)) {
	elog("Unable to invoke ${cmd} on host ${host_name}",
	     ("cmd", cmd)
	     ("host_name", host->host_name));
        exit (01);
     }
  }
  cmd = "cd " + add;
  if (do_ssh(cmd,host->host_name)) {
    if(force_overwrite) {
      cmd = "rm -rf " + add + "/" + BLOCK_DIR + " ;"
          + "rm -rf " + add + "/" + SHARED_MEM_DIR;
      if (!do_ssh (cmd, host->host_name)) {
	elog("Unable to remove old data directories on host ${host_name}",
	     ("host_name", host->host_name));
        exit (-1);
      }
    }
    else {
      elog("${add} already exists on host ${host_name}. Use -f/--force to overwrite configuration and erase blockchain.",
	   ("host_name", host->host_name));
      exit (-1);
    }
  }
  else {
    cmd = "mkdir -p " + add;
    if (!do_ssh (cmd, host->host_name)) {
      elog("Unable to invoke ${cmd} on host ${host_name}",
	   ("cmd", cmd)
	   ("host_name", host->host_name));
      exit (-1);
    }
  }
}

void
launcher_def::launch (eosd_def &instance, std::string &gts) {
  bfs::path dd = instance.data_dir_name;
  bfs::path reout = dd / "stdout.txt";
  bfs::path reerr_sl = dd / "stderr.txt";
  bfs::path reerr_base = bfs::path("stderr." + launch_time + ".txt");
  bfs::path reerr = dd / reerr_base;
  bfs::path pidf  = dd / "nodeos.pid";
  host_def* host;
  try {
     host = deploy_config_files (*instance.node);
  } catch (const bfs::filesystem_error &ex) {
     elog("deploy_config_files threw: ${errmsg}", ("errmsg", ex.what()));
     exit (-1);
  }

  node_rt_info info;
  info.remote = !host->is_local();

  std::string eosdcmd = "programs/nodeos/nodeos ";
  if (skip_transaction_signatures) {
    eosdcmd += "--skip-transaction-signatures ";
  }
  if (!eosd_extra_args.empty()) {
    if (instance.name == "bios") {
       // Strip the mongo-related options out of the bios node so
       // the plugins don't conflict between 00 and bios.
       std::regex r("--plugin +eosio::mongo_db_plugin");
       std::string args = std::regex_replace (eosd_extra_args,r,"");
       std::regex r2("--mongodb-uri +[^ ]+");
       args = std::regex_replace (args,r2,"");
       eosdcmd += args + " ";
    }
    else {
       eosdcmd += eosd_extra_args + " ";
    }
  }

  if( add_enable_stale_production ) {
    eosdcmd += "--enable-stale-production true ";
    add_enable_stale_production = false;
  }

  eosdcmd += " --config-dir " + instance.config_dir_name + " --data-dir " + instance.data_dir_name;
  if (gts.length()) {
    eosdcmd += " --genesis-timestamp " + gts;
  }

  if (!host->is_local()) {
    std::string cmdl ("cd ");
    cmdl += host->eosio_home + "; nohup " + eosdcmd + " > "
      + reout.string() + " 2> " + reerr.string() + "& echo $! > " + pidf.string()
      + "; rm -f " + reerr_sl.string()
      + "; ln -s " + reerr_base.string() + " " + reerr_sl.string();
    if (!do_ssh (cmdl, host->host_name)){
      elog("Unable to invoke ${cmdl} on host ${host_name}", ("cmdl", cmdl)("host_name", host->host_name));
      exit (-1);
    }

    std::string cmd = "cd " + host->eosio_home + "; kill -15 $(cat " + pidf.string() + ")";
    format_ssh (cmd, host->host_name, info.kill_cmd);
  }
  else {
    elog("Spawning child: ${eosdcmd}", ("eosdcmd", eosdcmd));

    bp::child c(eosdcmd, bp::std_out > reout, bp::std_err > reerr );
    bfs::remove(reerr_sl);
    bfs::create_symlink (reerr_base, reerr_sl);

    bfs::ofstream pidout (pidf);
    pidout << c.id() << std::flush;
    pidout.close();

    info.pid_file = pidf.string();
    info.kill_cmd = "";

    if(!c.running()) {
      elog("Child no running after spawn: ${eosdcmd}", ("eosdcmd", eosdcmd));
      for (int i = 0; i > 0; i++) {
        if (c.running () ) break;
      }
    }
    c.detach();

  }
  last_run.running_nodes.emplace_back (std::move(info));
}

#if 0
void
launcher_def::kill_instance(eosd_def, string sig_opt) {
}
#endif

void
launcher_def::kill (launch_modes mode, std::string sig_opt) {
  switch (mode) {
  case LM_NONE:
    return;
  case LM_VERIFY:
    // no-op
    return;
  case LM_NAMED: {
    elog("Feature not yet implemented.");
    #if 0
      auto node = network.nodes.find(launch_name);
      kill_instance (node.second.instance, sig_opt);
      #endif
    break;
  }
  case LM_ALL:
  case LM_LOCAL:
  case LM_REMOTE : {
    bfs::path source = "last_run.json";
    fc::json::from_file(source).as<last_run_def>(last_run);
    for (auto &info : last_run.running_nodes) {
      if (mode == LM_ALL || (info.remote && mode == LM_REMOTE) ||
          (!info.remote && mode == LM_LOCAL)) {
        if (info.pid_file.length()) {
          std::string pid;
          fc::json::from_file(info.pid_file).as<std::string>(pid);
          std::string kill_cmd = "kill " + sig_opt + " " + pid;
          boost::process::system (kill_cmd);
        }
        else {
          boost::process::system (info.kill_cmd);
        }
      }
    }
  }
  }
}

std::pair<host_def, eosd_def>
launcher_def::find_node(uint16_t node_num) {
   std::string dex = node_num < 10 ? "0":"";
   dex += boost::lexical_cast<std::string,uint16_t>(node_num);
   std::string node_name = network.name + dex;
   for (const auto& host: bindings) {
      for (const auto& node: host.instances) {
         if (node_name == node.name) {
            return std::make_pair(host, node);
         }
      }
   }
   elog("Unable to find node: ${node_num}", ("node_num", node_num));
   exit (-1);
}

std::vector<std::pair<host_def, eosd_def>>
launcher_def::get_nodes(const std::string& node_number_list) {
   std::vector<std::pair<host_def, eosd_def>> node_list;
   if (fc::to_lower(node_number_list) == "all") {
      for (auto host: bindings) {
         for (auto node: host.instances) {
            ilog("host=${host_name}, node=${node_name}", ("host_name", host.host_name)("node_name", node.name));
            node_list.push_back(std::make_pair(host, node));
         }
      }
   }
   else {
      std::vector<std::string> nodes;
      boost::split(nodes, node_number_list, boost::is_any_of(","));
      for (std::string node_number: nodes) {
         uint16_t node = -1;
         try {
            node = boost::lexical_cast<uint16_t,std::string>(node_number);
         }
         catch(boost::bad_lexical_cast &) {
            // This exception will be handled below
         }
         if (node < 0 || node > 99) {
            elog("Bad node number found in node number list: ${node_num}", ("node_num", node_number));
            exit(-1);
         }
         node_list.push_back(find_node(node));
      }
   }
   return node_list;
}

void
launcher_def::bounce (const std::string& node_numbers) {
   auto node_list = get_nodes(node_numbers);
   for (auto node_pair: node_list) {
      const host_def& host = node_pair.first;
      const eosd_def& node = node_pair.second;
      std::string node_num = node.name.substr( node.name.length() - 2 );
      std::string cmd = "cd " + host.eosio_home + "; "
                 + "export EOSIO_HOME=" + host.eosio_home + std::string("; ")
                 + "export EOSIO_TN_NODE=" + node_num + "; "
                 + "./scripts/eosio-tn_bounce.sh";
      ilog("Bouncing ${node_name}", ("node_name", node.name));
      if (!do_ssh(cmd, host.host_name)) {
	 elog("Unable to bounce ${node_name}", ("node_name", node.name));
         exit (-1);
      }
   }
}

void
launcher_def::down (const std::string& node_numbers) {
   auto node_list = get_nodes(node_numbers);
   for (auto node_pair: node_list) {
      const host_def& host = node_pair.first;
      const eosd_def& node = node_pair.second;
      std::string node_num = node.name.substr( node.name.length() - 2 );
      std::string cmd = "cd " + host.eosio_home + "; "
                 + "export EOSIO_HOME=" + host.eosio_home + "; "
                 + "export EOSIO_TN_NODE=" + node_num + "; "
         + "export EOSIO_TN_RESTART_CONFIG_DIR=" + node.config_dir_name + "; "
                 + "./scripts/eosio-tn_down.sh";
      ilog("Taking down ${node_name}", ("node_name", node.name));
      if (!do_ssh(cmd, host.host_name)) {
	 elog("Unable to down ${node_name}", ("node_name", node.name));
         exit (-1);
      }
   }
}

void
launcher_def::roll (const std::string& host_names) {
   std::vector<std::string> hosts;
   boost::split(hosts, host_names, boost::is_any_of(","));
   for (std::string host_name: hosts) {
      ilog("Rolling ${host_name}", ("host_name", host_name));
      auto host = find_host_by_name_or_address(host_name);
      std::string cmd = "cd " + host->eosio_home + "; "
                 + "export EOSIO_HOME=" + host->eosio_home + "; "
                 + "./scripts/eosio-tn_roll.sh";
      if (!do_ssh(cmd, host_name)) {
	 elog("Unable to roll ${host_name}", ("host_name", host->host_name));
         exit (-1);
      }
   }
}

void
launcher_def::ignite() {

   if (boot) {
      elog("Invoking the blockchain boot script, ${start_script}", ("start_script", start_script));
      std::string script("bash " + start_script);

      bp::child c(script);
      try {
         boost::system::error_code ec;
         elog("Waiting for script completion.");
         c.wait();
      } catch (bfs::filesystem_error &ex) {
	 elog("wait() threw error ${err}", ("err", ex.what()));
      }
      catch (...) {
         // when script dies wait throws an exception but that is ok
      }

   } else {
      std::cerr << "**********************************************************************\n"
           << "run 'bash " << start_script << "' to kick off delegated block production\n"
           << "**********************************************************************\n";
   }

}

void
launcher_def::start_all (std::string &gts, launch_modes mode) {

  ilog("starting network");

  switch (mode) {
  case LM_NONE:
    return;
  case LM_VERIFY:
    //validate configuration, report findings, exit
    return;
  case LM_NAMED : {
    try {
      add_enable_stale_production = false;
      auto node = network.nodes.find(launch_name);
      launch(*node->second.instance, gts);
    } catch (fc::exception& fce) {
	elog("Unable to launch ${launch_name} fc::exception=${fc_except}",
	     ("launch_name", launch_name)
	     ("fc_except", fce.to_detail_string()));
    } catch (std::exception& stde) {
	elog("Unable to launch ${launch_name} std::exception=${std_except}",
	     ("launch_name", launch_name)
	     ("std_except", stde.what()));
    } catch (...) {
	elog("Unable to launch ${launch_name} Unknown exception.", ("launch_name", launch_name));
      exit (-1);
    }
    break;
  }
  case LM_ALL:
  case LM_REMOTE:
  case LM_LOCAL: {

    for (auto &h : bindings ) {
      if (mode == LM_ALL ||
          (h.is_local() ? mode == LM_LOCAL : mode == LM_REMOTE)) {
        for (auto &inst : h.instances) {
          try {
              elog("Launching ${launch_name}", ("launch_name", inst.name));
             launch (inst, gts);
          } catch (fc::exception& fce) {
              elog("Unable to launch ${launch_name} fc::exception=${fc_except}",
              	     ("launch_name", inst.name)
              	     ("fc_except", fce.to_detail_string()));
          } catch (std::exception& stde) {
              elog("Unable to launch ${launch_name} std::exception=${std_except}",
              	     ("launch_name", inst.name)
              	     ("std_except", stde.what()));
          } catch (...) {
              elog("Unable to launch ${launch_name} Unknown exception.", ("launch_name", inst.name));
          }
          sleep (start_delay);
        }
      }
    }
    break;
  }
  }
  bfs::path savefile = "last_run.json";
  ilog("Writing last_run file to: ${save_file}", ("save_file", savefile.string()));
  bfs::ofstream sf (savefile);

  sf << fc::json::to_pretty_string (last_run) << std::endl;
  sf.close();
}

}
}
