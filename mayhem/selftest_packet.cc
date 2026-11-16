// Known-answer tests for the Socket.IO packet codec (sio_packet.cpp), built with the project's
// normal flags. Prints one PASS/FAIL line per case and a SUMMARY line that test.sh parses.
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "sio_message.h"
#include "sio_packet.h"

static int g_run = 0, g_failed = 0;

static void check(bool ok, const char* name, const std::string& detail = "")
{
    ++g_run;
    if (!ok) ++g_failed;
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : " :: ", detail.c_str());
}

static std::string describe(const sio::message::ptr& m)
{
    if (!m) return "<null>";
    std::ostringstream os;
    switch (m->get_flag()) {
    case sio::message::flag_integer: os << "i" << m->get_int(); break;
    case sio::message::flag_double: os << "d" << m->get_double(); break;
    case sio::message::flag_string: os << "s'" << m->get_string() << "'"; break;
    case sio::message::flag_boolean: os << (m->get_bool() ? "true" : "false"); break;
    case sio::message::flag_null: os << "null"; break;
    case sio::message::flag_binary: os << "bin'" << *m->get_binary() << "'"; break;
    case sio::message::flag_array:
        os << "[";
        for (const auto& c : m->get_vector()) os << describe(c) << ",";
        os << "]";
        break;
    case sio::message::flag_object:
        os << "{";
        for (const auto& kv : m->get_map()) os << kv.first << "=" << describe(kv.second) << ",";
        os << "}";
        break;
    }
    return os.str();
}

struct Decoded {
    sio::packet::frame_type frame;
    int type;
    std::string nsp;
    unsigned pack_id;
    std::string msg;
};

static std::vector<Decoded> decode(const std::vector<std::string>& payloads)
{
    std::vector<Decoded> out;
    sio::packet_manager mgr;
    mgr.set_decode_callback([&](sio::packet const& p) {
        Decoded d;
        d.frame = p.get_frame();
        d.type = d.frame == sio::packet::frame_message ? (int)p.get_type() : -1;
        d.nsp = p.get_nsp();
        d.pack_id = p.get_pack_id();
        d.msg = describe(p.get_message());
        out.push_back(d);
    });
    for (const auto& s : payloads) mgr.put_payload(s);
    return out;
}

static std::vector<std::pair<bool, std::string>> encode(sio::packet& p)
{
    std::vector<std::pair<bool, std::string>> out;
    sio::packet_manager mgr;
    mgr.encode(p, [&](bool bin, std::shared_ptr<const std::string> const& s) { out.emplace_back(bin, *s); });
    return out;
}

int main()
{
    {
        auto r = decode({"42[\"hello\",{\"a\":1,\"b\":\"x\"}]"});
        bool ok = r.size() == 1 && r[0].frame == sio::packet::frame_message &&
                  r[0].type == sio::packet::type_event && r[0].nsp == "/" &&
                  r[0].pack_id == (unsigned)-1 && r[0].msg == "[s'hello',{a=i1,b=s'x',},]";
        check(ok, "event in default namespace", r.empty() ? "no packet" : r[0].msg);
    }
    {
        auto r = decode({"42/chat,15[\"msg\",true,null,1.5]"});
        bool ok = r.size() == 1 && r[0].type == sio::packet::type_event && r[0].nsp == "/chat" &&
                  r[0].pack_id == 15 && r[0].msg == "[s'msg',true,null,d1.5,]";
        check(ok, "event with namespace and ack id", r.empty() ? "no packet" : r[0].nsp + " " + r[0].msg);
    }
    {
        auto r = decode({"0{\"sid\":\"abc\",\"pingInterval\":25000}"});
        bool ok = r.size() == 1 && r[0].frame == sio::packet::frame_open &&
                  r[0].msg == "{pingInterval=i25000,sid=s'abc',}";
        check(ok, "open handshake", r.empty() ? "no packet" : r[0].msg);
    }
    {
        auto r = decode({"2"});
        check(r.size() == 1 && r[0].frame == sio::packet::frame_ping && r[0].msg == "<null>", "ping frame");
    }
    {
        auto r = decode({"40/admin"});
        bool ok = r.size() == 1 && r[0].type == sio::packet::type_connect && r[0].nsp == "/admin" &&
                  r[0].msg == "<null>";
        check(ok, "namespace connect", r.empty() ? "no packet" : r[0].nsp);
    }
    {
        auto r = decode({"451-[\"up\",{\"_placeholder\":true,\"num\":0}]", "ABC"});
        bool ok = r.size() == 1 && r[0].type == sio::packet::type_binary_event &&
                  r[0].msg == "[s'up',bin'ABC',]";
        check(ok, "binary event reassembly", r.empty() ? "no packet" : r[0].msg);
    }
    {
        auto r = decode({"49[]"});
        check(r.size() == 1 && r[0].msg == "<null>", "unknown packet type carries no message");
    }
    {
        sio::message::ptr arr = sio::array_message::create();
        arr->get_vector().push_back(sio::string_message::create("hi"));
        arr->get_vector().push_back(sio::int_message::create(5));
        sio::packet p("/", arr);
        auto e = encode(p);
        check(e.size() == 1 && !e[0].first && e[0].second == "42[\"hi\",5]", "encode event",
              e.empty() ? "nothing" : e[0].second);
    }
    {
        sio::message::ptr arr = sio::array_message::create();
        arr->get_vector().push_back(sio::string_message::create("hi"));
        arr->get_vector().push_back(sio::int_message::create(5));
        sio::packet p("/room", arr, 7);
        auto e = encode(p);
        check(e.size() == 1 && e[0].second == "42/room,7[\"hi\",5]", "encode namespaced event with id",
              e.empty() ? "nothing" : e[0].second);
    }
    {
        sio::message::ptr arr = sio::array_message::create();
        arr->get_vector().push_back(sio::string_message::create("e"));
        arr->get_vector().push_back(sio::binary_message::create(std::make_shared<std::string>("xyz")));
        sio::packet p("/", arr);
        auto e = encode(p);
        bool ok = e.size() == 2 && !e[0].first &&
                  e[0].second == "451-[\"e\",{\"_placeholder\":true,\"num\":0}]" && e[1].first &&
                  e[1].second == "xyz";
        check(ok, "encode binary event", e.empty() ? "nothing" : e[0].second);
    }
    {
        const std::string wire = "42/room,7[\"a\",{\"k\":[1,2,3],\"n\":null}]";
        auto r = decode({wire});
        bool ok = false;
        if (r.size() == 1) {
            sio::packet_manager mgr;
            sio::message::ptr got;
            mgr.set_decode_callback([&](sio::packet const& p) { got = p.get_message(); });
            mgr.put_payload(wire);
            sio::packet p("/room", got, 7);
            auto e = encode(p);
            ok = e.size() == 1 && e[0].second == "42/room,7[\"a\",{\"k\":[1,2,3],\"n\":null}]";
        }
        check(ok, "decode then encode round trip");
    }

    printf("SUMMARY: %d tests run; %d failed\n", g_run, g_failed);
    return g_failed == 0 && g_run > 0 ? 0 : 1;
}
