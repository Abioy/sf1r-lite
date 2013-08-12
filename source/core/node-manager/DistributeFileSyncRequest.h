#ifndef SF1R_NODEMANAGER_DISTRIBUTE_FILESYNC_REQ_H
#define SF1R_NODEMANAGER_DISTRIBUTE_FILESYNC_REQ_H

#include <sf1r-net/RpcServerRequest.h>
#include <sf1r-net/RpcServerRequestData.h>

namespace sf1r
{

struct GetReqLogData : public RpcServerRequestData
{
    bool success;
    uint32_t  start_inc;
    uint32_t  end_inc;
    std::vector<std::string>  logdata_list;
    MSGPACK_DEFINE(success, start_inc, end_inc, logdata_list);
};

struct GetRunningReqLogData : public RpcServerRequestData
{
    bool success;
    std::string  running_logdata;
    MSGPACK_DEFINE(success, running_logdata);
};

struct GetSCDListData : public RpcServerRequestData
{
    bool success;
    std::string  collection;
    std::vector<std::string> scd_list;
    MSGPACK_DEFINE(success, collection, scd_list);
};

struct GetCollectionFileListData : public RpcServerRequestData
{
    bool success;
    std::vector<std::string>  collections;
    std::vector<std::string> file_list;
    MSGPACK_DEFINE(success, collections, file_list);
};


struct GetFileData : public RpcServerRequestData
{
    bool success;
    std::string filepath;
    uint64_t filesize;
    std::string file_checksum;
    MSGPACK_DEFINE(success, filepath, filesize, file_checksum);
};

struct ReadyReceiveData : public RpcServerRequestData
{
    bool success;
    // for transfer file data.
    std::string receiver_ip;
    uint16_t receiver_port;
    // for notify finish receive file
    std::string receiver_rpcip;
    uint16_t receiver_rpcport;
    std::string filepath;
    uint64_t filesize;
    MSGPACK_DEFINE(success, receiver_ip, receiver_port, receiver_rpcip, receiver_rpcport, filepath, filesize);
};

struct FinishReceiveData : public RpcServerRequestData
{
    bool success;
    std::string filepath;
    MSGPACK_DEFINE(success, filepath);
};

struct ReportStatusReqData : public RpcServerRequestData
{
    std::string req_host;
    std::vector<std::string> check_file_list;
    std::vector<std::string> check_key_list;
    uint32_t check_log_start_id;
    MSGPACK_DEFINE(req_host, check_file_list, check_key_list, check_log_start_id);
};

struct ReportStatusRspData : public RpcServerRequestData
{
    bool success;
    std::string  rsp_host;
    std::vector<std::string> check_file_result;
    std::vector<std::string> check_key_result;
    std::vector<uint32_t> check_logid_list;
    std::vector<std::string> check_collection_list;
    MSGPACK_DEFINE(success, rsp_host, check_file_result, check_key_result, check_logid_list, check_collection_list);
};

struct GenerateSCDReqData : public RpcServerRequestData
{
    std::string req_host;
    std::string coll;
    std::map<uint8_t, std::vector<uint16_t> > migrate_vnode_list;
    MSGPACK_DEFINE(req_host, migrate_vnode_list);
};

struct GenerateSCDRspData : public RpcServerRequestData
{
    bool success;
    std::string  rsp_host;
    std::map<uint8_t, std::string> generated_insert_scds;
    std::map<uint8_t, std::string> generated_del_scds;
    MSGPACK_DEFINE(success, rsp_host, generated_insert_scds, generated_del_scds);
};


class FileSyncServerRequest : public RpcServerRequest
{
public:
    typedef std::string method_t;

    enum METHOD
    {
        METHOD_TEST = 0,
        METHOD_GET_REQLOG,
        METHOD_GET_SCD_LIST,
        METHOD_GET_COLLECTION_FILE_LIST,
        METHOD_GET_FILE,
        METHOD_READY_RECEIVE,
        METHOD_FINISH_RECEIVE,
        METHOD_REPORT_STATUS_REQ,
        METHOD_REPORT_STATUS_RSP,
        METHOD_GET_RUNNING_REQLOG,
        METHOD_GENERATE_MIGRATE_SCD_REQ,
        METHOD_GENERATE_MIGRATE_SCD_RSP,
        COUNT_OF_METHODS
    };
    static const method_t method_names[COUNT_OF_METHODS];
public:
    FileSyncServerRequest(const int& method) : RpcServerRequest(method){}
};

class GetReqLogRequest : public RpcRequestRequestT<GetReqLogData, FileSyncServerRequest>
{
public:
    GetReqLogRequest()
        :RpcRequestRequestT<GetReqLogData, FileSyncServerRequest>(METHOD_GET_REQLOG)
    {
    }
};

class GetRunningReqLogRequest : public RpcRequestRequestT<GetRunningReqLogData, FileSyncServerRequest>
{
public:
    GetRunningReqLogRequest()
        :RpcRequestRequestT<GetRunningReqLogData, FileSyncServerRequest>(METHOD_GET_RUNNING_REQLOG)
    {
    }
};

class GetSCDListRequest : public RpcRequestRequestT<GetSCDListData, FileSyncServerRequest>
{
public:
    GetSCDListRequest()
        :RpcRequestRequestT<GetSCDListData, FileSyncServerRequest>(METHOD_GET_SCD_LIST)
    {
    }
};

class GetCollectionFileListRequest : public RpcRequestRequestT<GetCollectionFileListData, FileSyncServerRequest>
{
public:
    GetCollectionFileListRequest()
        :RpcRequestRequestT<GetCollectionFileListData, FileSyncServerRequest>(METHOD_GET_COLLECTION_FILE_LIST)
    {
    }
};

class GetFileRequest : public RpcRequestRequestT<GetFileData, FileSyncServerRequest>
{
public:
    GetFileRequest()
        :RpcRequestRequestT<GetFileData, FileSyncServerRequest>(METHOD_GET_FILE)
    {
    }
};

class ReadyReceiveRequest : public RpcRequestRequestT<ReadyReceiveData, FileSyncServerRequest>
{
public:
    ReadyReceiveRequest()
        :RpcRequestRequestT<ReadyReceiveData, FileSyncServerRequest>(METHOD_READY_RECEIVE)
    {
    }
};

class FinishReceiveRequest : public RpcRequestRequestT<FinishReceiveData, FileSyncServerRequest>
{
public:
    FinishReceiveRequest()
        :RpcRequestRequestT<FinishReceiveData, FileSyncServerRequest>(METHOD_FINISH_RECEIVE)
    {
    }
};

class ReportStatusRequest : public RpcRequestRequestT<ReportStatusReqData, FileSyncServerRequest>
{
public:
    ReportStatusRequest()
        :RpcRequestRequestT<ReportStatusReqData, FileSyncServerRequest>(METHOD_REPORT_STATUS_REQ)
    {
    }
};

class ReportStatusRsp : public RpcRequestRequestT<ReportStatusRspData, FileSyncServerRequest>
{
public:
    ReportStatusRsp()
        :RpcRequestRequestT<ReportStatusRspData, FileSyncServerRequest>(METHOD_REPORT_STATUS_RSP)
    {
    }
};
class GenerateSCDRequest : public RpcRequestRequestT<GenerateSCDReqData, FileSyncServerRequest>
{
public:
    GenerateSCDRequest()
        :RpcRequestRequestT<GenerateSCDReqData, FileSyncServerRequest>(METHOD_GENERATE_MIGRATE_SCD_REQ)
    {
    }
};

class GenerateSCDRsp : public RpcRequestRequestT<GenerateSCDRspData, FileSyncServerRequest>
{
public:
    GenerateSCDRsp()
        :RpcRequestRequestT<GenerateSCDRspData, FileSyncServerRequest>(METHOD_GENERATE_MIGRATE_SCD_RSP)
    {
    }
};




}

#endif
