#include "manager.impl.hpp"
#include "logger.hpp"
#include "graph.hpp"

#include <filesystem>
#include <ranges>

namespace loader
{
    void manager::impl::setup_panic() const
    {
        auto panic = [](const std::optional<std::string> &message)
        {
            logger::get()->critical("lua panic: {}", message.value_or("<no message>"));
        };

        lua->set_panic(sol::c_call<decltype(panic), panic>);

        auto handler = [](lua_State *state, sol::optional<const std::exception &> exception, sol::string_view desc)
        {
            logger::get()->critical("exception ocurred: {}", desc);

            if (exception)
            {
                logger::get()->critical("what(): {}", exception->what());
            }

            return sol::stack::push(state, desc);
        };

        lua->set_exception_handler(handler);
    }

    void manager::impl::setup_mods()
    {
        auto dependencies = [](mod &mod)
        {
            return sol::as_table(mod.dependencies());
        };

        lua->new_usertype<mod>("mod",                                              //
                               "name", &mod::name,                                 //
                               "author", &mod::author,                             //
                                                                                   //
                               "version", &mod::version,                           //
                               "requires_restart", &mod::requires_restart,         //
                                                                                   //
                               "description", &mod::description,                   //
                               "detailed_description", &mod::detailed_description, //
                                                                                   //
                               "dependencies", dependencies,                       //
                                                                                   //
                               "enabled", &mod::enabled,                           //
                               "enable", &mod::enable                              //
        );

        auto mods = mod_api.create_named("mods");

        mods["all"] = []()
        {
            return sol::as_table(manager::get().mods());
        };

        mods["enabled"] = []()
        {
            return sol::as_table(manager::get().enabled());
        };
    }

    void manager::impl::setup_hooks()
    {
        auto table = mod_api.create_named("hooks");

        table["intercept_require"] = [this](const std::string &module, const sol::function &callback)
        {
            if (!hooks.contains(module))
            {
                logger::get()->debug("registering hook for \"{}\"", module);
            }

            auto it = hooks.emplace(module, callback.as<hook_callback>());

            const auto restore = [it, this]()
            {
                hooks.erase(it);
            };

            return std::make_unique<std::function<void()>>(restore);
        };

        table["mock_require"] = [this](const std::string &module, const sol::function &callback)
        {
            logger::get()->debug("registering mock for \"{}\"", module);

            if (mocks.contains(module))
            {
                logger::get()->warn("mock for \"{}\" already registered, overwriting", module);
            }

            mocks.emplace(module, callback.as<mock_callback>());

            const auto restore = [module, this]()
            {
                mocks.erase(module);
            };

            return std::make_unique<std::function<void()>>(restore);
        };

        lua->do_string(R"lua(
            mod_api.hooks.detour = function(table, func, callback)
                local original = table[func]

                table[func] = function(...)
                    return callback(original, ...)
                end

                return function()
                    table[func] = original
                end
            end
        )lua");

        auto require = lua->get<std::function<sol::object(const std::string &)>>("require");
        (*lua)["require"] = [this, require](const std::string &module)
        {
            if (module == "classes.battle.rules.onlineShouldBlockSkill")
            {
                manager::get().ready();
            }

            if (module.starts_with("!"))
            {
                return require(module.substr(1));
            }

            if (mocks.contains(module))
            {
                auto mock = mocks.at(module);
                return mock();
            }

            if (!hooks.contains(module))
            {
                return require(module);
            }

            logger::get()->debug("intercepting \"{}\"", module);

            auto rtn = require(module);

            using std::views::filter;
            auto callbacks = hooks | filter([&](const auto &x) { return x.first == module; });

            for (const auto &[_, callback] : callbacks)
            {
                callback(rtn);
            }

            return rtn;
        };
    }

    void manager::impl::setup_logger()
    {
        auto logger = mod_api.create_named("logger");

        lua->new_usertype<log_entry>("log_entry",                    //
                                     "message", &log_entry::message, //
                                     "level", &log_entry::level      //
        );

        const auto to_string = [this](const sol::variadic_args &args)
        {
            using std::views::transform;

            auto tostring = (*lua)["tostring"];
            auto transformed = args | transform([&](const auto &x) -> std::string { return tostring(x); });

            std::vector<std::string> strings{transformed.begin(), transformed.end()};
            return fmt::join(strings, " ");
        };

        logger["logs"] = []()
        {
            return sol::as_table(logger::get().logs());
        };

        logger["error"] = [to_string](const sol::variadic_args &args)
        {
            logger::get()->error("[lua] {}", to_string(args));
        };

        logger["debug"] = [to_string](const sol::variadic_args &args)
        {
            logger::get()->debug("[lua] {}", to_string(args));
        };

        logger["info"] = [to_string](const sol::variadic_args &args)
        {
            logger::get()->info("[lua] {}", to_string(args));
        };

        logger["warn"] = [to_string](const sol::variadic_args &args)
        {
            logger::get()->warn("[lua] {}", to_string(args));
        };
    }

    void manager::impl::load_mods()
    {
        auto mod_dir = fs::current_path() / "mods";
        logger::get()->debug("loading mods from \"{}\"", mod_dir.string());

        if (!fs::exists(mod_dir))
        {
            fs::create_directories(mod_dir);
        }

        if (!fs::is_directory(mod_dir))
        {
            logger::get()->error("not a directory (\"{}\")", mod_dir.string());
            return;
        }

        std::vector<std::shared_ptr<mod>> loaded;
        fs::directory_iterator iterator{mod_dir};

        for (const auto &entry : iterator)
        {
            if (!entry.is_directory())
            {
                logger::get()->debug("skipping \"{}\" as it's not a directory", entry.path().filename().string());
                continue;
            }

            auto mod = mod::from(entry);

            if (!mod)
            {
                continue;
            }

            auto duplicate = std::ranges::find_if(loaded, [&](auto &x) { return x->name() == mod->name(); });

            if (duplicate != loaded.end())
            {
                auto [name, author, version] = std::make_tuple(mod->name(), mod->author(), mod->version());

                auto [d_name, d_author, d_version] =
                    std::make_tuple((*duplicate)->name(), (*duplicate)->author(), (*duplicate)->version());

                logger::get()->warn(
                    R"(duplicate name: "{}" (by "{}", {}) and "{}" (by "{}", {}), the latter will not be loaded!)",
                    d_name, d_author, d_version, name, author, version);

                continue;
            }

            loaded.emplace_back(mod);
        }

        using std::views::filter;
        auto enabled = loaded | filter([](auto &x) { return x->enabled(); });

        graph graph;

        for (const auto &mod : enabled)
        {
            graph.add_node(mod->name());
        }

        for (const auto &mod : enabled)
        {
            for (const auto &dependency : mod->dependencies())
            {
                graph.add_edge(mod->name(), dependency);
            }
        }

        auto order = graph.resolve_dependencies();
        logger::get()->debug("load order: {}", fmt::join(order, ", "));

        for (const auto &name : order)
        {
            auto mod = std::ranges::find_if(loaded, [&](auto &x) { return x->name() == name; });

            if (mod == loaded.end())
            {
                continue;
            }

            mods.emplace_back(*mod)->load();
        }
    }
} // namespace loader