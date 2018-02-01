#pragma once

#include <sys/uio.h>
#include <poll.h>
#include <stdint.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>

#include <plog/Log.h>

#define PROJECT_NAME "libmc"
#define MC_DEFAULT_PORT 11211
#define MC_DEFAULT_POLL_TIMEOUT 300
#define MC_DEFAULT_CONNECT_TIMEOUT 10
#define MC_DEFAULT_RETRY_TIMEOUT 5


#ifdef UIO_MAXIOV
#define MC_UIO_MAXIOV UIO_MAXIOV
#else
#define MC_UIO_MAXIOV 1024
#endif


#ifdef NI_MAXHOST
#define MC_NI_MAXHOST NI_MAXHOST
#else
#define MC_NI_MAXHOST 1025
#endif

#ifdef NI_MAXSERV
#define MC_NI_MAXSERV NI_MAXSERV
#else
#define MC_NI_MAXSERV 32
#endif

#ifdef MSG_MORE
#define MC_MSG_MORE MSG_MORE
#else
#define MC_MSG_MORE 0
#endif


#define MIN_DATABLOCK_CAPACITY 8192
#define MIN(A, B) (((A) > (B)) ? (B) : (A))
#define MAX(A, B) (((A) < (B)) ? (B) : (A))
#define CSTR(STR) (const_cast<char*>(STR))
#define ASSERT_N_STREQ(S1, S2, N) do {ASSERT_TRUE(0 == std::strncmp((S1), (S2), (N)));} while (0)

#if defined __cplusplus
# define __VOID_CAST static_cast<void>
#else
# define __VOID_CAST (void)
#endif

void printBacktrace();

#define _mc_clean_errno() (errno == 0 ? "None" : strerror(errno))

#define MAX_LOG_LENGTH 300

#define _mc_log(LEVEL, FORMAT, ...) do { \
  char buf[MAX_LOG_LENGTH]; \
  if ((snprintf(buf, MAX_LOG_LENGTH, FORMAT "\n", ##__VA_ARGS__)) > 0) { \
    LOG(LEVEL) << buf; \
  } \
} while (0)

#define _mc_log_if(LEVEL, COND, FORMAT, ...) do { \
  if (COND) _mc_log(LEVEL, FORMAT, ##__VA_ARGS__); \
} while (0)

// consistent with plog/include/Serverity.h
#define MC_LOG_LEVEL_ERROR 2
#define MC_LOG_LEVEL_WARNING 3
#define MC_LOG_LEVEL_INFO 4
#define MC_LOG_LEVEL_DEBUG 5

#define MC_LOG_LEVEL MC_LOG_LEVEL_INFO

#ifdef NDEBUG
#define log_debug_if(COND, M, ...) __VOID_CAST(0)
#define log_debug(M, ...) __VOID_CAST(0)

#define _ASSERTION_FAILED(cond) { \
  _mc_log(plog::fatal, "failed assertion `%s'" , #cond); \
  printBacktrace(); \
} while (0)
#else

#define log_debug_if(COND, M, ...) _mc_log_if(plog::debug, COND, "[E: %s] " M, _mc_clean_errno(), ##__VA_ARGS__)
#define log_debug(M, ...) log_debug_if(MC_LOG_LEVEL >= MC_LOG_LEVEL_DEBUG, M, ##__VA_ARGS__)

#define _ASSERTION_FAILED(cond) do { \
  _mc_log(plog::fatal, "failed assertion `%s'" , #cond); \
  printBacktrace(); \
  abort(); \
} while (0)
#endif

#define ASSERT(cond) if (!(cond)) _ASSERTION_FAILED(cond)

#define NOT_REACHED() ASSERT(0)

#define log_info_if(COND, M, ...) _mc_log_if(plog::info, COND, M, ##__VA_ARGS__)
#define log_info(M, ...) log_info_if(MC_LOG_LEVEL >= MC_LOG_LEVEL_INFO, M, ##__VA_ARGS__)

#define log_warn_if(COND, M, ...) _mc_log_if(plog::warning, COND, "[E: %s] " M, _mc_clean_errno(), ##__VA_ARGS__)
#define log_warn(M, ...) log_warn_if(MC_LOG_LEVEL >= MC_LOG_LEVEL_WARNING, M, ##__VA_ARGS__)

#define log_err_if(COND, M, ...) _mc_log_if(plog::error, COND, "[E: %s] " M, _mc_clean_errno(), ##__VA_ARGS__)
#define log_err(M, ...) log_err_if(MC_LOG_LEVEL >= MC_LOG_LEVEL_ERROR, M, ##__VA_ARGS__)

namespace douban {
namespace mc {

typedef struct pollfd pollfd_t;

// FSM: finite state machine
typedef enum {
  FSM_START,

  // END
  FSM_END, // got "END\r\n"

  // ERROR
  FSM_ERROR, // got "ERROR\r\n"

  // VALUE <key> <flags> <bytes>[ <cas unique>]\r\n
  // <data block>\r\n
  FSM_GET_START, // got "VALUE "
  FSM_GET_KEY, // got "key "
  FSM_GET_FLAGS, // got "flags "
  FSM_GET_BYTES, // got "bytes " or "bytes\r"
  FSM_GET_CAS, // got "cas\r" or got "bytes\r"
  FSM_GET_VALUE_REMAINING, // not got <data block> + "\r\n"

  // VERSION
  FSM_VER_START, // got "VERSION "

  // STAT
  FSM_STAT_START, // got "STAT "

  // [0-9] // INCR/DECR
  FSM_INCR_DECR_START, // got [0-9]
  FSM_INCR_DECR_REMAINING, // not got "\r\n"
} parser_state_t;

#define IS_END_STATE(st) ((st) == FSM_END or (st) == FSM_ERROR)


typedef enum {
  // storage commands
  // <command name> <key> <flags> <exptime> <bytes>[ noreply]\r\n
  // cas <key> <flags> <exptime> <bytes> <cas unique>[ noreply]\r\n
  // <data block>\r\n
  // ->
  // text msg
  SET_OP,
  ADD_OP,
  REPLACE_OP,
  APPEND_OP,
  PREPEND_OP,
  CAS_OP,

  // retrieval commands
  // get <key>*\r\n
  // gets <key>*\r\n
  // ->
  // VALUE <key> <flags> <bytes> [<cas unique>]\r\n
  // <data block>\r\n
  // "END\r\n"
  GET_OP,
  GETS_OP,

  // incr/decr <key> <value>[ noreply]\r\n
  // ->
  // <value>\r\n or "NOT_FOUND\r\n"
  INCR_OP,
  DECR_OP,

  // touch <key> <exptime>[ noreply]\r\n
  // ->
  // text msg
  TOUCH_OP,

  // delete <key>[ noreply]\r\n
  // ->
  // text msg
  DELETE_OP,

  STATS_OP,
  FLUSHALL_OP,
  VERSION_OP,
  QUIT_OP,
} op_code_t;


} // namespace mc
} // namespace douban
