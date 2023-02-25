#include <iostream>
#include <vector>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

//#define ASIO_STANDALONE
#include <boost/asio.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <atomic_queue/atomic_queue.h>

using queue = atomic_queue::AtomicQueue2<std::vector<uint8_t>, 1024>;
using namespace boost;
std::vector<asio::ip::tcp::socket*> sockets;
std::vector<asio::ip::tcp::acceptor*> acceptors;

queue c2p_queue;
queue s2p_queue;

queue p2s_queue;
queue p2c_queue;

std::string make_string(asio::streambuf& streambuf)
{
    return {asio::buffers_begin(streambuf.data()), asio::buffers_end(streambuf.data())};
}
std::vector<uint8_t> make_uint8vector(asio::streambuf& streambuf)
{
    return {asio::buffers_begin(streambuf.data()), asio::buffers_end(streambuf.data())};
}
uint32_t make_uint32(asio::streambuf& streambuf) {
    std::istream is(&streambuf);
    return ((uint32_t) is.get() << 24) 
         + ((uint32_t) is.get() << 16) 
         + ((uint32_t) is.get() <<  8) 
         + ((uint32_t) is.get());
}

void readBuf(asio::ip::tcp::socket &socket, queue &out) {
    asio::streambuf read_buffer;

    while (1) {
        //Read first 4 bytes
        asio::read(socket, read_buffer, asio::transfer_exactly(4));
        std::vector<uint8_t> length_v = make_uint8vector(read_buffer);
        uint32_t length = make_uint32(read_buffer);
        std::cout << "Header: " << std::hex << (int) length << "\n";

        asio::read(socket, read_buffer, asio::transfer_exactly(length - 4));
        std::string s = make_string(read_buffer);
        std::vector<uint8_t> v = make_uint8vector(read_buffer);
        read_buffer.consume(length); // Remove data that was read.
        std::cout << "Read: " << s << std::endl;
        length_v.insert(length_v.end(), s.begin(), s.end());
        out.push(length_v);
    }
}

void writeBuf(asio::ip::tcp::socket &socket, queue &in) {
    while (1) {
        std::vector<uint8_t> s = in.pop();
        asio::write(socket, asio::buffer(s));
    }
}

void connectLocal(asio::io_context &context, asio::ip::tcp::socket* socket, asio::ip::address address, int port) {
    asio::ip::tcp::endpoint endpoint(address, port);
    asio::ip::tcp::acceptor* acceptor = new asio::ip::tcp::acceptor(context, endpoint.protocol());

    acceptor->set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor->bind(endpoint);
    acceptors.push_back(acceptor);
    acceptor->listen();
    
    acceptor->accept(*socket);
    sockets.push_back(socket);
}

void client(asio::io_context &context) {
    asio::ip::tcp::socket* socket = new asio::ip::tcp::socket(context);
    std::cout << "Connecting to client\n";
    connectLocal(context, socket, asio::ip::make_address("127.0.0.1"), 2050);

    std::thread c2p(readBuf, std::ref(*socket), std::ref(c2p_queue));
    std::thread p2c(writeBuf, std::ref(*socket), std::ref(s2p_queue));
    c2p.join();
    p2c.join();
}

void connectRemote(asio::io_context &context, asio::ip::tcp::socket* socket, asio::ip::address address, int port) {
    asio::ip::tcp::endpoint endpoint(address, port);
    socket->connect(endpoint);
    sockets.push_back(socket);
}

void server(asio::io_context &context) {
    asio::ip::tcp::socket* socket = new asio::ip::tcp::socket(context);
    std::cout << "Connecting to server\n";
    connectRemote(context, socket, asio::ip::make_address("54.86.47.176"), 2050);

    std::thread s2p(readBuf, std::ref(*socket), std::ref(s2p_queue));
    std::thread p2s(writeBuf, std::ref(*socket), std::ref(c2p_queue));
    s2p.join();
    p2s.join();
}

struct GroundStructure {
    std::string id;
    ushort type;
    bool no_walk;
    float speed;
    bool sink;
    ushort min_damage;
    ushort max_damage;
};

GroundStructure make_GroundStructure(boost::property_tree::ptree& child) {
    GroundStructure ts;
    ts.type = std::stoi(child.get_optional<std::string>("<xmlattr>.type").get_value_or("0x0"), nullptr, 0);
    ts.id = child.get_optional<std::string>("<xmlattr>.id").get_value_or("");
    ts.no_walk = child.find("NoWalk") != child.not_found();
    ts.speed = child.get_optional<float>("Speed").get_value_or(1);
    ts.sink = child.find("Sink") != child.not_found();
    ts.min_damage = child.get_optional<ushort>("MinDamage").get_value_or(0);
    ts.max_damage = child.get_optional<ushort>("MaxDamage").get_value_or(0);
    return ts;
};

struct ProjectileStructure {
    uint8_t id;
    std::string object_id;
    int damage;
    float speed;
    int size;
    float lifetime_ms;
    int max_damage;
    int min_damage;
    float magnitude;
    float amplitude;
    float frequency;
    bool wavy;
    bool parametric;
    bool boomerang;
    bool armor_piercing;
    bool multi_hit;
    bool passes_cover;
    std::unordered_map<std::string, float> status_effects;
};

ProjectileStructure make_ProjectileStructure(boost::property_tree::ptree& child) {
    ProjectileStructure ps;
    ps.id = child.get_optional<uint8_t>("<xmlattr>.id").get_value_or(0);
    ps.object_id = child.get_optional<std::string>("ObjectId").get_value_or("");
    ps.damage = child.get_optional<int>("Damage").get_value_or(0);
    ps.speed = child.get_optional<float>("Speed").get_value_or(0) / 10000.f;
    ps.size = child.get_optional<int>("Size").get_value_or(0);
    ps.lifetime_ms = child.get_optional<float>("LifetimeMS").get_value_or(0);
    ps.min_damage = child.get_optional<ushort>("MinDamage").get_value_or(0);
    ps.max_damage = child.get_optional<ushort>("MaxDamage").get_value_or(0);
    ps.magnitude = child.get_optional<float>("Magnitude").get_value_or(0);
    ps.amplitude = child.get_optional<float>("Amplitude").get_value_or(0);
    ps.frequency = child.get_optional<float>("Frequency").get_value_or(0);
    ps.wavy = child.find("Wavy") != child.not_found();
    ps.parametric = child.find("Parametric") != child.not_found();
    ps.boomerang = child.find("Boomerang") != child.not_found();
    ps.armor_piercing = child.find("ArmorPiercing") != child.not_found();
    ps.multi_hit = child.find("MultiHit") != child.not_found();
    ps.passes_cover = child.find("PassesCover") != child.not_found();
    for (auto &elem : child) {
        if (elem.first == "ConditionEffect") {
            float duration = elem.second.get_optional<float>("<xmlattr>.duration").get_value_or(0);
            std::string effect_name = elem.second.data();
            ps.status_effects.insert({effect_name, duration});
        }
    }
    return ps;
};

struct PlayerStats {
    int max_hp;
    int max_mp;
    int attack;
    int defense;
    int speed;
    int dexterity;
    int hp_regen;
    int mp_regen;
};

struct ObjectStructure {
    ushort type;
    std::string id;
    std::string class_;
    ushort max_hit_points;
    float xp_mult;
    bool static_;
    bool occupy_square;
    bool enemy_occupy_square;
    bool full_occupy;
    bool blocks_sight;
    bool protect_from_ground_damage;
    bool protect_from_sink;
    bool enemy;
    bool player;
    bool pet;
    bool draw_on_ground;
    ushort size;
    ushort shadow_size;
    ushort defense;
    bool flying;
    bool god;
    bool cube;
    bool quest;
    bool item;
    bool usable;
    bool soulbound;
    ushort mp_cost;
    std::unordered_set<ProjectileStructure> projectiles;
    bool invulnerable;
    bool invincible;
    PlayerStats player_stats;
};

ObjectStructure make_ObjectStructure(boost::property_tree::ptree &child) {
    ObjectStructure os;
    os.type = std::stoi(child.get_optional<std::string>("<xmlattr>.type").get_value_or("0x0"), nullptr, 0);
    os.id = child.get_optional<std::string>("<xmlattr>.id").get_value_or("");
    os.class_ = child.get_optional<std::string>("Class").get_value_or("GameObject");
    os.max_hit_points = std::stoi(child.get_optional<std::string>("<xmlattr>.type").get_value_or("0x0"), nullptr, 0);
    os.xp_mult = child.get_optional<float>("XpMult").get_value_or(0);
    os.static_ = child.find("Static") != child.not_found();
    os.occupy_square = child.find("OccupySquare") != child.not_found();
    os.enemy_occupy_square = child.find("EnemyOccupySquare") != child.not_found();
    os.full_occupy = child.find("FullOccupy") != child.not_found();
    os.blocks_sight = child.find("BlocksSight") != child.not_found();
    os.protect_from_ground_damage = child.find("ProtectFromGroundDamage") != child.not_found();
    os.protect_from_sink = child.find("ProtectFromSink") != child.not_found();
    os.enemy = child.find("Enemy") != child.not_found();
    os.player = child.find("Player") != child.not_found();
    os.pet = child.find("Pet") != child.not_found();
    os.draw_on_ground = child.find("DrawOnGround") != child.not_found();
    os.size = child.get_optional<ushort>("Size").get_value_or(0);
    os.shadow_size = child.get_optional<ushort>("ShadowSize").get_value_or(0);
    os.defense = child.get_optional<ushort>("Defense").get_value_or(0);
    os.flying = child.find("Flying") != child.not_found();
    os.god = child.find("God") != child.not_found();
    os.cube = child.find("Cube") != child.not_found();
    os.quest = child.find("Quest") != child.not_found();
    os.invulnerable = child.find("Invulnerable") != child.not_found();
    os.invincible = child.find("Invincible") != child.not_found();
    os.item = child.find("Item") != child.not_found();
    os.usable = child.find("Usable") != child.not_found();
    os.soulbound = child.find("Soubound") != child.not_found();
    os.mp_cost = child.get_optional<ushort>("MpCost").get_value_or(0);
    for (auto &elem : child) {
        if (elem.first == "Projectile") {
            os.projectiles.insert(make_ProjectileStructure(elem.second));
        }
    }
    if (os.player) {
        os.player_stats.max_hp = child.get_child_optional("MaxHitPoints").map([](boost::property_tree::ptree &child_child) {
            child_child.get_optional<int>("max");
        }).get_value_or(0);
        os.player_stats.max_mp = child.get_child_optional("MaxMagicPoints").map([](boost::property_tree::ptree &child_child) {
            child_child.get_optional<int>("max");
        }).get_value_or(0);
        os.player_stats.attack = child.get_child_optional("Attack").map([](boost::property_tree::ptree &child_child) {
            child_child.get_optional<int>("max");
        }).get_value_or(0);
        os.player_stats.defense = child.get_child_optional("Defense").map([](boost::property_tree::ptree &child_child) {
            child_child.get_optional<int>("max");
        }).get_value_or(0);
        os.player_stats.speed = child.get_child_optional("Speed").map([](boost::property_tree::ptree &child_child) {
            child_child.get_optional<int>("max");
        }).get_value_or(0);
        os.player_stats.dexterity = child.get_child_optional("Dexterity").map([](boost::property_tree::ptree &child_child) {
            child_child.get_optional<int>("max");
        }).get_value_or(0);
        os.player_stats.hp_regen = child.get_child_optional("HpRegen").map([](boost::property_tree::ptree &child_child) {
            child_child.get_optional<int>("max");
        }).get_value_or(0);
        os.player_stats.mp_regen = child.get_child_optional("MpRegen").map([](boost::property_tree::ptree &child_child) {
            child_child.get_optional<int>("max");
        }).get_value_or(0);
    }
}

struct ItemStructure {
    ushort type;
    std::string id;
    ProjectileStructure projectile;
    int num_projectiles;
    int tier;
    uint8_t slot_type;
    float rate_of_fire;
    uint32_t feed_power;
    uint8_t bag_type;
    uint8_t mp_cost;
    double cooldown;
    float radius;
    uint8_t fame_bonus;
    bool soulbound;
    bool usable;
    bool consumable;
    bool potion;
    int quickslot_allowed_maxstack;
    std::unordered_set<std::pair<std::string, float>> activate_type_list;
};

ItemStructure make_ItemStructure(boost::property_tree::ptree &child) {
    ItemStructure is;
    is.type = std::stoi(child.get_optional<std::string>("<xmlattr>.type").get_value_or("0x0"), nullptr, 0);
    is.id = child.get_optional<std::string>("<xmlattr>.id").get_value_or("");
    is.tier = child.get_optional<int>("Tier").get_value_or(-1); //fix once enum
    is.slot_type = child.get_optional<uint8_t>("SlotType").get_value_or(0);
    is.rate_of_fire = child.get_optional<float>("RateOfFire").get_value_or(1);
    is.feed_power = child.get_optional<uint32_t>("feedPower").get_value_or(0);
    is.bag_type = child.get_optional<uint8_t>("BagType").get_value_or(0);
    is.mp_cost = child.get_optional<uint8_t>("MpCost").get_value_or(0);
    is.cooldown = child.get_optional<double>("Cooldown").get_value_or(0);
    is.fame_bonus = child.get_optional<uint8_t>("FameBonus").get_value_or(0);
    is.soulbound = child.find("Soulbound") != child.not_found();
    is.usable = child.find("Usable") != child.not_found();
    is.consumable = child.find("Consumable") != child.not_found();
    is.num_projectiles = child.get_optional<int>("NumProjectiles").get_value_or(0);
    auto projectile = child.get_child_optional("Projectile");
    if (projectile) 
        is.projectile = make_ProjectileStructure(projectile.get());
    is.quickslot_allowed_maxstack = child.get_child_optional("QuickslotAllowed").map([](boost::property_tree::ptree child_child) {
        child_child.get_optional<int>("maxstack");
    }).get_value_or(6);
    auto activate = child.get_child_optional("Activate");
    if (activate) 
        for (auto &elem : activate.get()) {
            is.activate_type_list.insert({elem.second.data(), elem.second.get_optional<float>("<xmlattr>.radius").get_value_or(0)});
        }
}

struct PacketStructure {
    uint8_t id;
};

PacketStructure make_PacketStructure(boost::property_tree::ptree &ptree) {
    return PacketStructure();
    //no
}

int main()
{
    boost::property_tree::ptree ptree;
    boost::property_tree::xml_parser::read_xml("test.xml", ptree);
    for (auto &child : ptree.get_child("All").get_child("GroundTypes")) {
        make_GroundStructure(child.second);
    }
    make_ProjectileStructure(ptree.get_child("All").get_child("Projectile"));

    asio::io_context context;
    signal(SIGINT, [](int) {
        std::cout << "Closing sockets" << std::endl;
        for (auto socket : sockets) {
            socket->shutdown(asio::ip::tcp::socket::shutdown_both);
            socket->close();
            delete socket; 
        }
        for (auto acceptor : acceptors) {
            acceptor->cancel();
            acceptor->close();
            delete acceptor;
        }
        std::exit(0);
    });
    std::thread clientThread(client, std::ref(context));
    std::thread serverThread(server, std::ref(context));
    clientThread.join();
    serverThread.join();
    return 0;
}