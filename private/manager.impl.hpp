#include "manager.hpp"

#include <map>
#include <mutex>
#include <functional>

namespace loader
{
    struct manager::impl
    {
        static inline std::unique_ptr<manager> instance;

      private:
        using mock_callback = std::function<sol::object()>;
        using hook_callback = std::function<void(const sol::object &)>;

      public:
        std::unique_ptr<sol::state_view> lua;

      public:
        std::mutex init_mutex;
        std::vector<std::shared_ptr<mod>> mods;

      public:
        sol::table mod_api;
        sol::function require;

      public:
        std::map<std::string, mock_callback> mocks;
        std::multimap<std::string, hook_callback> hooks;

      public:
        void setup_panic() const;

      public:
        void setup_mods();
        void setup_hooks();
        void setup_logger();

      public:
        void load_mods();
    };
} // namespace loader