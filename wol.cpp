#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <map>
#include <string>
#include <regex>
#include <vector>
#include <sstream>

static std::string stores_file_path;

static void print_usage();
static void list_aliases();
static bool remove_alias(const std::string &alias);
static bool stores_alias(const std::string &alias, const std::string &mac);
static bool wake_alias(const std::map<std::string, std::string> &cmd_map, const std::vector<std::string> &wake_machine_vec);
static std::vector<std::string> split(const std::string &s, char delim);
static std::map<uint64_t, std::string> parse_mac_addr(const std::string &data);
static std::string mac_addr_to_str(const std::map<uint64_t, std::string> &mac_map);

constexpr uint32_t kMACSize = sizeof(uint64_t);
constexpr uint32_t kAliasSize = sizeof(uint16_t);
/*
   8 byte      2 byte    variable len   
 ______________________________________
|          |           |               |
| mac addr | alias len | variable data |
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

    bool write_atomic(const std::string &data);

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

int main(int argc, char **argv)
{
    auto pwd = getpwuid(getuid());
    if (pwd == nullptr)
    {
        fprintf(stderr, "getpwuid failed, errno:%d, dsec:%s", errno, strerror(errno));
        return 1;
    }

    stores_file_path = pwd->pw_dir;
    stores_file_path.append("/.config/wol.db");

    std::regex reg("^(-){0,2}(.)+$");
    std::vector<std::string> wake_machine_vec; 
    std::map<std::string, std::string> cmd_map;

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

    if (cmd_map.count("wake") > 0)
    {
        wake_alias(cmd_map, wake_machine_vec) ? exit(0) : exit(1);
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

static std::string mac_integer_to_str(uint64_t mac_addr)
{
    uint8_t array[6];
    //00:00:00:00:12:34:56:78
    for (int i = 6; i > 0; --i)
    {
        array[i] = mac_addr & 0xfful;
        mac_addr >>= 8;
    }

    std::string mac_str(18, 0);
    snprintf(&mac_str[0], mac_str.size(),
             "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
             array[0], array[1], array[2], array[3], array[4], array[5]);

    return mac_str;
}

static uint64_t mac_str_to_integer(const std::string &mac_addr)
{

}

static void print_usage()
{
    const char *usage = "Usage:\n"
    "   To wake up a machine:\n"
    "       wol wake <mac address | alias> <optional ...>\n"
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
    "   -v --version       prints the application version\n"
    "   -h --help          prints this help menu\n"
    "   -p --port          udp port to send bcast packet to\n"
    "   -b --bcast         broadcast IP to send packet to\n"
    "   -i --interface     outbound interface to broadcast using\n";
    
    printf("%s\n", usage);
}

static void list_aliases()
{
    file_helper file;
    if (!file.open(stores_file_path, O_RDONLY))
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
            printf("    %s  %s\n", item.first, item.second);
        }
    }
    else
    {

    }
}

static bool remove_alias(const std::string &alias)
{

}

static bool stores_alias(const std::string &alias, const std::string &mac)
{

}

static bool wake_alias(const std::map<std::string, std::string> &cmd_map, const std::vector<std::string> &wake_machine_vec)
{

}

file_helper::file_helper(/* args */)
{
}

file_helper::~file_helper()
{
}
