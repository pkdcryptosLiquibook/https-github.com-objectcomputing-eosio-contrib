/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#pragma once
#include <appbase/application.hpp>

namespace eosio {

namespace launcher {
  class launcher_def;
}

using namespace appbase;

/**
 *  This is a template plugin, intended to serve as a starting point for making new plugins
 */
class launcher_plugin : public appbase::plugin<launcher_plugin> {
public:
   launcher_plugin();
   virtual ~launcher_plugin();
 
   APPBASE_PLUGIN_REQUIRES()
   virtual void set_program_options(options_description& cli, options_description& cfg) override;
 
   void plugin_initialize(const variables_map& options);
   void plugin_startup();
   void plugin_shutdown();

private:
   std::unique_ptr<class launcher_plugin_impl> my;
};

}
