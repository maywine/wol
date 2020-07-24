#include <sys/types.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/ether.h>
#include <ifaddrs.h>  
#include <pwd.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <map>
#include <set>
#include <string>
#include <regex>
#include <vector>
#include <sstream>

static const std::string s_reg_str = "^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$";
static std::string s_stores_file_path;

static void print_usage();
static void list_aliases();
static bool remove_alias(const std::string &alias);
static bool stores_alias(const std::string &alias, const std::string &mac);
static bool send_wol(std::map<std::string, std::string> &cmd_map, std::vector<std::string> &wake_machine_vec);
static std::vector<std::string> split(const std::string &s, char delim);
static std::map<std::string, std::string> parse_mac_addr(const std::string &new_data);
static std::string mac_addr_to_str(const std::map<std::string, std::string> &mac_map);

constexpr uint32_t kMACSize = 17;
constexpr uint32_t kAliasSize = sizeof(uint16_t);
constexpr uint32_t kHeadSize = kMACSize + kAliasSize;
/*
   17 byte     2 byte    variable len   
 ______________________________________
|          |           |               |
| mac addr | alias len |  alias name   |
|__________|___________|_______________|

*/

class file_helper
{
private:
    /* data */
public:
    file_helper() = default;
    ~file_helper();

    bool open(const std::string &file_name, const int mode);

    bool read(std::string &data);

    bool read(std::string &data, const std::size_t file_Size);

    bool write(const std::string &data);

    bool write_truncate_atomic(const std::string &data);

    int fd() const 
    {
        return fd_;
    }

    const std::string& file_name() const
    {
        return file_name_;
    }

private:
    int fd_ = -1;
    std::string file_name_;
};

struct do_on_exit
{
    do_on_exit(std::function<void(void)> hd) : do_on_exit_hd_(hd) {}
    ~do_on_exit()
    {
        if (do_on_exit_hd_)
        {
            do_on_exit_hd_();
        }
    }

private:
    std::function<void(void)> do_on_exit_hd_;
};

int main(int argc, char **argv)
{
    auto pwd = getpwuid(getuid());
    if (pwd == nullptr)
    {
        fprintf(stderr, "getpwuid failed, errno:%d, dsec:%s", errno, strerror(errno));
        return 1;
    }

    s_stores_file_path = pwd->pw_dir;
    s_stores_file_path.append("/.config/wol.db");

    std::regex reg("^(-{0,2})(.+)$");
    std::vector<std::string> wake_machine_vec; 
    std::map<std::string, std::string> cmd_map;

    try
    {
        for (int i = 1; i < argc;)
        {
            std::string cmd = argv[i];
            std::smatch match_result;
            if (std::regex_match(cmd, match_result, reg))
            {
                cmd = match_result[2];
            }

            if (cmd == "h" || cmd == "help")
            {
                print_usage();
                exit(0);
            }
            else if (cmd == "p" || cmd == "port")
            {
                if (i + 1 < argc)
                {
                    cmd_map.emplace("port", argv[i+1]);
                    i += 2;
                    continue;
                }
                else 
                {
                    fprintf(stderr, "option %s required parameters\n", cmd.c_str());
                    exit(1);
                }
            }
            else if (cmd == "b" || cmd == "bcast")
            {
                if (i + 1 < argc)
                {
                    cmd_map.emplace("bcast", argv[i+1]);
                    i += 2;
                    continue;
                }
                else 
                {
                    fprintf(stderr, "option %s required parameters\n", cmd.c_str());
                    exit(1);
                }
            }
            else if (cmd == "i" || cmd == "interface")
            {
                if (i + 1 < argc)
                {
                    cmd_map.emplace("interface", argv[i+1]);
                    i += 2;
                    continue;
                }
                else 
                {
                    fprintf(stderr, "option %s required parameters\n", cmd.c_str());
                    exit(1);
                }
            }
            else if (cmd == "list")
            {
                list_aliases();
                exit(0);
            }
            else if (cmd == "remove")
            {
                if (i + 1 < argc)
                {            
                    remove_alias(argv[i+1]) ? exit(0) : exit(1);
                }
                else
                {
                    fprintf(stderr, "option %s required parameters\n", cmd.c_str());
                    exit(1);   
                }
            }
            else if (cmd == "alias")
            {
                if (i + 2 < argc)
                {
                    stores_alias(argv[i+1], argv[i+2]) ? exit(0) : exit(1);
                }
                else 
                {
                    fprintf(stderr, "option %s required two parameters\n", cmd.c_str());
                    exit(1);
                }
            }
            else if (cmd == "wake")
            {
                if (i + 1 < argc)
                {
                    cmd_map.emplace("wake", argv[i+1]);
                    i += 2;
                    continue;
                }
                else 
                {
                    fprintf(stderr, "option %s required two parameters\n", cmd.c_str());
                    exit(1);
                }
            }

            wake_machine_vec.emplace_back(argv[i]);
            ++i;
        }

        send_wol(cmd_map, wake_machine_vec) ? exit(0) : exit(1);
        
    }
    catch (const std::exception &err)
    {
        fprintf(stderr, "catch exception： %s\n", err.what());
        return 1;
    }

    return 0;
}

static std::vector<std::string> split(const std::string &s, char delim) 
{
    std::stringstream ss(s);
    std::string item;
    std::vector<std::string> elems;
    while (std::getline(ss, item, delim)) 
    {
        elems.push_back(std::move(item));
    }
    return elems;
}

static void print_usage()
{
    const char *usage = "Usage:\n"
    "   To wake up a machine:\n"
    "       wol wake <mac address | alias> <optional ...>\n"
    "       wol <mac address | alias> <optional ...>\n"
    "\n"
    "   To store an alias:\n"
    "       wol alias <alias> <mac address>\n"
    "\n"
    "   To view aliases:\n"
    "        wol list\n"
    "\n"
    "   To delete aliases:\n"
    "       wol remove <alias>\n"
    "\n"
    "   The following MAC addresses are valid and will match:\n"
    "   01-23-45-56-67-89, 89:AB:CD:EF:00:12, 89:ab:cd:ef:00:12\n"
    "\n"
    "   The following MAC addresses are not (yet) valid:\n"
    "   1-2-3-4-5-6, 01 23 45 56 67 89\n"
    "\n"
    "Commands:\n"
    "   wake               wakes up a machine by mac address or alias\n"
    "   list               lists all mac addresses and their aliases\n"
    "   alias              stores an alias to a mac address\n"
    "   remove             removes an alias or a mac address\n"
    "\n"
    "\n"
    "Options:\n"
    "   -h --help          prints this help menu\n"
    "   -p --port          udp port to send bcast packet to\n"
    "   -b --bcast         broadcast IP to send packet to\n"
    "   -i --interface     outbound interface to broadcast using\n";
    
    printf("%s\n", usage);
}

static std::map<std::string, std::string> parse_mac_addr(const std::string &data)
{
    std::size_t pos = 0;
    std::size_t data_size = data.size();

    std::map<std::string, std::string> mac_addr_map;
    while (data_size - pos > kHeadSize)
    {
        uint16_t alias_size = 0;
        memcpy(&alias_size, &data[pos + kMACSize], kAliasSize);
        if (alias_size == 0 
            || (data_size - pos) < (kHeadSize + alias_size))
        {
            fprintf(stderr, "stores file: %s invalid, please remove it\n", s_stores_file_path.c_str());
            exit(1);
        }

        auto alias = data.substr(pos + kHeadSize, alias_size);
        auto mc_addr = data.substr(pos, kMACSize);
        mac_addr_map.emplace(std::move(alias), std::move(mc_addr));
        pos += kHeadSize + alias_size;
    }

    return mac_addr_map;
}

static std::string mac_addr_to_str(const std::map<std::string, std::string> &mac_map)
{
    std::string data_str;
    std::size_t total_size = 0;
    for (auto &item : mac_map)
    {
        total_size += item.first.size();
        total_size += kHeadSize;
    }

    if (total_size == 0)
    {
        return data_str;
    }

    data_str.resize(total_size);
    std::size_t pos = 0;
    for (auto &item : mac_map)
    {
        memcpy(&data_str[pos], &item.second[0], kMACSize);
        pos += kMACSize;
        uint16_t alias_size = item.first.size();
        memcpy(&data_str[pos], &alias_size, kAliasSize);
        pos += kAliasSize;
        memcpy(&data_str[pos], &item.first[0], alias_size);
        pos += alias_size;
    }

    return data_str;
}

static void list_aliases()
{
    file_helper file;
    if (!file.open(s_stores_file_path, O_RDONLY | O_CREAT))
    {
        exit(1);
    }
    std::string data;
    if (!file.read(data))
    {
        exit(1);
    }
    auto mac_addr_map = parse_mac_addr(data);
    if (!mac_addr_map.empty())
    {
        printf("all aliases:\n");
        for (auto &item : mac_addr_map)
        {
            printf("    %s    %s\n", item.second.c_str(), item.first.c_str());
        }
    }
    else
    {
        printf("no aliases\n");
    }
}

static bool remove_alias(const std::string &alias)
{
    file_helper file;
    if (!file.open(s_stores_file_path, O_RDONLY))
    {
        return false;
    }
    std::string data;
    if (!file.read(data))
    {
        return false;
    }
    auto mac_addr_map = parse_mac_addr(data);
    auto it = mac_addr_map.find(alias);
    if (it == mac_addr_map.end())
    {
        fprintf(stderr, "alias: %s no found\n", alias.c_str());
        return false;
    }

    auto mac_addr = it->second;
    mac_addr_map.erase(it);
    auto new_data = mac_addr_to_str(mac_addr_map);
    if (file.write_truncate_atomic(new_data))
    {
        printf("remove alias: %s %s ok\n", alias.c_str(), mac_addr.c_str());
        return true;
    }
    else
    {
        fprintf(stderr, "remove alias failed\n");
        return false;
    }
}

static bool stores_alias(const std::string &alias, const std::string &mac)
{
    std::regex reg(s_reg_str);
    if (!std::regex_match(mac, reg))
    {
        fprintf(stderr, "invalid mac addr：%s failed\n", mac.c_str());
        return false;
    }

    file_helper file;
    if (!file.open(s_stores_file_path, O_RDONLY | O_CREAT))
    {
        exit(1);
    }
    std::string data;
    if (!file.read(data))
    {
        exit(1);
    }
    auto mac_addr_map = parse_mac_addr(data);
    auto it = mac_addr_map.find(alias);
    if (it != mac_addr_map.end())
    {
        fprintf(stderr, "alias: %s  %s already exist\n", alias.c_str(), it->second.c_str());
        return false;
    }

    mac_addr_map.emplace(alias, mac);
    auto new_data = mac_addr_to_str(mac_addr_map);
    if (file.write_truncate_atomic(new_data))
    {
        printf("stores alias %s %s ok\n", alias.c_str(), mac.c_str());
        return true;
    }
    else
    {
        fprintf(stderr, "stores alias failed\n");
        return false;
    }
}

std::vector<unsigned char> package_magic_data(const std::string &mac_addr)
{
    std::vector<unsigned char> package_data;
    package_data.resize(102);
    memset(&package_data[0], 0xff, 6);

    std::vector<char> mac_addr_data;
    auto mac_addr_vec = split(mac_addr, ':');
    for (auto &item : mac_addr_vec)
    {
        mac_addr_data.emplace_back(std::stoul(item, 0, 16));
    }
    
    int package_data_pos = 6;
    for(int i = 0; i < 16; ++i) 
    {
        memcpy(&package_data[package_data_pos], &mac_addr_data[0], 6);
        package_data_pos += 6;
    }

    return package_data;
}

std::set<std::string> get_interfaces()
{
    std::set<std::string> interfaces_name_set;
    struct ifaddrs *iflist;

    if (getifaddrs(&iflist) < 0)
    {
        return interfaces_name_set;
    }

    for (auto ifa = iflist; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr
            && ifa->ifa_flags & IFF_UP
            && !(ifa->ifa_flags & IFF_LOOPBACK))
        {
            std::string ifa_name(ifa->ifa_name);
            interfaces_name_set.emplace(std::move(ifa_name));
        }
    }

    freeifaddrs(iflist);
    return interfaces_name_set;
}

static bool send_wol(std::map<std::string, std::string> &cmd_map, std::vector<std::string> &wake_machine_vec)
{
    std::string bcast_addr = "255.255.255.255";
    uint16_t port = 9;
    std::set<std::string> interface_set;

    auto it = cmd_map.find("bcast");
    if (it != cmd_map.end())
    {
        bcast_addr = it->second;
    }
    it = cmd_map.find("port");
    if (it != cmd_map.end())
    {
        port = std::stoi(it->second);
    }
    it = cmd_map.find("interface");
    if (it != cmd_map.end())
    {
        interface_set.emplace(it->second);
    }

    if (interface_set.empty())
    {
        interface_set = get_interfaces();
        if (interface_set.empty())
        {
            fprintf(stderr, "get network interfaces failed, errno:%d, dsec:%s\n", errno, strerror(errno));
            return false;
        }
    }

    file_helper file;
    std::string data;
    if (!file.open(s_stores_file_path, O_RDONLY | O_CREAT))
    {
        fprintf(stderr, "open stores file faile\n");
        return false;
    }
    else 
    {
        if (!file.read(data))
        {
            fprintf(stderr, "read open stores file failed\n");
            return false;
        }
    }

    auto mac_addr_map = parse_mac_addr(data);
    it = cmd_map.find("wake");
    if (it != cmd_map.end())
    {
        wake_machine_vec.emplace_back(it->second);
    }

    std::vector<std::string> mac_addr_vec;
    for (auto &mac_addr : wake_machine_vec)
    {
        std::regex reg(s_reg_str);
        if (std::regex_match(mac_addr, reg))
        {
            std::replace(mac_addr.begin(), mac_addr.begin(), '-', ':');
            mac_addr_vec.emplace_back(std::move(mac_addr));
        }
        else
        {
            auto addr_it = mac_addr_map.find(mac_addr);
            if (addr_it == mac_addr_map.end())
            {
                fprintf(stderr, "no aliase: %s found\n", mac_addr.c_str());
                return false;
            }

            std::replace(addr_it->second.begin(), addr_it->second.begin(), '-', ':');
            mac_addr_vec.emplace_back(addr_it->second);
        }
    }

    for (auto &mac_addr : mac_addr_vec)
    {
        auto packet = package_magic_data(mac_addr);
        for (auto &interface : interface_set)
        {
            int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (sock  < 0 )
            {
                fprintf(stderr, "cannot open socket, errno:%d, dsec:%s\n", errno, strerror(errno));
                return false;
            }

            int optval = 1;
            if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char *) &optval, sizeof(optval)) < 0)
            {
                fprintf(stderr, "cannot set socket options, errno:%d, desc:%s\n", errno, strerror(errno));
                return false;
            }

            struct ifreq req;
            memset(&req, 0, sizeof(req));
            strncpy(req.ifr_name, interface.c_str(), IFNAMSIZ);
            ioctl(sock, SIOCGIFINDEX, &req);

            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            if (inet_aton(bcast_addr.c_str(), &addr.sin_addr) == 0)
            {
                fprintf(stderr, "Invalid remote ip address given: %s\n", bcast_addr.c_str());
                return false;
            }

            if (sendto(sock, &packet[0], packet.size(), 0, (struct sockaddr *)&addr, sizeof(addr)) < 0)
            {
                fprintf(stderr, "cannot send data, errno:%d,  desc:%s\n", errno, strerror(errno));
                return false;
            }

            printf("Successful sent WOL magic packet to: %s by interface: %s\n", mac_addr.c_str(), interface.c_str());
        }
    }

    return true;
}

bool file_helper::open(const std::string &file_name, const int mode)
{
    fd_ = ::open(file_name.c_str(), mode, 0660);
    if (fd_ < 0)
    {
        fprintf(stderr, 
                "open file: %s failed, errno:%d, dsec:%s\n",
                file_name.c_str(),
                errno,
                strerror(errno));
        return false;
    }

    file_name_ = file_name;
    return true;
}

bool file_helper::read(std::string &data)
{
    struct stat st;
    if (fstat(fd_, &st) != 0)
    {
        fprintf(stderr, 
                "fstat file, errno:%d, dsec:%s\n",
                errno,
                strerror(errno));
        return false;
    }

    data.resize(st.st_size);
    decltype(st.st_size) pos = 0;
    while (pos < st.st_size)
    {
        auto ret = ::read(fd_, &data[pos], st.st_size - pos);
        if (ret < 0)
        {
            fprintf(stderr, 
                    "read file failed, errno:%d, dsec:%s\n",
                    errno,
                    strerror(errno));
            return false;
        }
        else if (ret == 0)
        {
            break;
        }

        pos += ret;
    }

    if (pos != st.st_size)
    {
        fprintf(stderr, "read file failed\n");
        return false;
    }

    return true;
}

bool file_helper::read(std::string &data, const std::size_t file_Size)
{
    data.resize(file_Size);
    std::size_t pos = 0;
    while (pos < file_Size)
    {
        auto ret = ::read(fd_, &data[pos], file_Size - pos);
        if (ret < 0)
        {
            fprintf(stderr, 
                    "read file failed, errno:%d, dsec:%s\n",
                    errno,
                    strerror(errno));
            return false;
        }
        else if (ret == 0)
        {
            break;
        }

        pos += ret;
    }

    return true;
}

bool file_helper::write(const std::string &data)
{
    if (data.empty())
    {
        return true;
    }

    std::size_t pos = 0;
    std::size_t data_size = data.size();
    while (pos < data_size)
    {
        auto ret = ::write(fd_, &data[pos], data_size - pos);
        if (ret < 0)
        {
            fprintf(stderr, 
                    "write file failed, errno:%d, dsec:%s\n",
                    errno,
                    strerror(errno));
            return false;
        }
        pos += ret;
    }
    
    return true;
}

bool file_helper::write_truncate_atomic(const std::string &new_data)
{
    file_helper write_tmp_file;
    do_on_exit do_exit([&write_tmp_file]()
    {
        auto file_name = write_tmp_file.file_name();
        if (access(file_name.c_str(), F_OK) != -1)
        {
            unlink(file_name.c_str());
        }
    });

    uint32_t i = 0;
    while (true)
    {
        auto tmp_file = file_name_;
        if (write_tmp_file.open(tmp_file.append(std::to_string(i)), O_RDWR | O_CREAT | O_EXCL))
        {
            break;
        }
    }
    
    if (!write_tmp_file.write(new_data))
    {
        return false;
    }

    if (fsync(write_tmp_file.fd()) != 0)
    {
        fprintf(stderr, 
                "rename file, errno:%d, dsec:%s\n",
                errno,
                strerror(errno));
        return false;
    }

    if (rename(write_tmp_file.file_name().c_str(), file_name_.c_str()) != 0)
    {
        fprintf(stderr, 
                "rename file, errno:%d, dsec:%s\n",
                errno,
                strerror(errno));
        return false;
    }

    return true;
}

file_helper::~file_helper()
{
    if (fd_ > 0)
    {
        close(fd_);
    }
}
