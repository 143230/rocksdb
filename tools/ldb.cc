// Copyright (c) 2012 Facebook. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/ldb_cmd.h"

namespace leveldb {

class LDBCommandRunner {
public:

  static void PrintHelp(const char* exec_name) {
    string ret;

    ret.append("ldb - LevelDB Tool");
    ret.append("\n\n");
    ret.append("All commands MUST specify --" + LDBCommand::ARG_DB +
        "=<full_path_to_db_directory>\n");
    ret.append("\n");
    ret.append("The following optional parameters control if keys/values are "
        "input/output as hex or as plain strings:\n");
    ret.append("  --" + LDBCommand::ARG_KEY_HEX +
        " : Keys are input/output as hex\n");
    ret.append("  --" + LDBCommand::ARG_VALUE_HEX +
        " : Values are input/output as hex\n");
    ret.append("  --" + LDBCommand::ARG_HEX +
        " : Both keys and values are input/output as hex\n");
    ret.append("\n");

    ret.append("The following optional parameters control the database "
        "internals:\n");
    ret.append("  --" + LDBCommand::ARG_BLOOM_BITS + "=<int,e.g.:14>\n");
    ret.append("  --" + LDBCommand::ARG_COMPRESSION_TYPE +
        "=<no|snappy|zlib|bzip2>\n");
    ret.append("  --" + LDBCommand::ARG_BLOCK_SIZE +
        "=<block_size_in_bytes>\n");
    ret.append("  --" + LDBCommand::ARG_AUTO_COMPACTION + "=<true|false>\n");
    ret.append("  --" + LDBCommand::ARG_WRITE_BUFFER_SIZE +
        "=<int,e.g.:4194304>\n");
    ret.append("  --" + LDBCommand::ARG_FILE_SIZE + "=<int,e.g.:2097152>\n");

    ret.append("\n\n");
    ret.append("Data Access Commands:\n");
    PutCommand::Help(ret);
    GetCommand::Help(ret);
    BatchPutCommand::Help(ret);
    ScanCommand::Help(ret);
    DeleteCommand::Help(ret);
    DBQuerierCommand::Help(ret);
    ApproxSizeCommand::Help(ret);

    ret.append("\n\n");
    ret.append("Admin Commands:\n");
    WALDumperCommand::Help(ret);
    CompactorCommand::Help(ret);
    ReduceDBLevelsCommand::Help(ret);
    DBDumperCommand::Help(ret);
    DBLoaderCommand::Help(ret);

    fprintf(stderr, "%s\n", ret.c_str());
  }

  static void RunCommand(int argc, char** argv) {
    if (argc <= 2) {
      PrintHelp(argv[0]);
      exit(1);
    }

    LDBCommand* cmdObj = LDBCommand::InitFromCmdLineArgs(argc, argv);
    if (cmdObj == NULL) {
      fprintf(stderr, "Unknown command\n");
      PrintHelp(argv[0]);
      exit(1);
    }

    if (!cmdObj->ValidateCmdLineOptions()) {
      exit(1);
    }

    cmdObj->Run();
    LDBCommandExecuteResult ret = cmdObj->GetExecuteState();
    fprintf(stderr, "%s\n", ret.ToString().c_str());
    delete cmdObj;

    exit(ret.IsFailed());
  }

};

}

int main(int argc, char** argv) {
  leveldb::LDBCommandRunner::RunCommand(argc, argv);
}
