#include "DistributeFileSyncRequest.h"

namespace sf1r
{

const FileSyncServerRequest::method_t FileSyncServerRequest::method_names[] =
{
    "test",
    "get_reqlog",
    "get_scdlist",
    "get_col_filelist",
    "get_file",
    "ready_receive",
    "finish_receive",
    "report_status_req",
    "report_status_rsp",
    "get_running_reqlog"
    "generate_migrate_scd_req",
    "generate_migrate_scd_rsp",
};

}

