#pragma once

#include <libtorrent/aux_/session_interface.hpp>
#include <libtorrent/aux_/session_impl.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_class.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/session.hpp>


namespace CustomLtPlugin
{
    struct CustomPeerClasses
    {
        lt::peer_class_pool *pool = {};
        lt::peer_class_t unbound_class{}, indebt_class{}, high_priority_class{};
    };

    class peer_uploading_bump_peer_plugin final : public lt::peer_plugin
    {
    private:
        libtorrent::peer_connection_handle m_peer_connection_handle;
        uint64_t m_tick_count = 0;
        std::shared_ptr<CustomPeerClasses> m_custom_peer_classes;

    public:
        peer_uploading_bump_peer_plugin(libtorrent::peer_connection_handle const &pc, std::shared_ptr<CustomPeerClasses> custom_peer_classes)
            : m_peer_connection_handle(pc), m_custom_peer_classes(std::move(custom_peer_classes)) {}

        libtorrent::string_view type() const override { return "peer_uploading_bump_plugin"; }

        void tick() override
        {
            ++m_tick_count;
            if ((__builtin_bswap64(reinterpret_cast<uint64_t>(this)) + m_tick_count) % 11 != 0)
                return;
            auto pc = m_peer_connection_handle.native_handle();
            if (!pc)
                return;
            lt::peer_info info;
            pc->get_peer_info(info);
            if (info.total_download <= 0)
                return;
            if (info.down_speed >= info.up_speed) {
                if (!pc->has_class(m_custom_peer_classes->unbound_class))
                    pc->add_class(*m_custom_peer_classes->pool, m_custom_peer_classes->unbound_class);
                if (pc->is_choked())
                    pc->maybe_unchoke_this_peer();
            }
            else if (info.total_download * 5 >= info.total_upload) {
                if (!pc->has_class(m_custom_peer_classes->indebt_class))
                    pc->add_class(*m_custom_peer_classes->pool, m_custom_peer_classes->indebt_class);
                if (pc->has_class(m_custom_peer_classes->unbound_class))
                    pc->remove_class(*m_custom_peer_classes->pool, m_custom_peer_classes->unbound_class);
            }
            else if (info.total_download > 0){
                if (!pc->has_class(m_custom_peer_classes->high_priority_class))
                    pc->add_class(*m_custom_peer_classes->pool, m_custom_peer_classes->high_priority_class);
                if (pc->has_class(m_custom_peer_classes->unbound_class))
                    pc->remove_class(*m_custom_peer_classes->pool, m_custom_peer_classes->unbound_class);
                if (pc->has_class(m_custom_peer_classes->indebt_class))
                    pc->remove_class(*m_custom_peer_classes->pool, m_custom_peer_classes->indebt_class);
            }
        }
    };

    class peer_uploading_bump_torrent_plugin : public lt::torrent_plugin
    {
    private:
        std::shared_ptr<CustomPeerClasses> m_custom_peer_classes;

    public:
        peer_uploading_bump_torrent_plugin(std::shared_ptr<CustomPeerClasses> custom_peer_classes)
            : m_custom_peer_classes(std::move(custom_peer_classes)) {}

        std::shared_ptr<lt::peer_plugin> new_connection(lt::peer_connection_handle const &pc) override
        {
            return std::make_shared<peer_uploading_bump_peer_plugin>(pc, m_custom_peer_classes);
        }
    };

    class peer_uploading_bump_session_plugin : public lt::plugin
    {
    private:
        std::shared_ptr<peer_uploading_bump_torrent_plugin> m_torrent_plugin;

    public:
        peer_uploading_bump_session_plugin(lt::session *session)
        {
            auto classes = std::make_shared<CustomPeerClasses>();
            classes->pool = &session->native_handle()->peer_classes();
            classes->unbound_class = session->create_peer_class("peer_uploading_bump_session_plugin_unbound");
            classes->indebt_class = session->create_peer_class("peer_uploading_bump_session_plugin_indebt");
            classes->high_priority_class = session->create_peer_class("peer_uploading_bump_session_plugin_high_priority");

            auto unbound_peer_info = session->get_peer_class(classes->unbound_class);
            unbound_peer_info.ignore_unchoke_slots = true;
            unbound_peer_info.upload_priority = 100;
            session->set_peer_class(classes->unbound_class, unbound_peer_info);

            auto indebt_peer_info = session->get_peer_class(classes->indebt_class);
            indebt_peer_info.ignore_unchoke_slots = false;
            indebt_peer_info.upload_priority = 90;
            session->set_peer_class(classes->indebt_class, indebt_peer_info);

            auto high_priority_peer_info = session->get_peer_class(classes->high_priority_class);
            high_priority_peer_info.ignore_unchoke_slots = false;
            high_priority_peer_info.upload_priority = 80;
            session->set_peer_class(classes->high_priority_class, high_priority_peer_info);

            m_torrent_plugin = std::make_shared<peer_uploading_bump_torrent_plugin>(classes);
        }

        std::shared_ptr<lt::torrent_plugin> new_torrent(lt::torrent_handle const &, lt::client_data_t) override
        {
            return m_torrent_plugin;
        }
    };
}
