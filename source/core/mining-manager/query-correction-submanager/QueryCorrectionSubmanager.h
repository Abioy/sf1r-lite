/**
 * @brief  Defination of QueryCorrectionSubmanager. If user inputs a wrong query, it can be used to get correct candidates and select the best one.
 * @author  Jinglei Zhao & Jinli Liu
 * @date Sep 15,2009
 * @version v3.0
 * ================
 * log:
 * 2009.02.23 Peisheng Wang
 *   --Using log-manager to enhance queryCorrection Manager
 *   --Integrate Chinese query correcion
 *
 *
 */
#ifndef _QUERY_CORRECTION_SUBMANAGER_H_
#define _QUERY_CORRECTION_SUBMANAGER_H_

#include <log-manager/LogManager.h>
#include <query-manager/QueryManager.h>
#include <idmlib/query-correction/cn_query_correction.h>
#include "EkQueryCorrection.h"
#include <boost/noncopyable.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/shared_ptr.hpp>
#include <sdb/SequentialDB.h>

namespace sf1r
{

class QueryCorrectionSubmanager : public boost::noncopyable
{
    typedef boost::tuple<uint32_t, uint32_t, izenelib::util::UString> QueryLogItemType;
    typedef std::list<QueryLogItemType> QueryLogListType;
    static const int DEFAULT_MAX_EDITDISTANCE_ = 2;

public:
    QueryCorrectionSubmanager(const std::string& queryDataPath, bool enableEK, bool enableChn, int ed = DEFAULT_MAX_EDITDISTANCE_);

    ~QueryCorrectionSubmanager();

    static QueryCorrectionSubmanager& getInstance();

    /**
     * @brief The main interface to refine a user query,the query can be set of tokens
     * @param queryUString         the inputed query of the user.
     * @param refinedQueryUString  the corrected query, if no correction is done, this string is returned empty.
     * @return true if success false if failed.
     */

    bool getRefinedQuery(const UString& queryUString, UString& refinedQueryUString);

    bool getPinyin(const izenelib::util::UString& hanzi,
                   std::vector<izenelib::util::UString>& pinyin);

    void updateCogramAndDict(const QueryLogListType& recentQueryList);

    void Inject(const izenelib::util::UString& query, const izenelib::util::UString& result);

    void FinishInject();

protected:
    /**
     * @brief Initialize some member variables
     * @return true if success false if failed.
     */
    bool initialize();

    bool getRefinedToken_(const izenelib::util::UString& token, izenelib::util::UString& result);

public:
    static const std::string& system_resource_path_;
    static const std::string& system_working_path_;

private:
    std::string queryDataPath_;
    bool enableEK_;
    bool enableChn_;
    int ed_;

    /**
     *  max edit distance used to canididae generation
     */
    bool activate_;

    boost::mutex logMutex_;

    boost::shared_ptr<idmlib::qc::CnQueryCorrection> cmgr_;

    //English or Korean Query CorrectionManager
    static boost::shared_ptr<EkQueryCorrection> ekmgr_;

    static boost::unordered_map<izenelib::util::UString, izenelib::util::UString> global_inject_data_;
    boost::unordered_map<izenelib::util::UString, izenelib::util::UString> collection_inject_data_;
    boost::unordered_map<izenelib::util::UString, izenelib::util::UString>& default_inject_data_;
    bool has_new_inject_;
};

}

#endif/*_QUERY_CORRECTION_SUBMANAGER_H_*/
