#ifndef SF1R_B5MMANAGER_B5MHELPER_H_
#define SF1R_B5MMANAGER_B5MHELPER_H_
#include <string>
#include <vector>
#include <boost/assign/list_of.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <common/ScdParser.h>
#include <common/Utilities.h>

namespace sf1r {

    struct B5MPropertyList
    {
        std::vector<std::string> value;

        B5MPropertyList(const std::string& env_name, const std::vector<std::string>& default_value)
        {
            char* env_char = getenv(env_name.c_str());
            if(env_char==NULL)
            {
                value = default_value;
            }
            else
            {
                std::string env_value(env_char);
                std::cout<<"get env "<<env_name<<" : <"<<env_value<<">"<<std::endl;
                boost::algorithm::split( value, env_value, boost::algorithm::is_any_of(",") );
            }
        }
    };

    class B5MHelper {
    public:
        static const B5MPropertyList B5MO_PROPERTY_LIST;
        static const B5MPropertyList B5MP_PROPERTY_LIST;
        static const B5MPropertyList B5MC_PROPERTY_LIST;

        static uint128_t StringToUint128(const std::string& str)
        {
            unsigned long long high = 0, low = 0;
            sscanf(str.c_str(), "%016llx%016llx", &high, &low);
            return (uint128_t) high << 64 | (uint128_t) low;
        }

        static std::string Uint128ToString(const uint128_t& val)
        {
            static char tmpstr[33];
            sprintf(tmpstr, "%016llx%016llx", (unsigned long long) (val >> 64), (unsigned long long) val);
            return std::string(reinterpret_cast<const char *>(tmpstr), 32);
        }

        static void GetScdList(const std::string& scd_path, std::vector<std::string>& scd_list)
        {
            namespace bfs = boost::filesystem;
            if(!bfs::exists(scd_path)) return;
            if( bfs::is_regular_file(scd_path) && ScdParser::checkSCDFormat(scd_path))
            {
                scd_list.push_back(scd_path);
            }
            else if(bfs::is_directory(scd_path))
            {
                bfs::path p(scd_path);
                bfs::directory_iterator end;
                for(bfs::directory_iterator it(p);it!=end;it++)
                {
                    if(bfs::is_regular_file(it->path()))
                    {
                        std::string file = it->path().string();
                        if(ScdParser::checkSCDFormat(file))
                        {
                            scd_list.push_back(file);
                        }
                    }
                }
            }
            std::sort(scd_list.begin(), scd_list.end(), ScdParser::compareSCD);
        }

        static void GetIScdList(const std::string& scd_path, std::vector<std::string>& scd_list)
        {
            GetScdList(scd_path, scd_list);
            std::vector<std::string> valid_scd_list;
            for(uint32_t i=0;i<scd_list.size();i++)
            {
                int scd_type = ScdParser::checkSCDType(scd_list[i]);
                if(scd_type==INSERT_SCD)
                {
                    valid_scd_list.push_back(scd_list[i]);
                }
            }
            scd_list.swap(valid_scd_list);
        }
    };

}

#endif

