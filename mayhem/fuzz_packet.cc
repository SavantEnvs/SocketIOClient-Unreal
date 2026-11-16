// Fuzzes the Socket.IO wire-protocol decoder (sio::packet_manager::put_payload) with the same
// calls sio_client_impl makes: raw frames in, decoded packets out through the decode callback,
// plus the encoder on every decoded packet.
//
// The input is a sequence of websocket payloads separated by 0xFF (never valid in the UTF-8 text
// frames; binary attachments follow a "5"/"6" text frame as separate payloads).
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sio_message.h"
#include "sio_packet.h"

static size_t walk(const sio::message::ptr& m, int depth)
{
    if (!m) return 0;
    size_t n = 1;
    switch (m->get_flag()) {
    case sio::message::flag_array:
        for (const auto& c : m->get_vector()) n += walk(c, depth + 1);
        break;
    case sio::message::flag_object:
        for (const auto& kv : m->get_map()) n += kv.first.size() + walk(kv.second, depth + 1);
        break;
    case sio::message::flag_string:
        n += m->get_string().size();
        break;
    case sio::message::flag_binary:
        n += m->get_binary() ? m->get_binary()->size() : 0;
        break;
    case sio::message::flag_integer:
        n += (size_t)m->get_int();
        break;
    case sio::message::flag_double:
        n += (size_t)(m->get_double() != 0.0);
        break;
    case sio::message::flag_boolean:
        n += m->get_bool();
        break;
    default:
        break;
    }
    return n;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    sio::packet_manager mgr;
    volatile size_t sink = 0;

    mgr.set_decode_callback([&](sio::packet const& p) {
        sink += p.get_frame();
        if (p.get_frame() == sio::packet::frame_message) {
            // The client only reads the type of a packet routed to a socket: on_decode looks the
            // socket up by exact nsp (keys always start with '/'), and on_message_packet checks
            // nsp == m_nsp before switch(get_type()). packet::parse leaves _nsp empty only when it
            // rejects an out-of-range type digit, and every _nsp assignment follows that check, so
            // this skips exactly the packets the client drops.
            if (!p.get_nsp().empty())
                sink += p.get_type();
            sink += p.get_nsp().size();
            sink += p.get_pack_id();
        }
        sink += walk(p.get_message(), 0);

        sio::packet copy = p;
        sio::packet_manager enc;
        enc.encode(copy, [&](bool, std::shared_ptr<const std::string> const& payload) {
            sink += payload->size();
        });
    });

    size_t start = 0;
    for (size_t i = 0; i <= size; ++i) {
        if (i == size || data[i] == 0xFF) {
            mgr.put_payload(std::string((const char*)data + start, i - start));
            start = i + 1;
        }
    }
    return 0;
}
