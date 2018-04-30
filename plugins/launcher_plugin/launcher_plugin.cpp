/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#include <eosio/launcher_plugin/launcher_plugin.hpp>

#include <eosio/launcher_plugin/launcher_defines.hpp>
#include <eosio/launcher_plugin/launcher_def.hpp>

#include <boost/algorithm/string.hpp>

namespace eosio {
   static appbase::abstract_plugin& _launcher_plugin = app().register_plugin<launcher_plugin>();

class launcher_plugin_impl {
public:
  launcher_plugin_impl()
  : kill_arg()
  , bounce_nodes()
  , down_nodes()
  , roll_nodes()
  , gts()
  , mode(launcher::LM_NONE)
  , launcher_ptr(new eosio::launcher::launcher_def()) {

  }

public:
  std::string kill_arg;
  std::string bounce_nodes;
  std::string down_nodes;
  std::string roll_nodes;
  std::string gts;
  launcher::launch_modes mode;

  std::unique_ptr<eosio::launcher::launcher_def> launcher_ptr;
};

launcher_plugin::launcher_plugin()
  : my(new launcher_plugin_impl())
  {}
launcher_plugin::~launcher_plugin(){}

void launcher_plugin::set_program_options(options_description& cli, options_description& cfg) {

  // config file options
  cfg.add_options()
      ("force,f", bpo::bool_switch()->default_value(false), "Force overwrite of existing configuration files and erase blockchain")
      ("nodes,n",bpo::value<size_t>()->default_value(1),"total number of nodes to configure and launch")
      ("pnodes,p",bpo::value<size_t>()->default_value(1),"number of nodes that contain one or more producers")
      ("producers",bpo::value<size_t>()->default_value(21),"total number of non-bios producer instances in this network")
      ("mode,m",bpo::value<std::vector<std::string>>()->multitoken()->default_value({"any"}, "any"),"connection mode, combination of \"any\", \"producers\", \"specified\", \"none\"")
      ("shape,s",bpo::value<std::string>()->default_value("star"),"network topology, use \"star\" \"mesh\" or give a filename for custom")
      ("genesis,g",bpo::value<bfs::path>()->default_value("./genesis.json"),"set the path to genesis.json")
      ("skip-signature", bpo::bool_switch()->default_value(false), "nodeos does not require transaction signatures.")
      ("nodeos", bpo::value<std::string>(), "forward nodeos command line argument(s) to each instance of nodeos, enclose arg in quotes")
      ("delay,d",bpo::value<int>()->default_value(0),"seconds delay before starting each node after the first")
      ("boot",bpo::bool_switch()->default_value(false),"After deploying the nodes and generating a boot script, invoke it.")
      ("nogen",bpo::bool_switch()->default_value(false),"launch nodes without writing new config files")
      ("host-map",bpo::value<bfs::path>()->default_value(""),"a file containing mapping specific nodes to hosts. Used to enhance the custom shape argument")
      ("servers",bpo::value<bfs::path>()->default_value(""),"a file containing ip addresses and names of individual servers to deploy as producers or non-producers ")
      ("per-host",bpo::value<int>()->default_value(0),"specifies how many nodeos instances will run on a single host. Use 0 to indicate all on one.")
      ("network-name",bpo::value<std::string>()->default_value("testnet_"),"network name prefix used in GELF logging source")
      ("enable-gelf-logging",bpo::value<bool>()->default_value(true),"enable gelf logging appender in logging configuration file")
      ("gelf-endpoint",bpo::value<std::string>()->default_value("10.160.11.21:12201"),"hostname:port or ip:port of GELF endpoint")
      ("template",bpo::value<std::string>()->default_value("testnet.template"),"the startup script template")
      ("script",bpo::value<std::string>()->default_value("bios_boot.sh"),"the generated startup script name")
      ;

  // command line options
  cli.add_options()
      ("timestamp,i",bpo::value<std::string>(),"set the timestamp for the first block. Use \"now\" to indicate the current time")
      ("launch,l",bpo::value<std::string>(), "select a subset of nodes to launch. Currently may be \"all\", \"none\", or \"local\". If not set, the default is to launch all unless an output file is named, in which case it starts none.")
      ("output,o",bpo::value<bfs::path>(),"save a copy of the generated topology in this file")
      ("kill,k", bpo::value<std::string>(),"The launcher retrieves the previously started process ids and issues a kill to each.")
      ("down", bpo::value<std::string>(),"comma-separated list of node numbers that will be taken down using the eosio-tn_down.sh script")
      ("bounce", bpo::value<std::string>(),"comma-separated list of node numbers that will be restarted using the eosio-tn_bounce.sh script")
      ("roll", bpo::value<std::string>(),"comma-separated list of host names where the nodes should be rolled to a new version using the eosio-tn_roll.sh script")
      ;
}

void launcher_plugin::plugin_initialize(const variables_map& options) {
  ilog("launcher_plugin::plugin_initialize");

  boost::shared_ptr<eosio::launcher::local_identity> local_id_ptr(new eosio::launcher::local_identity);
  local_id_ptr->initialize();
  my->launcher_ptr->set_local_id(local_id_ptr);

  my->launcher_ptr->initialize(options);

  if(options.count("kill")) {
      my->kill_arg = options.at("kill").as<std::string>();
  }

  if(options.count("timestamp")) {
      my->gts = options.at("timestamp").as<std::string>();
  }

  if(options.count("down")) {
      my->down_nodes = options.at("down").as<std::string>();
  }

  if(options.count("bounce")) {
      my->bounce_nodes = options.at("bounce").as<std::string>();
  }

  if(options.count("roll")) {
    my->roll_nodes = options.at("roll").as<std::string>();
  }

  if (options.count("launch")) {
    std::string l = options["launch"].as<std::string>();
    if (boost::iequals(l,"all"))
      my->mode = launcher::LM_ALL;
    else if (boost::iequals(l,"local"))
      my->mode = launcher::LM_LOCAL;
    else if (boost::iequals(l,"remote"))
      my->mode = launcher::LM_REMOTE;
    else if (boost::iequals(l,"none"))
      my->mode = launcher::LM_NONE;
    else if (boost::iequals(l,"verify"))
      my->mode = launcher::LM_VERIFY;
    else {
      my->mode = launcher::LM_NAMED;
      my->launcher_ptr->get_launch_name() = l;
    }
  }
  else {
    my->mode = !my->kill_arg.empty() || my->launcher_ptr->get_output_dir().empty() ? launcher::LM_ALL : launcher::LM_NONE;
  }
}

void launcher_plugin::plugin_startup() {

  ilog("launcher_plugin::plugin_startup");

  if(!my->kill_arg.empty()) {
      if(my->kill_arg[0] != '-') {
	  my->kill_arg = "-" + my->kill_arg;
      }
      ilog("Killing: ${kill_arg}", ("kill_arg", my->kill_arg));
      my->launcher_ptr->kill(my->mode, my->kill_arg);
  }
  else if(!my->bounce_nodes.empty()) {
      my->launcher_ptr->bounce(my->bounce_nodes);
  }
  else if(!my->down_nodes.empty()) {
      my->launcher_ptr->down(my->down_nodes);
  }
  else if(!my->roll_nodes.empty()) {
      my->launcher_ptr->roll(my->roll_nodes);
  }
  else {
      my->launcher_ptr->generate();
      my->launcher_ptr->start_all(my->gts, my->mode);
      my->launcher_ptr->ignite();
  }
}

void launcher_plugin::plugin_shutdown() {
  ilog("launcher_plugin::plugin_shutdown");
}

}
