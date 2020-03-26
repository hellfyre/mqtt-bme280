#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DEBUG 4
#define LOG_LEVEL_VERBOSE 5

void log_verbose(String message) {
    #ifdef LOG_LEVEL
    if (LOG_LEVEL == LOG_LEVEL_VERBOSE) {
        Serial.println(String("V " + message));
    }
    #endif
}

void log_debug(String message) {
    #ifdef LOG_LEVEL
    if (LOG_LEVEL >= LOG_LEVEL_DEBUG) {
        Serial.println(String("D " + message));
    }
    #endif
}

void log_info(String message) {
    #ifdef LOG_LEVEL
    if (LOG_LEVEL >= LOG_LEVEL_INFO) {
        Serial.println(String("I " + message));
    }
    #endif
}

void log_warn(String message) {
    #ifdef LOG_LEVEL
    if (LOG_LEVEL >= LOG_LEVEL_WARN) {
        Serial.println(String("W " + message));
    }
    #endif
}

void log_error(String message) {
    #ifdef LOG_LEVEL
    if (LOG_LEVEL >= LOG_LEVEL_ERROR) {
        Serial.println(String("E " + message));
    }
    #endif
}
