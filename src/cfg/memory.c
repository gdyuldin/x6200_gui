#include "memory.private.h"

#include "cfg.private.h"
#include "band.private.h"

#include "../lvgl/lvgl.h"
#include <stdlib.h>


#define STR_EQUAL(a, b) (strcmp(a, b) == 0)

struct memory_item {
    int32_t val;
    bool loaded;
};

typedef struct {
    struct memory_item freq;
    struct memory_item mode;
    struct memory_item agc;
    struct memory_item pre;
    struct memory_item att;
    struct memory_item rfg;
} memory_data_t;

static sqlite3        *db;
static sqlite3_stmt   *write_stmt;
static sqlite3_stmt   *read_stmt;
static pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t read_mutex  = PTHREAD_MUTEX_INITIALIZER;

inline static void fill_data(const char *name, int32_t val, memory_data_t *mem_data);

void cfg_memory_init(sqlite3 *database) {
    db = database;
    int rc;
    rc = sqlite3_prepare_v2(db, "SELECT name, val FROM memory WHERE id=:id", -1, &read_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO memory(id, name, val) VALUES(:id, :name, :val)", -1,
                            &write_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare write statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
}

bool cfg_memory_load(int32_t id) {
    int           rc;
    memory_data_t mem_data = {
        .freq={.loaded=false},
        .mode={.loaded=false},
        .agc={.loaded=false},
        .att={.loaded=false},
        .pre={.loaded=false},
    };
    sqlite3_stmt *stmt = read_stmt;
    pthread_mutex_lock(&read_mutex);
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":id"), id);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind mem id %i: %s", id, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&read_mutex);
        return false;
    }
    const char *name;
    int32_t val;
    while (1) {
        rc = sqlite3_step(stmt);

        if (rc == SQLITE_ROW) {
            name = sqlite3_column_text(stmt, 0);
            val = sqlite3_column_int(stmt, 1);
            fill_data(name, val, &mem_data);
        } else if (rc == SQLITE_DONE) {
            rc = 0;
            break;
        } else {
            LV_LOG_ERROR("Error while reading rows: %s", sqlite3_errmsg(db));
            break;
        }
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&read_mutex);

    /* Setup values */
    if (!mem_data.freq.loaded) {
        return false;
    }
    band_info_t *band_info = get_band_info_by_freq(mem_data.freq.val);
    int32_t band_id;
    if (!band_info) {
        band_id = BAND_UNDEFINED;
    } else {
        band_id = band_info->id;
    }
    subject_set_int(cfg.band_id.val, band_id);
    subject_set_int(cfg_cur.fg_freq, mem_data.freq.val);
    if (mem_data.mode.loaded) subject_set_int(cfg_cur.mode, mem_data.mode.val);
    if (mem_data.agc.loaded) subject_set_int(cfg_cur.agc, mem_data.agc.val);
    if (mem_data.att.loaded) subject_set_int(cfg_cur.att, mem_data.att.val);
    if (mem_data.pre.loaded) subject_set_int(cfg_cur.pre, mem_data.pre.val);
    return true;
}

void cfg_memory_save(int32_t id) {

    struct vfo_params fg_params;
    if (subject_get_int(cfg_cur.band->vfo.val) == X6200_VFO_A) {
        fg_params = cfg_cur.band->vfo_a;
    } else {
        fg_params = cfg_cur.band->vfo_b;
    }

    struct {
        const char *name;
        Subject *subj;
    } items[] = {
        {"rfg", cfg_cur.band->rfg.val},
        // fg params, save as vfoa
        {"vfoa_freq", fg_params.freq.val},
        {"vfoa_mode", fg_params.mode.val},
        {"vfoa_agc", fg_params.agc.val},
        {"vfoa_pre", fg_params.pre.val},
        {"vfoa_att", fg_params.att.val},
    };

    int rc;

    sqlite3_stmt *stmt = write_stmt;
    pthread_mutex_lock(&write_mutex);
    uint32_t items_len = sizeof(items) / sizeof(items[0]);
    for (size_t i = 0; i < items_len; i++)
    {
        rc = sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":name"), items[i].name, strlen(items[i].name), 0);
        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Failed to bind name %s: %s", items[i].name, sqlite3_errmsg(db));
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            pthread_mutex_unlock(&write_mutex);
            return;
        }
        rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":id"), id);
        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Failed to bind id %i: %s", id, sqlite3_errmsg(db));
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            pthread_mutex_unlock(&write_mutex);
            return;
        }
        int32_t val = subject_get_int(items[i].subj);
        rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":val"), val);
        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Failed to bind val %i: %s", val, sqlite3_errmsg(db));
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            pthread_mutex_unlock(&write_mutex);
            return;
        }
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            LV_LOG_ERROR("Failed save item %s: %s", items[i].name, sqlite3_errmsg(db));
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    pthread_mutex_unlock(&write_mutex);
}


inline static void fill_data(const char *name, int32_t val, memory_data_t *mem_data) {
    if (STR_EQUAL(name, "rfg")) {
        mem_data->rfg.val = val;
        mem_data->rfg.loaded = true;
    } else if (STR_EQUAL(name, "vfoa_freq")) {
        mem_data->freq.val = val;
        mem_data->freq.loaded = true;
    } else if (STR_EQUAL(name, "vfoa_mode")) {
        mem_data->mode.val = val;
        mem_data->mode.loaded = true;
    } else if (STR_EQUAL(name, "vfoa_agc")) {
        mem_data->agc.val = val;
        mem_data->agc.loaded = true;
    } else if (STR_EQUAL(name, "vfoa_pre")) {
        mem_data->pre.val = val;
        mem_data->pre.loaded = true;
    } else if (STR_EQUAL(name, "vfoa_att")) {
        mem_data->att.val = val;
        mem_data->att.loaded = true;
    }
}
