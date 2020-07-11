#include <stdlib.h>
#include <stdio.h>
#include <map>
#include <string>
#include <regex>

static const std::map<std::string, std::string> s_g_cmd_map = {
    {"wake", "wakes up a machine by mac address or alias"},
    {"list", "lists all mac addresses and their aliases"},
    {"alias", "stores an alias to a mac address"},
    {"remove", "removes an alias or a mac address"}
};

static const std::map<std::string, std::string> s_g_opt_map = 
{
    {"h,help", "prints the help menu"},
    {"p,port", "udp port to send bcast packet to"},
    {"b,bcast", "broadcast IP to send packet to"},
    {"i,interface", "outbound interface to broadcast using"}
};

static std::map<std::string, std::string> parse_cmd(int argc, char **argv);
static void print_usage();
static void list_aliases();
static bool remove_alias(const std::string &alias);
static bool stores_alias(const std::string &alias, const std::string &mac);

int main(int argc, char **argv)
{
    
}

std::map<std::string, std::string> parse_cmd(int argc, char **argv)
{
    std::regex reg("^(-){0,2}(.)+$");

    std::map<std::string, std::string> result_map;
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
                result_map.emplace("port", argv[i+1]);
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
                result_map.emplace("bcast", argv[i+1]);
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
                result_map.emplace("interface", argv[i+1]);
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
            ;
        }

        ++i;
    }

}