#include "SummarizationSubManager.h"
#include "SummarizationStorage.h"
#include "CommentCacheStorage.h"
#include "splm.h"

#include <document-manager/DocumentManager.h>
#include <la-manager/LAPool.h>

#include <log-manager/LogServerRequest.h>
#include <log-manager/LogServerConnection.h>

#include <common/ScdWriter.h>
#include <common/Utilities.h>
#include <idmlib/util/idm_analyzer.h>

#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>

#include <glog/logging.h>

#include <iostream>
#include <algorithm>
#include <boost/filesystem.hpp>
#include "OpinionsManager.h"
#include <am/succinct/wat_array/wat_array.hpp>
#include <node-manager/synchro/SynchroFactory.h>
#include <node-manager/DistributeRequestHooker.h>


#define OPINION_COMPUTE_THREAD_NUM 4
#define OPINION_COMPUTE_QUEUE_SIZE 2000

using izenelib::util::UString;
using namespace izenelib::ir::indexmanager;
namespace bfs = boost::filesystem;

namespace sf1r
{

static const std::string syncID_totalComment = "TOTAL_COMMENT";
static const char* SUMMARY_SCD_BACKUP_DIR = "summary_backup";
static const UString DOCID("DOCID", UString::UTF_8);
static const std::size_t COMPUTE_COMMENT_NUM = 4;

bool CheckParentKeyLogFormat(
        const SCDDocPtr& doc,
        const UString& parent_key_name)
{
    if (doc->size() != 2) return false;
    const UString first = UString((*doc)[0].first, UString::UTF_8);
    const UString second = UString((*doc)[1].first, UString::UTF_8);
    //FIXME case insensitive compare, but it requires extra string conversion,
    //which introduces unnecessary memory fragments
    return (first == DOCID && second == parent_key_name);
}

void SegmentToSentece(const UString& Segment, vector<std::pair<double,UString> >& Sentence)
{
    string temp ;
    Segment.convertString(temp, izenelib::util::UString::UTF_8);
    string dot=",";
    size_t templen = 0;
  
    while(!temp.empty())
    {
        size_t len1 = temp.find(",");
        size_t len2 = temp.find(".");
        if(len1 != string::npos || len2 != string::npos)
        {
            if(len1 == string::npos)
            {
                templen = len2;
            }
            else if(len2 == string::npos)
            {
                templen = len1;
            }
            else
            {
                templen = min(len1,len2);
            }
            if(temp.substr(0,templen).length()>0 && temp.substr(0,templen).length()<28)
            {
                Sentence.push_back(std::make_pair(1.0, UString(temp.substr(0,templen), UString::UTF_8)) );
            }
            temp = temp.substr(templen + dot.length());
        }
        else
        {   
            if(temp.length()>0&&temp.length()<28)
            {
               Sentence.push_back(std::make_pair(1.0, UString(temp, UString::UTF_8)));
            }
            break;
        }
    }
}

struct IsParentKeyFilterProperty
{
    const std::string& parent_key_property;

    IsParentKeyFilterProperty(const std::string& property)
        : parent_key_property(property)
    {}

    bool operator()(const QueryFiltering::FilteringType& filterType)
    {
        return boost::iequals(parent_key_property, filterType.property_);
    }
};


MultiDocSummarizationSubManager::MultiDocSummarizationSubManager(
        const std::string& homePath,
        const std::string& sys_res_path,
        const std::string& collectionName,
        const std::string& scdPath,
        SummarizeConfig schema,
        boost::shared_ptr<DocumentManager> document_manager,
        //boost::shared_ptr<IndexManager> index_manager,
        idmlib::util::IDMAnalyzer* analyzer)
    : is_rebuild_(false)
    , last_docid_path_(homePath + "/last_docid.txt")
    , total_scd_path_(scdPath + "/" + SUMMARY_SCD_BACKUP_DIR)
    , collectionName_(collectionName)
    , homePath_(homePath)
    , schema_(schema)
    , document_manager_(document_manager)
    //, index_manager_(index_manager)
    , analyzer_(analyzer)
    , comment_cache_storage_(new CommentCacheStorage(homePath))
    , summarization_storage_(new SummarizationStorage(homePath))
    , corpus_(new Corpus())
    , system_resource_path_(sys_res_path)
{
}

MultiDocSummarizationSubManager::~MultiDocSummarizationSubManager()
{
    delete summarization_storage_;
    delete comment_cache_storage_;
    delete corpus_;
}

void MultiDocSummarizationSubManager::dealTotalScd(const std::string& filename 
                                , const std::set<KeyType>& del_docid_set
                                , fstream& os)
{
    std::string ScdName_new = opinion_scd_writer_->GenSCDFileName(RTYPE_SCD);
    ofstream outNewScd;
    outNewScd.open((total_scd_path_ + "/" + ScdName_new).c_str(), ios::out|ios::app);
    if (!outNewScd.good())
    {
        LOG(ERROR) << "Disk is full..." <<endl; 
        return;
    }
    ScdParser parser;
    if (!parser.load(total_scd_path_ + "/" + filename))
    {
        os.open((total_scd_path_ + "/" + filename).c_str(), ios::out|ios::app);//for create file
        outNewScd.close();
        boost::filesystem::path outNewScdPath((total_scd_path_ + "/" + ScdName_new).c_str());
        boost::filesystem::remove(outNewScdPath);
    }
    else
    {
        if (del_docid_set.size() == 0)
        {
            LOG(INFO) << "No comments need to delete..."<<endl;
            os.open((total_scd_path_ + "/" + filename).c_str(), ios::out|ios::app);
            outNewScd.close();
            boost::filesystem::path new_path(total_scd_path_ + "/" + ScdName_new);
            boost::filesystem::remove(new_path);
            return;
        }
        for (ScdParser::iterator doc_iter = parser.begin();
                doc_iter != parser.end(); ++doc_iter)
        {
            if (*doc_iter == NULL)
            {
                LOG(WARNING) << "SCD File not valid.";
                return;
            }
            SCDDocPtr SCDdoc = (*doc_iter);
            SCDDoc::iterator p = SCDdoc->begin();
            for (; p != SCDdoc->end(); p++)//for eache line
            {
                const std::string& fieldStr = p->first;// preventing copy
                if (fieldStr == "DOCID")
                {
                    std::string key_str = propstr_to_str(p->second);
                    KeyType docid = Utilities::uuidToUint128(key_str);
                    if (del_docid_set.find(docid) != del_docid_set.end())
                        break;
                    outNewScd << "<" << fieldStr << ">" << key_str << endl;
                }
                else
                {
                    std::string content_str = propstr_to_str(p->second);
                    outNewScd << "<" << p->first << ">" << content_str << endl;
                }
            }
        }//add to NewScdFile;
        boost::filesystem::path old_path(total_scd_path_ + "/" + filename);
        boost::filesystem::path new_path(total_scd_path_ + "/" + ScdName_new);
        boost::filesystem::remove(old_path);
        boost::filesystem::rename(new_path, old_path);
        outNewScd.close();
        boost::filesystem::remove(new_path);
        os.open((total_scd_path_ + "/" + filename).c_str(), ios::out|ios::app);
    }
}

void MultiDocSummarizationSubManager::commentsClassify(int x)
{
    while(true)
    {
        int i = 0;
        Document doc;
        {
            boost::unique_lock<boost::mutex> g(waiting_opinion_lock_);

            while(docList_.empty())
            {
                if(can_quit_compute_)
                {
                    LOG(INFO) << "!!!---Classify thread:" << (long)pthread_self() << " finished. ---!!!" << endl;
                    return;
                }
                waiting_opinion_cond_.wait(g);
            }
            doc = docList_.front().first;
            i = docList_.front().second;
            docList_.pop();
        }
        Document::property_const_iterator kit = doc.findProperty(schema_.uuidPropName);
        if (kit == doc.propertyEnd()) continue;

        Document::property_const_iterator cit = doc.findProperty(schema_.contentPropName);
        if (cit == doc.propertyEnd()) continue;

        Document::property_const_iterator ait = doc.findProperty(schema_.advantagePropName);
        if (ait == doc.propertyEnd()) continue;

        Document::property_const_iterator dit = doc.findProperty(schema_.disadvantagePropName);
        if (dit == doc.propertyEnd()) continue;

        Document::property_const_iterator title_it = doc.findProperty(schema_.titlePropName);
        if (title_it == doc.propertyEnd()) continue;

        const Document::doc_prop_value_strtype& key = kit->second.getPropertyStrValue();
        if (key.empty()) continue;

        ContentType content ;

        std::string str = propstr_to_str(cit->second.getPropertyStrValue());
        std::pair<UString, UString> advantagepair;
        OpcList_[x]->Classify(str, advantagepair);

        AdvantageType advantage = advantagepair.first;
        DisadvantageType disadvantage = advantagepair.second;

        str = propstr_to_str(title_it->second.getPropertyStrValue());
        OpcList_[x]->Classify(str,advantagepair);

        if(advantage.find(advantagepair.first) == UString::npos)
        {
            advantage.append(advantagepair.first);
        }
        if(disadvantage.find(advantagepair.second) == UString::npos)
        {
            disadvantage.append(advantagepair.second);
        }
        str = propstr_to_str(ait->second.getPropertyStrValue());
        OpcList_[x]->Classify(str,advantagepair);
        if(advantage.find(advantagepair.first) == UString::npos)
        {
            advantage.append(advantagepair.first);
        }

        str = propstr_to_str(dit->second.getPropertyStrValue());
        OpcList_[x]->Classify(str,advantagepair);
        if(disadvantage.find(advantagepair.second) == UString::npos)
        disadvantage.append(advantagepair.second);
        float score = 0.0f;
        document_manager_->getNumericPropertyTable(schema_.scorePropName)->getFloatValue(i, score);
        {
            boost::unique_lock<boost::mutex> g(waiting_opinion_lock_);
            comment_cache_storage_->AppendUpdate(Utilities::md5ToUint128(key), i, content,
                advantage, disadvantage, score);
        }
    }
}

bool MultiDocSummarizationSubManager::buildDocument(docid_t docID, const Document& doc)
{
    boost::unique_lock<boost::mutex> g(waiting_opinion_lock_);
    while(docList_.size() > OPINION_COMPUTE_QUEUE_SIZE)
    {
        waiting_opinion_cond_.timed_wait(g, boost::posix_time::millisec(30));
    }
    docList_.push(std::make_pair(doc, docID));
    waiting_opinion_cond_.notify_one();
    return true;
}

bool MultiDocSummarizationSubManager::preProcess()
{
    check_rebuild();

    boost::filesystem::path totalscdPath(total_scd_path_);
    if (!boost::filesystem::exists(totalscdPath))
    {
        boost::filesystem::create_directory(totalscdPath);
    }
    std::string opinionScdName = "B-00-205001071530-00000-R-C.SCD";
    std::string scoreScdName = "B-00-205001071530-00001-R-C.SCD";
    
    std::vector<docid_t> del_docid_list;
    std::set<KeyType> del_key_set;
    document_manager_->getDeletedDocIdList(del_docid_list);
    for(unsigned int i = 0; i < del_docid_list.size();++i)
    {
        Document doc;
        bool b = document_manager_->getDocument(i, doc);
        if(!b) continue;
        Document::property_const_iterator kit = doc.findProperty(schema_.uuidPropName);
        if (kit == doc.propertyEnd()) continue;

        const Document::doc_prop_value_strtype& key = kit->second.getPropertyStrValue();
        if (key.empty()) continue;
        comment_cache_storage_->ExpelUpdate(Utilities::md5ToUint128(key), i);
        del_key_set.insert(Utilities::md5ToUint128(key));
    }

    dealTotalScd(opinionScdName, del_key_set, total_Opinion_Scd_);
    dealTotalScd(scoreScdName, del_key_set, total_Score_Scd_);

    std::string cma_path;
    LAPool::getInstance()->get_cma_path(cma_path);

    {
        boost::unique_lock<boost::mutex> g(waiting_opinion_lock_);
        can_quit_compute_ = false;
    }
    for (int i = 0; i < OPINION_COMPUTE_THREAD_NUM; ++i)
    {
        OpcList_.push_back(new OpinionsClassificationManager(cma_path, schema_.opinionWorkingPath));
    }
    
    for(int i = 0; i < OPINION_COMPUTE_THREAD_NUM; i++)
    {
        comment_classify_threads_.push_back(new boost::thread(&MultiDocSummarizationSubManager::commentsClassify, this, i));
    }
    return true;
}

bool MultiDocSummarizationSubManager::postProcess()
{
    LOG(INFO) << "Finish document iterator....";

    {
        boost::unique_lock<boost::mutex> g(waiting_opinion_lock_);
        can_quit_compute_ = true;
        waiting_opinion_cond_.notify_all();
    }

    for(int i = 0; i < OPINION_COMPUTE_THREAD_NUM; i++)
    {
        comment_classify_threads_[i]->timed_join(boost::posix_time::seconds(15));
    }

    LOG(INFO) << "All document iterator lines finished" ;
    
    for(int i = 0; i < OPINION_COMPUTE_THREAD_NUM; i++)
    {
        delete comment_classify_threads_[i];
    }
    comment_classify_threads_.clear();
    for (int i = 0; i < OPINION_COMPUTE_THREAD_NUM; ++i)
    {
        delete OpcList_[i];
    }
    OpcList_.clear();

    SetLastDocid_(document_manager_->getMaxDocId());
    comment_cache_storage_->Flush(true);

    string OpPath = schema_.opinionWorkingPath;
    boost::filesystem::path opPath(OpPath);
    if (!boost::filesystem::exists(opPath))
    {
        boost::filesystem::create_directory(opPath);
    }

    boost::filesystem::path generated_scds_path(OpPath + "/generated_scds");
    boost::filesystem::create_directory(generated_scds_path);

    score_scd_writer_.reset(new ScdWriter(generated_scds_path.c_str(), RTYPE_SCD));
    opinion_scd_writer_.reset(new ScdWriter(generated_scds_path.c_str(), RTYPE_SCD));

    {
        boost::unique_lock<boost::mutex> g(waiting_opinion_lock_);
        can_quit_compute_ = false;
    }

    std::vector<UString> filters;
    std::vector<UString> synonym_strs;
    LOG(INFO)<<"OpPath"<<OpPath<<endl;

    try
    {
        ifstream infile;
        infile.open((system_resource_path_ + "/opinion/opinion_filter_data.txt").c_str(), ios::in);
        while(infile.good())
        {
            std::string line;
            getline(infile, line);
            if(line.empty())
            {
                continue;
            }
            filters.push_back(UString(line, UString::UTF_8));
        }
        infile.close();
    }
    catch(...)
    {
        LOG(ERROR) << "read opinion filter file error" << endl;
    }

    try
    {
        ifstream infile;
        infile.open((system_resource_path_ + "/opinion/opinion_synonym_data.txt").c_str(), ios::in);
        while(infile.good())
        {
            std::string line;
            getline(infile, line);
            if(line.empty())
            {
                continue;
            }
            synonym_strs.push_back(UString(line, UString::UTF_8));
        }
        infile.close();
    }
    catch(...)
    {
        LOG(ERROR) << "read opinion synonym file error" << endl;
    }


    for(int i = 0; i < OPINION_COMPUTE_THREAD_NUM; i++)
    {
        std::string log_path = OpPath;
        log_path += "/opinion-log-";
        log_path.push_back('a' + i);
        boost::filesystem::path p(log_path);
        boost::filesystem::create_directory(p);

        std::string cma_path;
        LAPool::getInstance()->get_cma_path(cma_path);

        Ops_.push_back(new OpinionsManager( log_path, cma_path, OpPath));

        Ops_.back()->setSigma(0.1, 5, 0.6, 20);
        //////////////////////////
        Ops_.back()->setFilterStr(filters);
        Ops_.back()->setSynonymWord(synonym_strs);

        opinion_compute_threads_.push_back(new boost::thread(&MultiDocSummarizationSubManager::DoComputeOpinion,
                    this, Ops_[i]));
    }

    LOG(INFO) << "====== Evaluating summarization begin ======" << std::endl;
    {
        CommentCacheStorage::DirtyKeyIteratorType dirtyKeyIt(comment_cache_storage_->dirty_key_db_);
        CommentCacheStorage::DirtyKeyIteratorType dirtyKeyEnd;
        for (uint32_t count = 0; dirtyKeyIt != dirtyKeyEnd; ++dirtyKeyIt)
        {
            const KeyType& key = dirtyKeyIt->first;

            CommentCacheItemType commentCacheItem;
            comment_cache_storage_->Get(key, commentCacheItem);
            if (commentCacheItem.empty())
            {
                summarization_storage_->Delete(key);
                continue;
            }

            Summarization summarization(commentCacheItem);
            DoEvaluateSummarization_(summarization, key, commentCacheItem);

            DoOpinionExtraction(summarization, key, commentCacheItem);// add data for process....
            if (++count % 1000 == 0)
            {
                std::cout << "\r === Evaluating summarization and opinion count: " << count << " ===" << std::flush;
            }
        }

        {
            boost::unique_lock<boost::mutex> g(waiting_opinion_lock_);
            can_quit_compute_ = true;
            waiting_opinion_cond_.notify_all();
        }

        DoWriteOpinionResult();

        summarization_storage_->Flush();
    } //Destroy dirtyKeyIterator before clearing dirtyKeyDB

    LOG(INFO) << "====== Evaluating summarization end ======" << std::endl;
    for(int i = 0; i < OPINION_COMPUTE_THREAD_NUM; i++)
    {
        delete opinion_compute_threads_[i];
        delete Ops_[i];
    }
    Ops_.clear();
    opinion_compute_threads_.clear();

    if (score_scd_writer_)
    {
        score_scd_writer_->Close();
        score_scd_writer_.reset();
    }
    if (opinion_scd_writer_)
    {
        opinion_scd_writer_->Close();
        opinion_scd_writer_.reset();
    }

    comment_cache_storage_->ClearDirtyKey();
    //sync data;
   
    if (DistributeRequestHooker::get()->isRunningPrimary())
    {
        if (!is_rebuild_)
        {
            SynchroProducerPtr syncProducer = SynchroFactory::getProducer(schema_.opinionSyncId);
            SynchroData syncData;
            syncData.setValue(SynchroData::KEY_COLLECTION, collectionName_);
            syncData.setValue(SynchroData::KEY_DATA_TYPE, SynchroData::DATA_TYPE_SCD_INDEX);
            syncData.setValue(SynchroData::KEY_DATA_PATH, generated_scds_path.c_str());
            if (syncProducer->produce(syncData, boost::bind(boost::filesystem::remove_all, generated_scds_path.c_str())))
            {
                syncProducer->wait();
            }
            else
            {
                LOG(WARNING) << "produce incre syncData error";
            }
        }

        {
            SynchroProducerPtr syncProducer = SynchroFactory::getProducer(schema_.opinionSyncId + syncID_totalComment);
            SynchroData syncTotalData;
            syncTotalData.setValue(SynchroData::KEY_COLLECTION, collectionName_);

            syncTotalData.setValue(SynchroData::KEY_DATA_TYPE, SynchroData::TOTAL_COMMENT_SCD);
            syncTotalData.setValue(SynchroData::KEY_DATA_PATH, total_scd_path_.c_str());

            if (syncProducer->produce(syncTotalData))
            {
                syncProducer->wait();
            }
            else
            {
                LOG(WARNING) << "produce total syncData error";
            }
        }
    }
    total_Opinion_Scd_.close();
    total_Score_Scd_.close();
    LOG(INFO) << "Finish evaluating summarization.";
    return true;
}

docid_t MultiDocSummarizationSubManager::getLastDocId()
{
    return GetLastDocid_() + 1;
}

void MultiDocSummarizationSubManager::DoComputeOpinion(OpinionsManager* Op)
{
    LOG(INFO) << "opinion compute thread started : " << (long)pthread_self() << endl;
    int count = 0;
    while(true)
    {
        assert(Op != NULL);
        WaitingComputeCommentItem opinion_data;
        {
            boost::unique_lock<boost::mutex> g(waiting_opinion_lock_);
            while(waiting_opinion_comments_.empty())
            {
                if(can_quit_compute_)
                {
                    LOG(INFO) << "!!!---compute thread:" << (long)pthread_self() << " finished. ---!!!" << endl;
                    return;
                }
                waiting_opinion_cond_.wait(g);// this may waiting in all line;
            }
            opinion_data = waiting_opinion_comments_.front();
            waiting_opinion_comments_.pop();
        }
        if(opinion_data.cached_comments.empty())
            continue;

        // std::vector<UString> Z;
        std::vector<UString> advantage_comments;
        std::vector<UString> disadvantage_comments;
        //Z.reserve(opinion_data.cached_comments.size());
        advantage_comments.reserve(opinion_data.cached_comments.size());
        disadvantage_comments.reserve(opinion_data.cached_comments.size());
        for (CommentCacheItemType::const_iterator it = opinion_data.cached_comments.begin();
                it != opinion_data.cached_comments.end(); ++it)
        {

            //Z.push_back((it->second).content);
            advantage_comments.push_back(it->second.advantage);
            disadvantage_comments.push_back(it->second.disadvantage);
            
        }

        //Op->setComment(Z);
       
        std::vector< std::pair<double, UString> > product_opinions;// = Op->getOpinion();
        Op->setComment(advantage_comments);
        std::vector< std::pair<double, UString> > advantage_opinions = Op->getOpinion();
        Op->setComment(disadvantage_comments);
        std::vector< std::pair<double, UString> > disadvantage_opinions = Op->getOpinion();

        if(advantage_opinions.empty() && (!advantage_comments.empty()))
        {
            for(unsigned i = 0; i < min(advantage_comments.size(), COMPUTE_COMMENT_NUM);i++)
            {
                std::vector< std::pair<double, UString> > temp;
                SegmentToSentece(advantage_comments[i], temp);
                for (std::vector< std::pair<double, UString> >::iterator iter = temp.begin(); iter != temp.end(); ++iter)
                {
                    bool isIN = false;
                    for (unsigned int j = 0; j < advantage_opinions.size(); ++j)
                    {
                        if (advantage_opinions[j].second == iter->second)
                        {
                            isIN = true;
                            advantage_opinions[j].first += 1.0;
                            break;
                        }
                    }
                    if (!isIN)
                        advantage_opinions.push_back(*iter);
                }
                if(advantage_opinions.size() >= 4)
                    break;
            }
        }
        
        if(disadvantage_opinions.empty() && (!disadvantage_comments.empty()))
        {
            for(unsigned i = 0;i < min(disadvantage_comments.size(), COMPUTE_COMMENT_NUM); i++)
            {
                std::vector< std::pair<double, UString> > temp;
                SegmentToSentece(disadvantage_comments[i], temp);
                for (std::vector< std::pair<double, UString> >::iterator iter = temp.begin(); iter != temp.end(); ++iter)
                {
                    bool isIN = false;
                    for (unsigned int j = 0; j < disadvantage_opinions.size(); ++j)
                    {
                        if (disadvantage_opinions[j].second == iter->second)
                        {
                            isIN = true;
                            disadvantage_opinions[j].first += 1.0;
                            break;
                        }
                    }
                    if (!isIN)
                        disadvantage_opinions.push_back(*iter);
                }
                 if(disadvantage_opinions.size() > 4)
                    break;
            }
        }
        
        if((!advantage_opinions.empty())||(!disadvantage_opinions.empty()))
        {
            OpinionResultItem item;
            item.key = opinion_data.key;


            item.result_advantage = advantage_opinions;
            item.result_disadvantage = disadvantage_opinions;

            item.summarization.swap(opinion_data.summarization);
            boost::unique_lock<boost::mutex> g(opinion_results_lock_);
            opinion_results_.push(item);
            opinion_results_cond_.notify_one();
        }
        if (++count % 100 == 0)
        {
            cout << "\r == thread:" << (long)pthread_self() << " computing opinion count: " << count << " ===" << std::flush;
        }
    }
}

void MultiDocSummarizationSubManager::DoOpinionExtraction(
        Summarization& summarization,
        const KeyType& key,
        const CommentCacheItemType& comment_cache_item)
{
    WaitingComputeCommentItem item;
    item.key = key;
    item.cached_comments = comment_cache_item;
    item.summarization.swap(summarization);
    boost::unique_lock<boost::mutex> g(waiting_opinion_lock_);
    while(waiting_opinion_comments_.size() > OPINION_COMPUTE_QUEUE_SIZE)
    {
        waiting_opinion_cond_.timed_wait(g, boost::posix_time::millisec(500));//wait for time;
    }
    waiting_opinion_comments_.push(item);
    waiting_opinion_cond_.notify_one();//condition_ wake up 
}

void MultiDocSummarizationSubManager::DoWriteOpinionResult()
{
    int count = 0;
    while(true)
    {
        bool all_finished = false;
        OpinionResultItem result;
        {
            boost::unique_lock<boost::mutex> g(opinion_results_lock_);
            while(opinion_results_.empty())
            {
                // check if all thread finished.
                all_finished = true;
                for(size_t i = 0; i < opinion_compute_threads_.size(); ++i)
                {
                    if(!opinion_compute_threads_[i]->timed_join(boost::posix_time::millisec(1)))
                    {
                        if (opinion_compute_threads_[i]->get_id() == boost::thread::id())
                        {
                            LOG(INFO) << "timed_join returned for Not-Thread-Id";
                            continue;
                        }
                        if (!opinion_compute_threads_[i]->joinable())
                        {
                            LOG(INFO) << "timed_join returned for Not-Joinable";
                            continue;
                        }
                        // not finished
                        all_finished = false;
                        break;
                    }
                }
                if(!all_finished)
                {
                    opinion_results_cond_.timed_wait(g, boost::posix_time::millisec(5000));
                }
                else
                {
                    LOG(INFO) << "opinion result write finished." << endl;
                    return;
                }
            }
            result = opinion_results_.front();
            opinion_results_.pop();
        }
        if((!result.result_advantage.empty())||(!result.result_advantage.empty()))
        {
            if(!result.result_advantage.empty())
            {
                UString final_opinion_str = result.result_advantage[0].second;
                for(size_t i = 1; i < result.result_advantage.size(); ++i)
                {
                    final_opinion_str.append( UString(",", UString::UTF_8) );
                    final_opinion_str.append(result.result_advantage[i].second);
                }

                std::string key_str;
                key_str = Utilities::uint128ToUuid(result.key);

                if (opinion_scd_writer_)
                {
                    Document doc;
                    doc.property("DOCID") = str_to_propstr(key_str);
                    doc.property(schema_.opinionPropName) = ustr_to_propstr(final_opinion_str);
                    opinion_scd_writer_->Append(doc);
                }

                if ( total_Opinion_Scd_.good())
                {
                    total_Opinion_Scd_ << "<DOCID>" << key_str <<endl;
                    std::string content_str;
                    final_opinion_str.convertString(content_str, UString::UTF_8);
                    total_Opinion_Scd_ << "<" << schema_.opinionPropName << ">" << content_str <<endl;
                }
            }

            // result.summarization.updateProperty("overview", result.result_opinions);
            result.summarization.updateProperty("advantage", result.result_advantage);
            result.summarization.updateProperty("disadvantage", result.result_disadvantage);
            summarization_storage_->Update(result.key, result.summarization);

            if (++count % 1000 == 0)
            {
                cout << "\r== write opinion count: " << count << " ====" << std::flush;
            }
        }
    }
}

bool MultiDocSummarizationSubManager::DoEvaluateSummarization_(
        Summarization& summarization,
        const KeyType& key,
        const CommentCacheItemType& comment_cache_item)
{
#define MAX_SENT_COUNT 1000

    ScoreType total_score = 0;
    uint32_t count = 0;

    std::string key_str;
    key_str = Utilities::uint128ToUuid(key);

    for (CommentCacheItemType::const_iterator it = comment_cache_item.begin();
            it != comment_cache_item.end(); ++it)
    {
        if (it->second.score)
        {
            ++count;
            total_score += (it->second).score;
        }
    }
    if (count)
    {
        std::vector<std::pair<double, UString> > score_list(1);
        double avg_score = (double)total_score / (double)count;
        score_list[0].first = avg_score;
        summarization.updateProperty("avg_score", score_list);

        if (score_scd_writer_)
        {
            Document doc;
            doc.property("DOCID") = str_to_propstr(key_str);
            doc.property(schema_.scorePropName) = str_to_propstr(boost::lexical_cast<std::string>(avg_score), UString::UTF_8);
            if(!schema_.commentCountPropName.empty())
                doc.property(schema_.commentCountPropName) = str_to_propstr(boost::lexical_cast<std::string>(count));
            score_scd_writer_->Append(doc);
        }
        if (total_Score_Scd_.good())
        {
            total_Score_Scd_ << "<DOCID>" << key_str << endl;
            total_Score_Scd_ << "<" << schema_.scorePropName << ">" << boost::lexical_cast<std::string>(avg_score) << endl;
            if(!schema_.commentCountPropName.empty())
            {
                total_Score_Scd_ << "<" << schema_.commentCountPropName << ">" << boost::lexical_cast<std::string>(count) << endl;
            }
         }
    }
    return true;
}

bool MultiDocSummarizationSubManager::GetSummarizationByRawKey(
        const std::string& rawKey,
        Summarization& result)
{
    return summarization_storage_->Get(Utilities::uuidToUint128(rawKey), result);
}

//void MultiDocSummarizationSubManager::AppendSearchFilter(
//        std::vector<QueryFiltering::FilteringType>& filtingList)
//{
//    ///When search filter is based on ParentKey, get its associated values,
//    ///and add those values to filter conditions.
//    ///The typical situation of this happen when :
//    ///SELECT * FROM comments WHERE product_type="foo"
//    ///This hook will translate the semantic into:
//    ///SELECT * FROM comments WHERE product_id="1" OR product_id="2" ...
//
//    typedef std::vector<QueryFiltering::FilteringType>::iterator IteratorType;
//    IteratorType it = std::find_if(filtingList.begin(),
//            filtingList.end(), IsParentKeyFilterProperty(schema_.uuidPropName));
//    if (it != filtingList.end())
//    {
//        const std::vector<PropertyValue>& filterParam = it->values_;
//        if (!filterParam.empty())
//        {
//            try
//            {
//                const std::string& paramValue = get<std::string>(filterParam[0]);
//                KeyType param = Utilities::uuidToUint128(paramValue);
//
//                LogServerConnection& conn = LogServerConnection::instance();
//                GetDocidListRequest req;
//                UUID2DocidList resp;
//
//                req.param_.uuid_ = param;
//                conn.syncRequest(req, resp);
//                if (req.param_.uuid_ != resp.uuid_) return;
//
//                BTreeIndexerManager* pBTreeIndexer = index_manager_->getBTreeIndexer();
//                QueryFiltering::FilteringType filterRule;
//                filterRule.operation_ = QueryFiltering::INCLUDE;
//                filterRule.property_ = schema_.docidPropName;
//                for (std::vector<KeyType>::const_iterator rit = resp.docidList_.begin();
//                        rit != resp.docidList_.end(); ++rit)
//                {
//                    UString result(Utilities::uint128ToMD5(*rit), UString::UTF_8);
//                    if (pBTreeIndexer->seek(schema_.docidPropName, result))
//                    {
//                        ///Protection
//                        ///Or else, too many unexisted keys are added
//                        PropertyValue v(result);
//                        filterRule.values_.push_back(v);
//                    }
//                }
//                //filterRule.logic_ = QueryFiltering::OR;
//                filtingList.erase(it);
//                //it->logic_ = QueryFiltering::OR;
//                filtingList.push_back(filterRule);
//            }
//            catch (const boost::bad_get &)
//            {
//                filtingList.erase(it);
//                return;
//            }
//        }
//    }
//}

void MultiDocSummarizationSubManager::check_rebuild()
{
    std::ifstream ifs(last_docid_path_.c_str());

    if (!ifs) 
        is_rebuild_ = true;
    else
        is_rebuild_ = false;
}

uint32_t MultiDocSummarizationSubManager::GetLastDocid_() const
{
    std::ifstream ifs(last_docid_path_.c_str());

    if (!ifs) return 0;

    uint32_t docid;
    ifs >> docid;
    return docid;
}

void MultiDocSummarizationSubManager::SetLastDocid_(uint32_t docid) const
{
    std::ofstream ofs(last_docid_path_.c_str());

    if (ofs) ofs << docid;
}

}
