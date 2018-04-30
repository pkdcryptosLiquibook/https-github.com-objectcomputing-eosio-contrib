/*
 * main.cpp
 *
 */

#include <appbase/application.hpp>

#include <eosio/launcher_plugin/launcher_plugin.hpp>

#include <fc/log/logger_config.hpp>
#include <fc/log/appender.hpp>
#include <fc/exception/exception.hpp>
#include <fc/filesystem.hpp>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/exception/diagnostic_information.hpp>

#include "config.hpp"

using namespace appbase;
using namespace eosio;

namespace fc {
   std::unordered_map<std::string,appender::ptr>& get_appender_map();
}

namespace detail {

void configure_logging(const bfs::path& config_path)
{
   try {
      try {
         fc::configure_logging(config_path);
      } catch (...) {
         elog("Error reloading logging.json");
         throw;
      }
   } catch (const fc::exception& e) {
      elog("${e}", ("e",e.to_detail_string()));
   } catch (const boost::exception& e) {
      elog("${e}", ("e",boost::diagnostic_information(e)));
   } catch (const std::exception& e) {
      elog("${e}", ("e",e.what()));
   } catch (...) {
      // empty
   }
}

} // namespace detail

void logging_conf_loop()
{
   std::shared_ptr<boost::asio::signal_set> sighup_set(new boost::asio::signal_set(appbase::app().get_io_service(), SIGHUP));
   sighup_set->async_wait([sighup_set](const boost::system::error_code& err, int /*num*/) {
      if(!err)
      {
         ilog("Received HUP.  Reloading logging configuration.");
         auto config_path = appbase::app().get_logging_conf();
         if(fc::exists(config_path))
            ::detail::configure_logging(config_path);
         for(auto iter : fc::get_appender_map())
            iter.second->initialize(appbase::app().get_io_service());
         logging_conf_loop();
      }
   });
}

void initialize_logging()
{
   auto config_path = appbase::app().get_logging_conf();
   if(fc::exists(config_path))
     fc::configure_logging(config_path); // intentionally allowing exceptions to escape
   for(auto iter : fc::get_appender_map())
     iter.second->initialize(appbase::app().get_io_service());

   logging_conf_loop();
}

int main(int argc, char** argv)
{
   try {
      appbase::app().set_version(eosio::launcher::config::version);
      auto root = fc::app_path();
      appbase::app().set_default_data_dir(root / "eosio/launcher/data" );
      appbase::app().set_default_config_dir(root / "eosio/launcher/config" );
      if(!appbase::app().initialize<eosio::launcher_plugin>(argc, argv))
        return -1;
      initialize_logging();
      ilog("launch version ${ver}", ("ver", eosio::launcher::config::itoh(static_cast<uint32_t>(appbase::app().version()))));
      ilog("eosio root is ${root}", ("root", root.string()));
      // Call startup, then immediately shutdown in order to execute a single command
      // otherwise we'll sit and "hang" waiting for a signal to exit the app
      appbase::app().startup(); // execute plugin(s)
      appbase::app().shutdown(); // exit plugin(s)
   } catch (const fc::exception& e) {
      elog("${e}", ("e",e.to_detail_string()));
   } catch (const boost::exception& e) {
      elog("${e}", ("e",boost::diagnostic_information(e)));
   } catch (const std::exception& e) {
      elog("${e}", ("e",e.what()));
   } catch (...) {
      elog("unknown exception");
   }

   return 0;
}


