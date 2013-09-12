#include "OpinionsManager.h"
#include "OpinionTraining.h"
#include <common/CMAKnowledgeFactory.h>
#include <glog/logging.h>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#define VERY_LOW  -50
#define OPINION_NGRAM_MIN  2
#define OPINION_NGRAM_MAX  30
#define MAX_SEED_BIGRAM_RATIO   50
#define REFINE_THRESHOLD  4
#define MAX_SEED_BIGRAM_IN_SINGLE_COMMENT  100
#define RUBBISH_COMMENT_THRESHOLD  0.3
#define REASONABLE_MAX_LENGTH  15

using namespace std;
using namespace cma;

//static std::vector<uint32_t> result;
//static uint64_t total_times2 = 0;
//static uint64_t total_times3 = 0;
//static uint64_t total_times4 = 0;
//static int64_t total_gencandtime1 = 0;
//static int64_t total_gencandtime2 = 0;
//static int64_t total_gencandtime3 = 0;
namespace sf1r
{

typedef std::pair<OpinionsManager::WordStrType, int> seedPairT;
typedef OpinionsManager::OpinionCandStringT seedPair2T;

struct seedPairCmp {
    bool operator() (const seedPairT seedPair1,const seedPairT seedPair2)
    {
        return seedPair1.second > seedPair2.second;
    }

} seedPairCmp;

struct seedPairCmp2 {
    bool operator() (const seedPair2T seedPair1,const seedPair2T seedPair2)
    {
        return seedPair1.first.occurrence_num > seedPair2.first.occurrence_num;
    }

} seedPairCmp2;


static const UCS2Char  en_dou(','), en_ju('.'), en_gantan('!'),
             en_wen('?'), en_mao(':'), en_space(' ');
static const UCS2Char  ch_ju(0x3002), ch_dou(0xff0c), ch_gantan(0xff01), ch_wen(0xff1f), ch_mao(0xff1a), ch_dun(0x3001);
// 全角标点符号等
inline bool IsFullWidthChar(UCS2Char c)
{
    return c >= 0xff00 && c <= 0xffef;
}

inline bool isNonSenceCharForRight(UCS2Char c)
{
    static const UCS2Char ch_a = UString("啊", UString::UTF_8)[0];
    static const UCS2Char ch_o = UString("哦", UString::UTF_8)[0];
    static const UCS2Char ch_ba = UString("吧", UString::UTF_8)[0];
    static const UCS2Char ch_ne = UString("呢", UString::UTF_8)[0];
    static const UCS2Char ch_la = UString("啦", UString::UTF_8)[0];
    static const UCS2Char ch_de = UString("的", UString::UTF_8)[0];
    static const UCS2Char ch_ya = UString("呀", UString::UTF_8)[0];
    static const UCS2Char ch_ha = UString("哈", UString::UTF_8)[0];
    static const UCS2Char ch_liao = UString("了", UString::UTF_8)[0];
    return c == ch_a || c == ch_o ||
        c ==  ch_ba || c == ch_ne ||
        c == ch_la || c == ch_de ||
        c == ch_ya || c == ch_ha || c == ch_liao;
}

inline bool IsCJKSymbols(UCS2Char c)
{
    return c >= 0x3000 && c <= 0x303f;
}

inline bool IsCommentSplitChar(UCS2Char c)
{
    return c == en_dou || c == en_ju || c==en_gantan ||
        c==en_wen || c==en_mao || c == en_space ||
        IsFullWidthChar(c) || IsCJKSymbols(c);
}

inline bool IsNeedIgnoreChar(UCS2Char c)
{
    if(IsCommentSplitChar(c))
        return false;
    return !izenelib::util::UString::isThisChineseChar(c) &&
        !izenelib::util::UString::isThisDigitChar(c) &&
        !izenelib::util::UString::isThisAlphaChar(c);
}

inline bool IsCommentSplitStr(const UString& ustr)
{
    for(size_t i = 0; i < ustr.length(); ++i)
    {
        if(IsCommentSplitChar(ustr[i]))
            return true;
    }
    return false;
}

inline bool IsAllOpinionStr(const UString& ustr)
{
    for(size_t i = 0; i < ustr.length(); ++i)
    {
        if( !izenelib::util::UString::isThisChineseChar(ustr[i]) &&
            !izenelib::util::UString::isThisDigitChar(ustr[i]) &&
            !izenelib::util::UString::isThisAlphaChar(ustr[i]))
            return false;
    }
    return true;
}

inline bool IsAllChineseStr(const UString& ustr)
{
    for(size_t i = 0; i < ustr.length(); ++i)
    {
        if(!izenelib::util::UString::isThisChineseChar(ustr[i]))
            return false;
    }
    return true;
}
inline uint32_t FindInsertionPos(const std::vector<uint32_t>& wi_pos,
        const std::vector<uint32_t>& wj_pos, std::vector<uint32_t>& ret_pos)
{
    size_t index_i = 0;
    size_t index_j = 0;
    while(index_i < wi_pos.size() && index_j < wj_pos.size())
    {
        uint32_t pos_i = wi_pos[index_i];
        uint32_t pos_j = wj_pos[index_j];
        if(pos_i > pos_j)
        {
            index_j++;
        }
        else if(pos_i < pos_j)
        {
            index_i++;
        }
        else
        {
            ret_pos.push_back(pos_i);
            index_i++;
            index_j++;
        }
    }
    return ret_pos.size();
}

inline uint32_t FindInsertionPos(const std::vector<uint32_t>& wi_pos,
        const std::vector<uint32_t>& wj_pos)
{
    size_t index_i = 0;
    size_t index_j = 0;
    uint32_t posnum = 0;
    while(index_i < wi_pos.size() && index_j < wj_pos.size())
    {
        uint32_t pos_i = wi_pos[index_i];
        uint32_t pos_j = wj_pos[index_j];
        if(pos_i > pos_j)
        {
            index_j++;
        }
        else if(pos_i < pos_j)
        {
            index_i++;
        }
        else
        {
            posnum++;
            index_i++;
            index_j++;
        }
    }
    return posnum;
}

void OpinionsManager::StripRightForNonSence(UString& ustr)
{
    WordStrType::iterator index = ustr.begin();
    while(ustr.length() > 0 && index != ustr.end())
    {
        if (index + 1 == ustr.end())
        {
            if (isNonSenceCharForRight(*index))
            {
                ustr.erase(index, ustr.end());
            }
            break;
        }
        else if (isNonSenceCharForRight(*index) &&
            (IsCommentSplitChar(*(index + 1)) || isNonSenceCharForRight(*(index + 1))))
        {
            index = ustr.erase(index, index + 1);
            continue;
        }
        ++index;
    }
}

inline bool isAdjective(int pos)
{
    return pos >= 1 && pos <= 4;
}
inline bool isNoun(int pos)
{
    return (pos >= 19 && pos <= 25) || pos == 29;
}

OpinionsManager::OpinionsManager(const string& log_dir, const std::string& dictpath,
        const string& training_data_path, const std::string& training_res_dir)
{
    //knowledge_ = CMA_Factory::instance()->createKnowledge();
    //knowledge_->loadModel( "utf8", dictpath.c_str());
    knowledge_ = CMAKnowledgeFactory::Get()->GetKnowledge(dictpath);

    //assert(knowledge_->isSupportPOS());
    analyzer_ = CMA_Factory::instance()->createAnalyzer();
    //analyzer->setOption(Analyzer::OPTION_TYPE_POS_TAGGING,0);
    //analyzer->setOption(Analyzer::OPTION_ANALYSIS_TYPE,77);
    if (!knowledge_->isSupportPOS())
    {
        LOG(ERROR) << "the knowledge has no POS support.";
    }
    analyzer_->setKnowledge(knowledge_);
    //analyzer->setPOSDelimiter(posDelimiter.data());
    training_data_ = new OpinionTraining(training_res_dir + "/opinion", training_data_path);
    training_data_->LoadFile();
    string logpath = log_dir + "/OpinionsManager.log";
    out.open(logpath.c_str(), ios::out);
    windowsize = 3;
    encodingType_ = UString::UTF_8;
    SigmaRep = 0.1;
    SigmaRead = 5;
    SigmaSim = 0.5;
    SigmaLength = 20;
    //
    try
    {
        ifstream infile;
        infile.open((training_res_dir + "/opinion/opinion_filter_any.txt").c_str(), ios::in);
        while(infile.good())
        {
            std::string line;
            getline(infile, line);
            if(line.empty())
            {
                continue;
            }
            any_filter_strs_.push_back(UString(line, UString::UTF_8));
        }
        infile.close();
    }
    catch(...)
    {
        LOG(ERROR) << "read opinion any filter file error" << endl;
    }
}

OpinionsManager::~OpinionsManager()
{
    out.close();
    //delete knowledge_;
    delete analyzer_;
    delete training_data_;
}

void OpinionsManager::setWindowsize(int C)
{
    windowsize=C;
}

void OpinionsManager::setEncoding(izenelib::util::UString::EncodingType encoding)
{
    encodingType_ = encoding;
}

void OpinionsManager::setFilterStr(const WordSegContainerT& filter_strs)
{
    filter_strs_ = filter_strs;
}

void OpinionsManager::setSynonymWord(WordSegContainerT& synonyms)
{
    synonym_map_.clear();
    // each line seperated by ', ' is synonym words.
    for(size_t i = 0; i < synonyms.size(); ++i)
    {
        WordSegContainerT synonym_vec;
        boost::algorithm::split(synonym_vec, synonyms[i], boost::is_any_of(","));
        for(size_t j = 0; j < synonym_vec.size(); ++j)
        {
            synonym_map_[synonym_vec[j]] = synonym_vec[0];
        }
    }
}

void OpinionsManager::CleanCacheData()
{
    pmi_cache_hit_num_ = 0;
    word_cache_hit_num_ = 0;
    valid_cache_hit_num_ = 0;
    cached_word_insentence_.clear();
    cached_word_inngram_.clear();
    cached_pmimodified_.clear();
    cached_valid_ngrams_.clear();
    cached_srep.clear();
    SigmaRep_dynamic = CandidateSrepQueueT();
    SigmaRep_dynamic.push(VERY_LOW);
    Z.clear();
    orig_comments_.clear();
    begin_bigrams_.clear();
    end_bigrams_.clear();
    sentence_offset_.clear();
}

bool OpinionsManager::IsRubbishComment(const WordSegContainerT& words)
{
    std::set<WordStrType> diff_words;
    for(size_t i = 0; i < words.size(); ++i)
        diff_words.insert(words[i]);
    return (double)diff_words.size()/(double)words.size() < RUBBISH_COMMENT_THRESHOLD;
}

void OpinionsManager::AppendStringToIDArray(const WordStrType& s, std::vector<uint32_t>& word_ids)
{
    for(size_t i = 0; i < s.length(); ++i)
    {
        uint32_t id = s[i];
        word_ids.push_back(id);
    }
}

void OpinionsManager::RecordCoOccurrence(const WordStrType& s, size_t& curren_offset)
{
    if(s.length() <= (size_t)windowsize)
    {
        for(size_t j = 0; j < s.length(); ++j)
        {
            if(cached_word_inngram_[s[j]].empty() || cached_word_inngram_[s[j]].back() != curren_offset)
                cached_word_inngram_[s[j]].push_back(curren_offset);
        }
        ++curren_offset;
    }
    else
    {
        for(size_t i = 0; i < s.length() - (size_t)windowsize; ++i)
        {
            for(size_t j = i; j <= (size_t)windowsize; ++j)
            {
                if(cached_word_inngram_[s[j]].empty() || cached_word_inngram_[s[j]].back() != curren_offset)
                    cached_word_inngram_[s[j]].push_back(curren_offset);
            }
            ++curren_offset;
        }
    }
}

void OpinionsManager::setComment(const SentenceContainerT& in_sentences)
{
    CleanCacheData();

    struct timeval starttime, endtime;
    gettimeofday(&starttime, NULL);

    std::vector<uint32_t>  word_ids;
    size_t curren_offset = 0;
    cached_word_insentence_.rehash( min((size_t)65536*2, in_sentences.size()*100) );
    cached_word_insentence_.max_load_factor(0.6);
    cached_word_inngram_.resize( 65536 );
    cached_pmimodified_.rehash( min((size_t)65536*2, in_sentences.size()*100) );
    cached_pmimodified_.max_load_factor(0.6);
    begin_bigrams_.rehash( in_sentences.size() * 10);
    begin_bigrams_.max_load_factor(0.6);
    end_bigrams_.rehash( in_sentences.size() * 10);
    end_bigrams_.max_load_factor(0.6);
    for(size_t i = 0; i < in_sentences.size(); i++)
    {
        if(Z.size() >= MAX_COMMENT_NUM)
            break;
        // not thread-safe to append to leveldb, so disable dynamic append.
        //training_data_->AppendSentence(in_sentences);
        WordStrType ustr = in_sentences[i];
        WordStrType::iterator uend = std::remove_if(ustr.begin(), ustr.end(), IsNeedIgnoreChar);
        if(uend == ustr.begin())
            continue;
        ustr.erase(uend, ustr.end());
        StripRightForNonSence(ustr);

        WordSegContainerT allwords;
        stringToWordVector(ustr, allwords);

        if(IsRubbishComment(allwords))
        {
            out << "rubbish comment filtered: " << getSentence(allwords) << endl;
            continue;
        }
        orig_comments_.push_back(allwords);
        // remove splitter in the sentence, avoid the impact on the srep by them.
        //
        uend = std::remove_if(ustr.begin(), ustr.end(), IsCommentSplitChar);
        if(uend == ustr.begin())
            continue;
        ustr.erase(uend, ustr.end());
        Z.push_back(ustr);
        RecordCoOccurrence(ustr, curren_offset);
        for(size_t j = 0; j < ustr.length(); j++)
        {
            cached_word_insentence_[ustr.substr(j, 1)].push_back(Z.size() - 1);
            cached_word_insentence_[ustr.substr(j, 2)].push_back(Z.size() - 1);
        }
    }
    out << "----- total comment num : " << Z.size() << endl;
    gettimeofday(&endtime, NULL);
    int64_t interval = (endtime.tv_sec - starttime.tv_sec)*1000 + (endtime.tv_usec - starttime.tv_usec)/1000;
    if(interval > 10)
        out << "set comment time(ms):" << interval << endl;
    out.flush();
}

void OpinionsManager::setSigma(double SigmaRep_,double SigmaRead_,double SigmaSim_,double SigmaLength_)
{
    SigmaRep=SigmaRep_;
    SigmaRead=SigmaRead_;
    SigmaSim=SigmaSim_;
    SigmaLength=SigmaLength_;
    out<<"SigmaRep:"<<SigmaRep<<endl;
    out<<"SigmaRead:"<<SigmaRead<<endl;
    out<<"SigmaSim:"<<SigmaSim<<endl;
    out<<"SigmaLength:"<<SigmaLength<<endl;
}

void OpinionsManager::stringToWordVector(const WordStrType& ustr, SentenceContainerT& words)
{
    size_t len = ustr.length();
    words.reserve(words.size() + len);
    for(size_t i = 0; i < len; i++)
    {
        words.push_back(ustr.substr(i, 1));
    }
    //分词//TODO
}

void OpinionsManager::WordVectorToString(WordStrType& Mi,const WordSegContainerT& words)
{
    Mi.clear();
    for(size_t i = 0; i < words.size(); i++)
    {
        Mi.append(words[i]);
    }
}

bool OpinionsManager::hasAdjectiveAndNoun(const WordStrType& phrase)
{
    std::string phrase_str;
    phrase.convertString(phrase_str, encodingType_);
    cma::Sentence single_sen(phrase_str.c_str());
    bool hasAdj = false;
    bool hasNoun = false;
    int res = analyzer_->runWithSentence(single_sen);
    if (res == 1)
    {
        int best = single_sen.getOneBestIndex();
        if (single_sen.getCount(best) > 1)
        {
            for(int i = 0; i < single_sen.getCount(best); i++)
            {
                int pos = single_sen.getPOS(best, i);
                if (isAdjective(pos))
                {
                    hasAdj = true;
                }
                else if (isNoun(pos))
                {
                    hasNoun = true;
                }
                if (hasAdj && hasNoun)
                    return true;
            }
        }
    }
    return false;
}

bool OpinionsManager::hasAdjectiveOrNoun(const WordStrType& phrase)
{
    std::string phrase_str;
    phrase.convertString(phrase_str, encodingType_);
    cma::Sentence single_sen(phrase_str.c_str());
    int res = analyzer_->runWithSentence(single_sen);
    if (res == 1)
    {
        int best = single_sen.getOneBestIndex();
        if (single_sen.getCount(best) > 1)
        {
            for(int i = 0; i < single_sen.getCount(best); i++)
            {
                int pos = single_sen.getPOS(best, i);
                if (isAdjective(pos) || isNoun(pos))
                {
                    return true;
                }
            }
        }
    }
    return false;
}

double OpinionsManager::getTopicScore(const WordStrType& phrase,
    const WordSegContainerT& words, size_t check_step)
{
    if (check_step > words.size())
        return 1;

    double joinpossib = Possib(phrase)/(double)Z.size();
    double ret = VERY_LOW;
    if(joinpossib > 0)
    {
        double appear_alone = 1;
        int seg_num = 1;
        double max_alone = 0;
        for (size_t i = 0; i < words.size() - check_step; i = i + check_step)
        {
            WordStrType tmp_phrase;
            for (size_t j = 0; j < check_step; ++j)
            {
                tmp_phrase += words[i + j];
            }
            double alone = (double)1/Z.size() + (double)Possib(tmp_phrase)/Z.size() - joinpossib;
            appear_alone *= alone;
            seg_num++;
            max_alone = std::max(max_alone, alone);
        }
        if (seg_num > 3 && max_alone > 0)
        {
            appear_alone /= max_alone;
            --seg_num;
        }
        // we make sure the phrase AB will have high score if AB is :
        // 1. AB always appear in the same window
        // 2. A or B appear alone very unlikely.
        ret  = log((double)Z.size() * joinpossib / appear_alone / seg_num) / log(2);
        if(ret < (double)VERY_LOW)
        {
            ret = (double)VERY_LOW;
        }
    }

    if (phrase.length() > 4 && ret > 0)
    {
        if (hasAdjectiveAndNoun(phrase))
        {
            if (phrase.length() < REASONABLE_MAX_LENGTH)
                ret *= 2*(double)REASONABLE_MAX_LENGTH/phrase.length();
            //std::string phrase_str;
            //phrase.convertString(phrase_str, encodingType_);
            //out << "a phrase has both adjective and noun: " << phrase_str << ", score: " << ret << std::endl;
        }
        else 
        {
            if (phrase.length() > REASONABLE_MAX_LENGTH)
                ret /= 2*(double)phrase.length()/REASONABLE_MAX_LENGTH;
        }
    }

    return ret;
}

double OpinionsManager::SrepSentence(const UString& phrase_ustr)
{
    // do not use cache. cache is computed by single words.
    std::string phrase_str;
    phrase_ustr.convertString(phrase_str, encodingType_);
    WordSegContainerT words;
    cma::Sentence single_sen(phrase_str.c_str());
    analyzer_->runWithSentence(single_sen);
    int best = single_sen.getOneBestIndex();
    for(int i = 0; i < single_sen.getCount(best); i++)
    {
        words.push_back(WordStrType(single_sen.getLexicon(best, i), encodingType_));
    }

    return getTopicScore(phrase_ustr, words, 1);

    size_t n = words.size();
    double sigmaPMI = 0;
    for(size_t i = 0; i < n; i++)
    {
        sigmaPMI += PMIlocal(words, i, windowsize*2);
    }
    sigmaPMI /= double(n);

    return sigmaPMI + getTopicScore(phrase_ustr, words, 1);
}

double OpinionsManager::Srep(const WordSegContainerT& words)
{
    if(words.empty())
        return (double)VERY_LOW;


    WordStrType phrase;
    WordVectorToString(phrase, words);
    CachedStorageT::iterator it = cached_srep.find(phrase);
    if(it != cached_srep.end())
    {
        pmi_cache_hit_num_++;
        return it->second;
    }

    double ret = getTopicScore(phrase, words, 2);
    cached_srep[phrase] = ret;
    return ret;

    //struct timeval starttime, endtime;
    //int64_t interval;
    //gettimeofday(&starttime, NULL);
    size_t n = words.size();
    double sigmaPMI = 0;
    for(size_t i = 0; i < n; i++)
    {
        sigmaPMI += PMIlocal(words, i, windowsize);
    }
    sigmaPMI /= double(n);
    sigmaPMI += getTopicScore(phrase, words, 2);
    if (sigmaPMI < (double)VERY_LOW)
        sigmaPMI = VERY_LOW;
    cached_srep[phrase] = sigmaPMI;
    //gettimeofday(&endtime, NULL);
    //interval = (endtime.tv_sec - starttime.tv_sec)*1000000 + (endtime.tv_usec - starttime.tv_usec);
    //total_gencandtime1 += interval;
    //if(interval > 1000)
    //    out << "compute srep >100us 2: " << interval << endl;
    return sigmaPMI;
}

double OpinionsManager::Score(const NgramPhraseT& words)
{
    return  Srep(words) /*+ 0.25*Possib(phrase)*log(Possib(phrase)/XXX)*/;
}

double OpinionsManager::Sim(const WordStrType& Mi, const WordStrType& Mj)
{
    if(Mi.find(Mj) != WordStrType::npos ||
            Mj.find(Mi) != WordStrType::npos)
    {
        return 0.9;
    }

    WordSegContainerT wordsi, wordsj;
    stringToWordVector(Mi, wordsi);
    stringToWordVector(Mj, wordsj);
    return Sim(wordsi, wordsj);
}

double OpinionsManager::SimSentence(const WordStrType& sentence_i, const WordStrType& sentence_j)
{
    if(sentence_i.find(sentence_j) != WordStrType::npos ||
            sentence_j.find(sentence_i) != WordStrType::npos)
    {
        return 0.9;
    }

    WordSegContainerT wordsi, wordsj;
    std::string phrase_str;
    sentence_i.convertString(phrase_str, encodingType_);
    cma::Sentence single_sen_i(phrase_str.c_str());
    analyzer_->runWithSentence(single_sen_i);
    int best = single_sen_i.getOneBestIndex();
    for(int i = 0; i < single_sen_i.getCount(best); i++)
    {
        wordsi.push_back(WordStrType(single_sen_i.getLexicon(best, i), encodingType_));
        if(synonym_map_.find(wordsi.back()) != synonym_map_.end())
            wordsi.back() = synonym_map_[wordsi.back()];
    }

    phrase_str.clear();
    sentence_j.convertString(phrase_str, encodingType_);
    cma::Sentence single_sen_j(phrase_str.c_str());
    analyzer_->runWithSentence(single_sen_j);
    best = single_sen_j.getOneBestIndex();
    for(int i = 0; i < single_sen_j.getCount(best); i++)
    {
        wordsj.push_back(WordStrType(single_sen_j.getLexicon(best, i), encodingType_));
        if(synonym_map_.find(wordsj.back()) != synonym_map_.end())
            wordsj.back() = synonym_map_[wordsj.back()];
    }

    size_t sizei = wordsi.size();
    size_t sizej = wordsj.size();
    size_t same = 0;

    size_t total_diff_size = 0;
    std::map< WordStrType, int >  words_hash;
    for(size_t i = 0; i < sizei; ++i)
    {
        words_hash[wordsi[i]] = 1;
    }
    total_diff_size = words_hash.size();
    std::map< WordStrType, int >  words_hash_diff;
    for(size_t j = 0; j < sizej; j++)
    {
        if(words_hash.find(wordsj[j]) != words_hash.end())
        {
            if(words_hash[wordsj[j]] == 1)
            {
                words_hash[wordsj[j]] += 1;
                same++;
            }
        }
        else
        {
            words_hash_diff[wordsj[j]] = 1;
        }
    }
    total_diff_size += words_hash_diff.size();
    if (total_diff_size == 0)
        return 1;
    return double(same)/total_diff_size;//Jaccard similarity
}

double OpinionsManager::Sim(const NgramPhraseT& wordsi, const NgramPhraseT& wordsj)
{
    size_t sizei = wordsi.size();
    size_t sizej = wordsj.size();
    size_t same = 0;
    if( sizei <= 2 && sizej <= 2 )
    {

        for(size_t i = 0; i < sizei && i < sizej; i++)
            if(wordsi[i] == wordsj[i])
            {
                same++;
            }
        return double(same)/double(sizei + sizej - same);
    }

    WordStrType Mi, Mj;
    WordVectorToString(Mi, wordsi);
    WordVectorToString(Mj, wordsj);
    if(Mi.find(Mj) != WordStrType::npos ||
            Mj.find(Mi) != WordStrType::npos)
    {
        return 0.9;
    }


    std::map< WordStrType, int >  words_hash;
    for(size_t i = 0; i < sizei; ++i)
    {
        words_hash[wordsi[i]] = 1;
    }

    for(size_t j = 0; j < sizej; j++)
    {
        if(words_hash.find(wordsj[j]) != words_hash.end())
        {
            if(words_hash[wordsj[j]] == 1)
            {
                words_hash[wordsj[j]] += 1;
                same++;
            }
        }
    }
    return double(same)/(double(sizei) + double(sizej) - double(same));//Jaccard similarity
}

//rep
double OpinionsManager::PMIlocal(const WordSegContainerT& words,
        const int& offset, int C) //C=window size
{
    double Spmi = 0;
    int size = words.size();
    int start = max(0, offset - C);
    int end = min(size, offset + C);
    for(int i = start; i < end; i++)
    {
        if(i != offset)
        {
            Spmi += PMImodified(words[offset], words[i], C);
        }
    }
    return Spmi/double(2*C);
}

double OpinionsManager::PMImodified(const WordStrType& Wi, const WordStrType& Wj, int C)
{
    WordJoinPossibilityMapT::iterator join_it = cached_pmimodified_.find(Wi);
    if( join_it != cached_pmimodified_.end() )
    {
        WordPossibilityMapT::iterator pit = join_it->second.find(Wj);
        if(pit != join_it->second.end())
        {
            pmi_cache_hit_num_++;
            return pit->second;
        }
    }
    join_it = cached_pmimodified_.find(Wj);
    if( join_it != cached_pmimodified_.end() )
    {
        WordPossibilityMapT::iterator pit = join_it->second.find(Wi);
        if(pit != join_it->second.end())
        {
            pmi_cache_hit_num_++;
            return pit->second;
        }
    }

    int s = Z.size();
    double possib_i = Possib(Wi);
    double possib_j = Possib(Wj);
    double joinpossib = Possib(Wi, Wj);
    double ret = VERY_LOW;
    if(joinpossib > 1)
    {
        // we make sure the phrase AB will have high score if AB is :
        // 1. AB always appear in the same window
        // 2. A or B appear alone very unlikely.
        ret  = log(CoOccurring(Wi,Wj,C)*double(s) /
            ((1 + possib_i - joinpossib)*(1 + possib_j - joinpossib))) / log(2);
        if(ret < (double)VERY_LOW)
        {
            ret = (double)VERY_LOW;
        }
    }
    if(cached_pmimodified_[Wi].size() == 0)
    {
        cached_pmimodified_[Wi].rehash( min((size_t)65536*2, Z.size()*100) );
        cached_pmimodified_[Wi].max_load_factor(0.6);
    }

    cached_pmimodified_[Wi][Wj] = ret;
    return ret;
}

double OpinionsManager::CoOccurring(const WordStrType& Wi, const WordStrType& Wj, int C)
{
    int Poss = 0;
    if(Wi.length() == Wj.length() && Wi.length() == 1)
    {
        if(cached_word_inngram_[Wi[0]].empty())
            return 0;
        if(cached_word_inngram_[Wj[0]].empty())
            return 0;
        const std::vector<uint32_t>&  wi_pos = cached_word_inngram_[Wi[0]];
        const std::vector<uint32_t>&  wj_pos = cached_word_inngram_[Wj[0]];
        Poss = FindInsertionPos(wi_pos, wj_pos);
    }
    else
    {
        std::vector<uint32_t> all_possible_sentence;
        if(cached_word_insentence_.find(Wi) != cached_word_insentence_.end()
                && cached_word_insentence_.find(Wj) != cached_word_insentence_.end())
        {
            const std::vector<uint32_t>& wi_pos = cached_word_insentence_[Wi];
            const std::vector<uint32_t>& wj_pos = cached_word_insentence_[Wj];
            FindInsertionPos(wi_pos, wj_pos, all_possible_sentence);
            if(all_possible_sentence.empty())
            {
                return 0;
            }
        }
        else
        {
            for(size_t i = 0; i < Z.size(); ++i)
                all_possible_sentence.push_back(i);
        }
        for(size_t j = 0; j < all_possible_sentence.size(); j++)
        {
            if(CoOccurringInOneSentence(Wi, Wj, C, all_possible_sentence[j]))
                Poss++;
        }
    }

    return Poss*Poss;
}

bool OpinionsManager::CoOccurringInOneSentence(const WordStrType& Wi,
        const WordStrType& Wj, int C, int sentence_index)
{
    const UString& ustr = Z[sentence_index];
    const UString& uwi = Wi;
    const UString& uwj = Wj;

    size_t loci = ustr.find(uwi);
    while(loci != UString::npos)
    {
        size_t locj = ustr.substr(max((int)loci - C, 0), (size_t)2*C + uwi.length()).find(uwj);
        if(locj != UString::npos /* && abs((int)locj - (int)loci) <= int(C + uwi.length() + uwj.length())*/ )
            return true;
        loci = ustr.find(uwi, loci + uwi.length());
    }
    return false;
}

double OpinionsManager::Possib(const WordStrType& Wi, const WordStrType& Wj)
{
    int Poss=0;
    bool wi_need_find = true;
    bool wj_need_find = true;
    if(cached_word_insentence_.find(Wi) != cached_word_insentence_.end())
    {
        word_cache_hit_num_++;
        wi_need_find = false;
    }
    if(cached_word_insentence_.find(Wj) != cached_word_insentence_.end())
    {
        word_cache_hit_num_++;
        wj_need_find = false;
    }
    if(wi_need_find || wj_need_find)
    {
        for(size_t j = 0; j < Z.size(); j++)
        {
            if(wi_need_find)
            {
                size_t loci = Z[j].find(Wi);
                if(loci != WordStrType::npos )
                {
                    cached_word_insentence_[Wi].push_back(j);
                }
            }
            if(wj_need_find)
            {
                size_t locj = Z[j].find(Wj);
                if( locj != WordStrType::npos )
                {
                    cached_word_insentence_[Wj].push_back(j);
                }
            }
        }
    }
    const std::vector<uint32_t>& wi_pos = cached_word_insentence_[Wi];
    const std::vector<uint32_t>& wj_pos = cached_word_insentence_[Wj];
    Poss = FindInsertionPos(wi_pos, wj_pos);
    return Poss;
}

double OpinionsManager::Possib(const WordStrType& Wi)
{
    int Poss=0;
    if(cached_word_insentence_.find(Wi) != cached_word_insentence_.end())
    {
        word_cache_hit_num_++;
        return cached_word_insentence_[Wi].size();
    }
    for(size_t j = 0; j < Z.size(); j++)
    {
        size_t loc = Z[j].find(Wi);
        if( loc != WordStrType::npos )
        {
            Poss++;
            cached_word_insentence_[Wi].push_back(j);
        }
    }
    return Poss;
}

bool OpinionsManager::IsNeedFilter(const WordStrType& teststr)
{
    if (teststr.length() <= 1)
        return true;
    for(size_t i = 0; i < any_filter_strs_.size(); ++i)
    {
        if(teststr.find(any_filter_strs_[i]) != WordStrType::npos)
            return true;
    }
    for(size_t i = 0; i < filter_strs_.size(); ++i)
    {
        if(teststr.find(filter_strs_[i]) != WordStrType::npos &&
            teststr.length() <= 1.5*filter_strs_[i].length())
            return true;
    }
    return false;
}

static bool FilterBigramByCounter(int filter_counter, const OpinionsManager::WordFreqMapT& word_freq_records,
        const OpinionsManager::BigramPhraseT& bigram)
{
    if(IsCommentSplitStr(bigram.first))
        return false;
    OpinionsManager::WordStrType phrase;
    phrase.append(bigram.first);
    phrase.append(bigram.second);
    OpinionsManager::WordFreqMapT::const_iterator it = word_freq_records.find(phrase);
    if(it != word_freq_records.end())
    {
        return (int)it->second < filter_counter;
    }
    return false;
}

bool OpinionsManager::FilterBigramByPossib(double possib, const OpinionsManager::BigramPhraseT& bigram)
{
    if(IsCommentSplitStr(bigram.first))
        return false;
    WordStrType phrase = bigram.first;
    phrase.append(bigram.second);
    if(Possib(phrase) > possib)
        return false;
    return true;
}

bool OpinionsManager::IsBeginBigram(const WordStrType& bigram)
{
    WordFreqMapT::const_iterator it = begin_bigrams_.find(bigram);
    if(it != begin_bigrams_.end() && it->second > 2)
    {
        return true;
    }
    if(training_data_->Freq_Begin(bigram) > SigmaRead)
    {
        return true;
    }
    return false;
}

bool OpinionsManager::IsEndBigram(const WordStrType& bigram)
{
    WordFreqMapT::const_iterator it = end_bigrams_.find(bigram);
    if(it != end_bigrams_.end() && it->second > 2)
    {
        return true;
    }
    if(training_data_->Freq_End(bigram) > SigmaRead)
    {
        return true;
    }
    return false;
}

bool OpinionsManager::GenSeedBigramList(BigramPhraseContainerT& resultList)
{
    // first , get all the frequency in overall and filter the low frequency phrase.
    //
    WordSegContainerT bigram_words;
    bigram_words.resize(2);
    WordFreqMapT  word_freq_records;
    for(size_t i = 0; i < orig_comments_.size(); i++)
    {
        int bigram_start = (int)resultList.size();
        int bigram_num_insingle_comment = 0;  // splitter is excluded
        bool is_begin_bigram = true;
        WordStrType tmp_end_bigram;
        for(size_t j = 0; j < orig_comments_[i].size() - 1; j++)
        {
            if(!IsAllChineseStr(orig_comments_[i][j]))
            {
                bigram_words[0] = bigram_words[1] = WordStrType(".", encodingType_);
                resultList.push_back(std::make_pair(bigram_words[0], bigram_words[1]));
                continue;
            }
            if(!IsAllChineseStr(orig_comments_[i][j + 1]))
            {
                // last before the splitter. we can pass it.
                continue;
            }

            bigram_words[0] = orig_comments_[i][j];
            bigram_words[1] = orig_comments_[i][j + 1];
            WordStrType tmpstr = bigram_words[0];
            tmpstr.append(bigram_words[1]);
            if(IsNeedFilter(tmpstr))
            {
                continue;
            }
            if(is_begin_bigram)
            {
                begin_bigrams_[tmpstr] += 1;
                is_begin_bigram = false;
            }
            tmp_end_bigram = tmpstr;
            //if( (Srep(bigram_words) >= SigmaRep) )
            {
                if(word_freq_records.find(tmpstr) == word_freq_records.end())
                {
                    word_freq_records[tmpstr] = 1;
                }
                else
                {
                    word_freq_records[tmpstr] += 1;
                }

                resultList.push_back(std::make_pair(bigram_words[0], bigram_words[1]));
                ++bigram_num_insingle_comment;
            }
        }
        end_bigrams_[tmp_end_bigram] += 1;
        // refine the seed bigram in a very long single comment.
        if(bigram_num_insingle_comment > MAX_SEED_BIGRAM_IN_SINGLE_COMMENT)
        {
            //out << "seed bigram is too much in phrase: "<< getSentence(orig_comments_[i]) << endl;
            WordPriorityQueue_  topk_seedbigram;
            topk_seedbigram.Init(MAX_SEED_BIGRAM_IN_SINGLE_COMMENT);
            for(int start = bigram_start; start < (int)resultList.size(); ++start)
            {
                WordStrType bigram_str = resultList[start].first;
                bigram_str.append(resultList[start].second);
                if(IsAllChineseStr(bigram_str))
                    topk_seedbigram.insert(std::make_pair(bigram_str, Possib(bigram_str)));
            }
            double possib_threshold = topk_seedbigram.top().second;
            //out << "bigram filter possibility: "<< possib_threshold << endl;
            BigramPhraseContainerT::iterator bigram_it = std::remove_if(resultList.begin() + bigram_start,
                    resultList.end(),
                    boost::bind(&OpinionsManager::FilterBigramByPossib, this, possib_threshold, _1));
            //out << "bigram filter num: "<< bigram_it - resultList.end() << endl;
            resultList.erase(bigram_it, resultList.end());
        }
        // push splitter at the end of each sentence to avoid the opinion cross two different sentence.
        resultList.push_back(std::make_pair(UString(".", UString::UTF_8), UString(".", UString::UTF_8)));
    }

    int filter_counter = 2;
    if(resultList.size() > MAX_SEED_BIGRAM_RATIO*SigmaLength)
    {
        // get the top-k bigram counter, and filter the seed bigram by this counter.
        WordPriorityQueue_  topk_words;
        topk_words.Init(min(MAX_SEED_BIGRAM_RATIO*SigmaLength, (double)word_freq_records.size()/4));

        WordFreqMapT::const_iterator cit = word_freq_records.begin();
        while(cit != word_freq_records.end())
        {
            if((int)cit->second > 2)
            {
                topk_words.insert(std::make_pair(cit->first, (int)cit->second));
            }
            ++cit;
        }
        if(topk_words.size() > 1)
            filter_counter = max(topk_words.top().second, filter_counter);
    }
    //out << "===== filter counter is: "<< filter_counter << endl;
    BigramPhraseContainerT::iterator bigram_it = std::remove_if(resultList.begin(), resultList.end(),
            boost::bind(FilterBigramByCounter, filter_counter, boost::cref(word_freq_records), _1));
    resultList.erase(bigram_it, resultList.end());
    out << "===== after filter, seed bigram size: "<< resultList.size() << endl;
    ///test opinion result
    //

    //ifstream infile;
    //infile.open("/home/vincentlee/workspace/sf1/opinion_result.txt", ios::in);
    //while(infile.good())
    //{
    //    std::string opinion_phrase;
    //    getline(infile, opinion_phrase);
    //    WordSegContainerT  words;
    //    stringToWordVector(opinion_phrase, words);
    //    LOG(INFO) << "phrase: " << getSentence(words) << ", srep:" << Srep(words) << endl;
    //}
    return true;
}

//static inline bool isAdjective(int pos)
//{
//    return pos >=1 && pos <= 4 ;
//}
//
//static inline bool isNoun(int pos)
//{
//    return (pos >= 19 && pos <= 25) || pos == 29;
//}

void OpinionsManager::RefineCandidateNgram(OpinionCandidateContainerT& candList)
{
    // refine the candidate.
    // we assume that a candidate ngram should has at least one or two frequency bigram .
    // and more, we can assume at least one noun phrase or adjective phrase should exist.
    for(size_t index = 0; index < candList.size(); ++index)
    {
        const NgramPhraseT& cand_phrase = candList[index].first;

        WordStrType phrase;
        WordVectorToString(phrase, cand_phrase);
        if (!hasAdjectiveOrNoun(phrase))
        {
            out << "candidate refined: " << getSentence(candList[index].first) << endl;
            candList[index].second = SigmaRep - 1;
        }
    }
}

void OpinionsManager::GetOrigCommentsByBriefOpinion(OpinionCandStringContainerT& candOpinionString)
{
    WordFreqMapT all_orig_split_comments;
    all_orig_split_comments.rehash(orig_comments_.size() * 10);
    all_orig_split_comments.max_load_factor(0.6);

    for(size_t i = 0; i < orig_comments_.size(); i++)
    {
        WordSegContainerT  split_comment;
        for(size_t j = 0; j < orig_comments_[i].size(); j++)
        {
            if(!IsAllOpinionStr(orig_comments_[i][j]))
            {
                if(!split_comment.empty())
                {
                    if(split_comment.size() < OPINION_NGRAM_MAX)
                    {
                        WordStrType tmpstr;
                        WordVectorToString(tmpstr, split_comment);
                        //std::string tmpoutstr;
                        //tmpstr.convertString(tmpoutstr, encodingType_);
                        //out << "orig split comment added: " << tmpoutstr << endl;
                        all_orig_split_comments[tmpstr] += 1;
                    }
                    split_comment.clear();
                }
            }
            else
            {
                split_comment.push_back(orig_comments_[i][j]);
            }
        }
        if(!split_comment.empty())
        {
            if(split_comment.size() < OPINION_NGRAM_MAX)
            {
                WordStrType tmpstr;
                WordVectorToString(tmpstr, split_comment);
                //std::string tmpoutstr;
                //tmpstr.convertString(tmpoutstr, encodingType_);
                //out << "orig split comment added: " << tmpoutstr << endl;
                all_orig_split_comments[tmpstr] += 1;
            }
            split_comment.clear();
        }
    }
    std::map<WordStrType, CommentScoreT> orig_comment_opinion;

    //out << "original splitted comment total size: " << all_orig_split_comments.size() << endl;
    for(size_t i = 0; i < candOpinionString.size(); i++)
    {
        // find the shortest original comment.
        WordStrType shortest_orig;
        UString& brief_opinion = candOpinionString[i].second;
        int cnt = 0;
        WordFreqMapT::iterator it = all_orig_split_comments.begin();
        while(it != all_orig_split_comments.end())
        {
            if((*it).first.find(brief_opinion) != WordStrType::npos)
            {
                std::string tmpout;
                it->first.convertString(tmpout, encodingType_);
                //out << "orig_comment founded, " << tmpout << ", num: " << it->second << endl;
                cnt += it->second;
                if( shortest_orig.empty() )
                {
                    shortest_orig = it->first;
                }
                else if(it->first.length() < shortest_orig.length())
                {
                    shortest_orig = it->first;
                }
                else if(it->first.length() == shortest_orig.length())
                {
                    if(SrepSentence(it->first) > SrepSentence(shortest_orig))
                    {
                        std::string tmpout2;
                        shortest_orig.convertString(tmpout2, encodingType_);
                        out << "phrase has higher score: " << tmpout << " VS " << tmpout2 << std::endl;
                        shortest_orig = it->first;
                    }
                }
            }
            //else if(SimSentence(brief_opinion, it->first) > 0.6)
            //{
            //    cnt += it->second;
            //}
            ++it;
        }
        if(!shortest_orig.empty())
        {
            if(orig_comment_opinion.find(shortest_orig) == orig_comment_opinion.end())
            {
                orig_comment_opinion[shortest_orig].srep_score =
                    candOpinionString[i].first.srep_score * brief_opinion.length() / shortest_orig.length();
                orig_comment_opinion[shortest_orig].occurrence_num = cnt;
            }
            else
            {
                orig_comment_opinion[shortest_orig].srep_score =
                    max(orig_comment_opinion[shortest_orig].srep_score, candOpinionString[i].first.srep_score * brief_opinion.length() / shortest_orig.length());
                orig_comment_opinion[shortest_orig].occurrence_num += cnt;
            }
        }
        else
        {
            std::string tmpstr;
            brief_opinion.convertString(tmpstr, encodingType_);
            out << "no orig_comment found:" << tmpstr << endl;
        }
    }
    candOpinionString.clear();
    std::map< WordStrType, CommentScoreT >::iterator it = orig_comment_opinion.begin();
    while(it != orig_comment_opinion.end())
    {
        if(it->second.srep_score >= SigmaRep && !IsNeedFilter(it->first))
        {
            double score = SrepSentence(it->first);
            if(score >= SigmaRep)
            {
                // find similarity
                bool can_insert = true;
                bool is_larger_replace_exist = false;

                size_t start = 0;
                size_t remove_end = candOpinionString.size();
                while( start < remove_end )
                {
                    if(SimSentence(it->first, candOpinionString[start].second) > SigmaSim)
                    {
                        can_insert = false;
                        if(score > candOpinionString[start].first.srep_score)
                        {
                            it->second.occurrence_num += candOpinionString[start].first.occurrence_num;

                            //  swap the similarity to the end.
                            OpinionCandStringT temp = candOpinionString[remove_end - 1];
                            candOpinionString[remove_end - 1] = candOpinionString[start];
                            candOpinionString[start] = temp;
                            std::string removed_str;
                            candOpinionString[remove_end - 1].second.convertString(removed_str, encodingType_);
                            out << "similarity sentence removed: " << removed_str << endl;
                            --remove_end;
                            continue;
                        }
                        else
                        {
                            candOpinionString[start].first.occurrence_num += it->second.occurrence_num;
                            is_larger_replace_exist = true;
                            std::string removed_str;
                            it->first.convertString(removed_str, encodingType_);
                            out << "similarity sentence not added: " << removed_str << endl;
                        }
                    }
                    ++start;
                }
                if(!can_insert && (remove_end < candOpinionString.size()))
                {
                    if(is_larger_replace_exist)
                    {
                        // just remove all similarity. Because a larger already in the candidate.
                        candOpinionString.erase(candOpinionString.begin() + remove_end, candOpinionString.end());
                    }
                    else
                    {
                        // replace the similarity with current.
                        candOpinionString[remove_end] = std::make_pair(it->second, it->first);
                        candOpinionString.erase(candOpinionString.begin() + remove_end + 1, candOpinionString.end());
                    }
                }
                if(can_insert)
                {
                    candOpinionString.push_back(std::make_pair(it->second, it->first));
                }
            }
            else
            {
                std::string tmp_str;
                it->first.convertString(tmp_str, encodingType_);
                out << "low score sentence: " << tmp_str << ", score : " << score << std::endl;
            }
        }
        else
        {
            // ignored
            std::string tmp_str;
            it->first.convertString(tmp_str, encodingType_);
            out << "ignored candOpinionString: " << tmp_str << ", score : " << it->second.srep_score<< std::endl;
        }
        ++it;
    }
}

void OpinionsManager::recompute_srep(OpinionCandStringContainerT& candList)
{
    // compute the srep in sentence. This can get better score for the
    // meaningful sentence to avoid the bad sentence with good bigrams.
    for(size_t i = 0; i < candList.size(); i++)
    {
        if(candList[i].first.srep_score >= SigmaRep)
            candList[i].first.srep_score = SrepSentence(candList[i].second);
    }
}

bool OpinionsManager::GetFinalMicroOpinion(const BigramPhraseContainerT& seed_bigramlist,
        bool need_orig_comment_phrase, std::vector<std::pair<double, WordStrType> >& final_result)
{
    if (seed_bigramlist.empty())
    {return true;}

    NgramPhraseContainerT seedGrams;
    OpinionCandidateContainerT candList;
    for(size_t i = 0; i < seed_bigramlist.size(); i++)
    {
        WordSegContainerT phrase;
        phrase.push_back(seed_bigramlist[i].first);
        phrase.push_back(seed_bigramlist[i].second);

        seedGrams.push_back(phrase);
    }

    struct timeval starttime, endtime;
    gettimeofday(&starttime, NULL);

    cached_valid_ngrams_.rehash(seedGrams.size() * 2);
    cached_valid_ngrams_.max_load_factor(0.8);
    cached_srep.rehash(seedGrams.size() * 2);
    cached_srep.max_load_factor(0.8);
    for(size_t i = 0; i < seedGrams.size(); i++)
    {
        WordStrType phrasestr;
        WordVectorToString(phrasestr, seedGrams[i]);
        if(IsBeginBigram(phrasestr))
        {
            GenerateCandidates(seedGrams[i], candList, seed_bigramlist, i);
        }
        else
        {
            //out << "seed bigram not begin : " << getSentence(seedGrams[i]) << endl;
        }
    }

    gettimeofday(&endtime, NULL);
    int64_t interval = (endtime.tv_sec - starttime.tv_sec)*1000 + (endtime.tv_usec - starttime.tv_usec)/1000;
    if(interval > 1000)
        out << "gen candidate used more than 1000ms " << interval << endl;

    //std::cout << "\r candList.size() = "<< candList.size();

    if(candList.size() > SigmaLength/2)
        RefineCandidateNgram(candList);
    OpinionCandStringContainerT candOpinionString;
    changeForm(candList, candOpinionString);

    if(candOpinionString.size() > 0)
    {
        out << "before recompute_srep: candidates are " << endl;
        for(size_t i = 0; i < candOpinionString.size(); ++i)
        {
            std::string tmpstr;
            candOpinionString[i].second.convertString(tmpstr, encodingType_);
            out << "str:" << tmpstr << ",score:" << candOpinionString[i].first.srep_score << endl;
        }
    }

    gettimeofday(&starttime, NULL);
    if(need_orig_comment_phrase)
    {
        // get orig_comment from the candidate opinions.
        GetOrigCommentsByBriefOpinion(candOpinionString);
    }
    //recompute_srep(candOpinionString);

    gettimeofday(&endtime, NULL);
    interval = (endtime.tv_sec - starttime.tv_sec)*1000 + (endtime.tv_usec - starttime.tv_usec)/1000;
    if(interval > 10)
        out << "get origin comment time: " << interval << endl;
    //out << "after recompute_srep: candidates are " << endl;
    //for(size_t i = 0; i < candOpinionString.size(); ++i)
    //{
    //    out << "str:" << candOpinionString[i].first << ",score:" << candOpinionString[i].second << endl;
    //}

    sort(candOpinionString.begin(), candOpinionString.end(), seedPairCmp2);
    size_t result_num = min((size_t)SigmaLength, min(2*orig_comments_.size(), candOpinionString.size()));
    for(size_t i = 0; i < result_num; ++i)
    {
        final_result.push_back(std::make_pair(candOpinionString[i].first.occurrence_num, candOpinionString[i].second));
    }
    return true;
}

std::string OpinionsManager::getSentence(const WordSegContainerT& candVector)
{
    WordStrType temp;
    for (size_t j = 0; j < candVector.size(); j++)
    {
        temp += candVector[j];
    }
    std::string tmpstr;
    temp.convertString(tmpstr, encodingType_);
    return tmpstr;
}

void OpinionsManager::ValidCandidateAndUpdate(const NgramPhraseT& phrase,
        OpinionCandidateContainerT& candList)
{
    WordStrType phrasestr;
    WordVectorToString(phrasestr, phrase);
    if(cached_valid_ngrams_.find(phrasestr) != cached_valid_ngrams_.end())
    {
        valid_cache_hit_num_++;
        return;
    }

    cached_valid_ngrams_[phrasestr] = 1;

    WordStrType end_bigram_str = phrase[phrase.size() - 2];
    end_bigram_str.append(phrase[phrase.size() - 1]);
    if(!IsEndBigram(end_bigram_str))
    {
        return;
    }

    if(IsNeedFilter(phrasestr))
    {
        out << "candidate filtered by configure : " << getSentence(phrase) << endl;
        return;
    }

    double score = Score(phrase);
    if(score < SigmaRep_dynamic.top())
    {
        //out << "skip phrase since score is less than min dynamic score : " << getSentence(phrase) << ", " << score << ", top score:" <<SigmaRep_dynamic.top() << std::endl;
        return;
    }

    bool can_insert = true;
    bool is_larger_replace_exist = false;

    size_t start = 0;
    size_t remove_end = candList.size();
    while( start < remove_end )
    {
        if(Sim(phrase, candList[start].first) > SigmaSim*1.2)
        {
            can_insert = false;
            if(score > candList[start].second)
            {
                //  swap the similarity to the end.
                OpinionCandidateT temp = candList[remove_end - 1];
                candList[remove_end - 1] = candList[start];
                //out << "similarity need removed: " << getSentence(candList[remove_end - 1].first) << endl;
                WordStrType removed_phrasestr;
                WordVectorToString(removed_phrasestr, candList[remove_end - 1].first);
                cached_valid_ngrams_[removed_phrasestr] = 1;
                candList[start] = temp;
                --remove_end;
                continue;
            }
            else
            {
                //out << "similarity larger is exist: " << getSentence(candList[start].first) << ",srep:" << candList[start].second << endl;
                is_larger_replace_exist = true;
            }
        }
        // if any low score item remove them.
        if(candList.size() > SigmaLength*4 && candList[start].second < SigmaRep_dynamic.top())
        {
            OpinionCandidateT temp = candList[remove_end - 1];
            candList[remove_end - 1] = candList[start];
            WordStrType removed_phrasestr;
            WordVectorToString(removed_phrasestr, candList[remove_end - 1].first);
            cached_valid_ngrams_[removed_phrasestr] = 1;
            candList[start] = temp;
            --remove_end;
            continue;
        }
        ++start;
    }
    if(!can_insert && (remove_end < candList.size()))
    {
        if(is_larger_replace_exist)
        {
            // just remove all similarity. Because a larger already in the candidate.
            candList.erase(candList.begin() + remove_end, candList.end());
        }
        else
        {
            // replace the similarity with current.
            candList[remove_end] = std::make_pair(phrase, score);
            candList.erase(candList.begin() + remove_end + 1, candList.end());
            //out << "similarity replaced by: " << getSentence(phrase) << endl;
        }
    }
    if(can_insert && /*score >= SigmaRep &&*/ phrase.size() > OPINION_NGRAM_MIN)
    {
        //out << "new added: " << getSentence(phrase) <<  endl;
        candList.push_back(std::make_pair(phrase, score));
    }
    SigmaRep_dynamic.push(score);
    while(SigmaRep_dynamic.size() > SigmaLength*4)
        SigmaRep_dynamic.pop();
}

bool OpinionsManager::NotMirror(const NgramPhraseT& phrase, const BigramPhraseT& bigram)
{

    if(phrase.size() > 3)
    {
        WordStrType phrasestr;
        WordVectorToString(phrasestr, phrase);
        WordStrType bigramstr = bigram.first;
        bigramstr.append(bigram.second);
        if(phrasestr.find(bigramstr) != WordStrType::npos)
            return false;
        return !(phrase[phrase.size() - 2] == bigram.second);
    }
    else if( phrase.size() == 2)
        return !( (phrase[0] == bigram.second) && (phrase[1] == bigram.first) );
    else if( phrase.size() == 3)
        return !( (phrase[1] == bigram.second) && (phrase[2] == bigram.first) );
    return true;
}

void OpinionsManager::Merge(NgramPhraseT& phrase, const BigramPhraseT& bigram)
{
    phrase.push_back(bigram.second);
}

OpinionsManager::BigramPhraseContainerT OpinionsManager::GetJoinList(const NgramPhraseT& phrase,
        const BigramPhraseContainerT& bigramlist, int current_merge_pos)
{
    BigramPhraseContainerT resultList;
    if( (size_t)current_merge_pos >= bigramlist.size() - 1 )
        return resultList;
    if(phrase[phrase.size() - 1] == bigramlist[current_merge_pos + 1].first)
    {
        resultList.push_back( bigramlist[current_merge_pos + 1] );
    }
    return resultList;
}

void OpinionsManager::GenerateCandidates(const NgramPhraseT& phrase,
        OpinionCandidateContainerT& candList,
        const BigramPhraseContainerT& seedBigrams, int current_merge_pos)
{
    if( (size_t)current_merge_pos >= seedBigrams.size() - 1 )
        return;
    if( phrase.size() > OPINION_NGRAM_MAX )
        return;
    //if (phrase.size() > OPINION_NGRAM_MIN)
    //{
    //    if ( Srep(phrase) < SigmaRep )
    //    {
    //        out << "skip low score phrase: " << getSentence(phrase) << ", " << Srep(phrase) << std::endl;
    //        return;
    //    }
    //}

    if( phrase.size() > OPINION_NGRAM_MIN)
    {
        ValidCandidateAndUpdate(phrase, candList);
    }

    // find the overlap bigram to prepare combining them.
    BigramPhraseContainerT joinList = GetJoinList(phrase, seedBigrams, current_merge_pos);
    for (size_t j = 0; j < joinList.size(); j++)
    {
        BigramPhraseT&  bigram = joinList[j];
        if( NotMirror(phrase, bigram) )
        {
            NgramPhraseT new_phrase = phrase;
            Merge(new_phrase, bigram);
            //out << "new phrase generated: " << getSentence(new_phrase) << ",srep:" << Srep(new_phrase) << endl;
            GenerateCandidates(new_phrase, candList, seedBigrams, current_merge_pos + 1);
        }
    }
}

void OpinionsManager::changeForm(const OpinionCandidateContainerT& candList, OpinionCandStringContainerT& newForm)
{
    WordStrType temp;
    for(size_t i = 0; i < candList.size(); i++)
    {
        if(candList[i].second >= SigmaRep)
        {
            WordVectorToString(temp, candList[i].first);
            newForm.push_back(std::make_pair(CommentScoreT(candList[i].second, 0), UString(temp)));
        }
    }
}

std::vector<std::pair<double, OpinionsManager::WordStrType> > OpinionsManager::getOpinion(bool need_orig_comment_phrase)
{
    struct timeval starttime, endtime;
    gettimeofday(&starttime, NULL);

    BigramPhraseContainerT seed_bigramlist;
    GenSeedBigramList(seed_bigramlist);

    gettimeofday(&endtime, NULL);
    int64_t interval = (endtime.tv_sec - starttime.tv_sec)*1000 + (endtime.tv_usec - starttime.tv_usec)/1000;
    if(interval > 100)
        out << "gen seed bigram used more than 100ms " << interval << endl;

    std::vector< std::pair< double, WordStrType> > final_result;
    GetFinalMicroOpinion( seed_bigramlist, need_orig_comment_phrase, final_result);
    if(!final_result.empty())
    {
        for(size_t i = 0; i < final_result.size(); i++)
        {
            std::string tmpstr;
            final_result[i].second.convertString(tmpstr, encodingType_);
            out << "MicroOpinions:" << tmpstr << ", occurrence_num: " << final_result[i].first << endl;
        }
        out<<"-------------Opinion finished---------------"<<endl;
        out.flush();
    }
    out << "word/pmi/valid cache hit ratio: " << word_cache_hit_num_ << "," << pmi_cache_hit_num_ << "," << valid_cache_hit_num_ << endl;

    gettimeofday(&endtime, NULL);
    interval = (endtime.tv_sec - starttime.tv_sec)*1000 + (endtime.tv_usec - starttime.tv_usec)/1000;
    if(interval > 10)
        out << "get opinion time(ms):" << interval << endl;

    CleanCacheData();
    return final_result;
}

};
